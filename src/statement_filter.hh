/**
 * Copyright (c) 2026, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lnav_statement_filter_hh
#define lnav_statement_filter_hh

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/lnav.console.hh"
#include "base/result.h"
#include "logfile.hh"
#include "logfile_sub_source.hh"
#include "textview_curses.hh"

/**
 * Filters in or out the messages logged by one log statement that log2src
 * found in the source (:filter-in-statement, :filter-out-statement).
 *
 * The statement is given as file:line, but a line moves when the file is
 * edited, so the filter keeps what identifies the statement apart from its
 * line: its file, its function's qualified name, and its message pattern.
 * After each source scan it finds the statement again by those, the one
 * nearest its old line if there are several, and takes its new line.  A
 * statement that can't be found (its message changed, or it was removed) is
 * unresolved: the filter stays, takes no part in filtering, and says why,
 * rather than filtering some other statement.
 *
 * Until log2src knows the file (the source hasn't been added or scanned yet,
 * as when a session is restored at startup), the filter is pending and takes
 * no part in filtering either.  A saved filter carries a hash of its
 * statement's identity (path:line@hash), so it finds the same statement when
 * it's restored even if the file changed in between.
 *
 * Matching never waits for a source scan.  A message that can't be matched
 * until the scan finishes doesn't match, and the filter is marked to run over
 * every line again once it has.
 */
class statement_filter : public text_filter {
public:
    enum class state_t {
        /** log2src doesn't know the file yet. */
        pending,
        /** Filtering the statement's messages. */
        resolved,
        /** The statement is gone; see get_reason(). */
        unresolved,
    };

    statement_filter(type_t type,
                     std::string path,
                     size_t line,
                     std::optional<std::string> ident,
                     size_t index);

    bool matches(std::optional<line_source> ls,
                 const shared_buffer_ref& line) override;

    bool is_active() const override
    {
        return this->sf_state == state_t::resolved;
    }

    std::string to_command() const override;

    /**
     * The statement as the filter's command gives it, "path:line@hash" once
     * resolved: what restores this filter, unlike its id, "path:line".
     */
    std::string get_point() const;

    /**
     * Whether [point] ("path:line", maybe with "@id") is where this filter's
     * statement is, or was when it became unresolved: one of its lines, and a
     * path that is the filter's, or a tail of it at a "/", or that the
     * filter's is a tail of, while the filter has only what it was given.
     */
    bool matches_location(const std::string& point) const;

    /**
     * Whether [other] filters the same statement, as far as can be told:
     * the same file and line once both are resolved, else where one's
     * location is one of the other's lines, as for matches_location().
     */
    bool same_statement(const statement_filter& other) const;

    /** The file, as given until resolved, then as log2src has it. */
    const std::string& get_path() const { return this->sf_path; }

    /** The location as the commands take it, with the shortest path. */
    std::string get_short_location() const;

    state_t get_state() const { return this->sf_state; }

    /**
     * Why the statement is unresolved, or, while pending, why log2src
     * doesn't know the file (empty while a scan may add it).
     */
    const std::string& get_reason() const { return this->sf_reason; }

    /** The statement's function, once resolved. */
    const std::string& get_function() const { return this->sf_name; }

    /**
     * The function's last two parts, like "Server.handle" for
     * "com.example.net.Server.handle" (or "::" separated ones), for a short
     * label; empty until resolved.
     */
    std::string get_short_function() const;

    /**
     * Whether matches() skipped a message because a source scan was running,
     * so the filter has to run again once it's done.  Clears the mark.
     */
    bool take_needs_rerun()
    {
        return this->sf_needs_rerun.exchange(false, std::memory_order_relaxed);
    }

    /** What a resolve() changed. */
    struct resolve_result {
        /** The messages hidden may differ, so the filters must run again. */
        bool rr_changed{false};
        /** The statement became unresolved: why, for the user. */
        std::optional<lnav::console::user_message> rr_message;
        /**
         * A source scan held log2src's statements, so nothing was done and
         * the filter has to be resolved again.
         */
        bool rr_busy{false};
        /** log2src has no file for the filter's path. */
        bool rr_no_file{false};
    };

    /**
     * Finds the statement among log2src's current statements: by its
     * identity once it has one, else by its line.  Never waits for a source
     * scan; see rr_busy.
     */
    resolve_result resolve();

private:
    /** Whether [line] of [path] (a tail will do, either way) is this one. */
    bool at_location(const std::string& path, size_t line) const;

    /** The file, as given until resolved, then as log2src has it. */
    std::string sf_path;
    /** The statement's first line, as of the last resolve. */
    size_t sf_line;
    /** Its last line, as of the last resolve; sf_line until then. */
    size_t sf_end_line;
    /** The identity hash, from the command or the last resolve. */
    std::optional<std::string> sf_ident;
    std::string sf_name;
    std::string sf_pattern;
    /** The log_msg_schema of the statement's messages. */
    std::string sf_schema;
    state_t sf_state{state_t::pending};
    std::string sf_reason;
    /** Set by matches(), from the filter threads; see take_needs_rerun(). */
    std::atomic<bool> sf_needs_rerun{false};
};

namespace lnav::statement_filters {

/**
 * A filter for the statement at [point], "path:line" or "path:line@hash".
 * An error when the point can't be parsed, or when log2src knows the file
 * and has no statement there; pending when it doesn't know the file yet.
 */
Result<std::shared_ptr<statement_filter>, lnav::console::user_message> create(
    text_filter::type_t type, const std::string& point, size_t index);

/**
 * The statement filter in [fs] for [point]: the one whose location it is (a
 * tail of the path will do), which works for one whose statement can't be
 * found any more too.  When a tail fits several, the one for the file
 * log2src finds for it.  Null when there's none.  Doesn't wait for a scan.
 */
std::shared_ptr<text_filter> find(filter_stack& fs, const std::string& point);

/**
 * The statement filter in [fs] for the same statement as [sf], other than
 * [ignore], or null: statement_filter::same_statement().
 */
std::shared_ptr<text_filter> find_same(filter_stack& fs,
                                       const statement_filter& sf,
                                       const text_filter* ignore = nullptr);

/** The error for filtering a statement [existing] already filters. */
std::string already_filtered_message(const text_filter& existing);

/**
 * [path]:[line] with the shortest tail of the path that log2src finds as the
 * same file, like "Server.java:42": what the statement commands take, for
 * completions and suggestions.  The full path while a source scan runs.
 */
std::string short_location(const std::string& path, size_t line);

/**
 * The short location of the statement that logged the message at [ll], or
 * nullopt when log2src has none for it.  The message is matched if the
 * statement map doesn't have it, which is quick enough for every message on
 * the screen; while a source scan runs, only what the map knows.
 */
std::optional<std::string> location_at(::logfile& lf,
                                       ::logfile::const_iterator ll);

/**
 * The short location of the statement that logged the message at [row] of
 * [lss]'s view, the focused one for a suggestion, or nullopt when it has
 * none.
 */
std::optional<std::string> location_at_row(logfile_sub_source& lss,
                                           vis_line_t row);

struct check_result {
    /**
     * The indexes of the filters whose messages changed, or that skipped
     * messages during a source scan, which must run over every line again.
     */
    std::vector<size_t> cr_changed;
    /** Why statements became unresolved, for the user. */
    std::vector<lnav::console::user_message> cr_messages;
};

/**
 * Resolves the statement filters in [fs] again once log2src finishes a
 * scan, and collects the ones that skipped messages while it ran.  Cheap
 * when nothing changed: called as the indexes are rebuilt.
 */
check_result check(filter_stack& fs);

}  // namespace lnav::statement_filters

#endif
