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

#include <cstdlib>

#include "statement_filter.hh"

#include "all_logs_vtab.hh"
#include "base/lnav_log.hh"
#include "base/string_util.hh"
#include "config.h"
#include "fmt/format.h"
#include "hasher.hh"
#include "logfile.hh"
#include "logline_window.hh"
#include "pcrepp/pcre2pp.hh"
#include "yajlpp/yajlpp.hh"

#ifdef HAVE_RUST_DEPS
#    include "lnav_rs_ext.cxx.hh"
#endif

namespace {

/** How many characters of the identity hash a saved filter carries. */
constexpr size_t IDENT_LENGTH = 12;

/**
 * The part of a statement that survives edits elsewhere in its file: its
 * function and message pattern, hashed.  The file is kept separately.
 */
std::string
identity_hash(const std::string& name, const std::string& pattern)
{
    hasher h;
    h.update(name);
    h.update(pattern);
    char buf[hasher::STRING_SIZE];
    h.to_string(buf);
    return std::string(buf, IDENT_LENGTH);
}

std::string
point_for(const std::string& path, size_t line)
{
    return fmt::format(FMT_STRING("{}:{}"), path, line);
}

/** A statement's location as the commands take it: "path:line[@hash]". */
struct parsed_point {
    std::string pp_path;
    size_t pp_line;
    std::optional<std::string> pp_ident;
};

std::optional<parsed_point>
parse_point(const std::string& point)
{
    static const auto POINT_RE
        = lnav::pcre2pp::code::from_const(R"(^(.+):(\d+)(?:@([0-9a-f]+))?$)");
    thread_local auto md = lnav::pcre2pp::match_data::unitialized();

    if (!POINT_RE.capture_from(point).into(md).found_p()) {
        return std::nullopt;
    }

    parsed_point retval;
    retval.pp_path = md[1]->to_string();
    retval.pp_line = (size_t) strtoull(md[2]->to_string().c_str(), nullptr, 10);
    if (md[3]) {
        retval.pp_ident = md[3]->to_string();
    }
    return retval;
}

/** Whether [tail] is [path], or the end of it after a "/". */
bool
is_path_tail(const std::string& path, const std::string& tail)
{
    if (tail.empty() || tail.size() > path.size()) {
        return false;
    }
    if (tail.size() == path.size()) {
        return path == tail;
    }
    return path.compare(path.size() - tail.size(), tail.size(), tail) == 0
        && path[path.size() - tail.size() - 1] == '/';
}

/** Whether log2src's statements are being scanned for now. */
bool
scan_running()
{
#ifdef HAVE_RUST_DEPS
    return lnav_rs_ext::get_status().status == lnav_rs_ext::Status::working;
#else
    return false;
#endif
}

}  // namespace

statement_filter::statement_filter(type_t type,
                                   std::string path,
                                   size_t line,
                                   std::optional<std::string> ident,
                                   size_t index)
    : text_filter(type, filter_lang_t::STATEMENT, point_for(path, line), index),
      sf_path(std::move(path)), sf_line(line), sf_end_line(line),
      sf_ident(std::move(ident))
{
}

bool
statement_filter::matches(std::optional<line_source> ls,
                          const shared_buffer_ref& line)
{
    if (this->sf_state != state_t::resolved || !ls) {
        return false;
    }
    if (!ls->ls_line->is_message()) {
        return false;
    }

    // The file isn't changed here, it's only read; read_full_message() just
    // isn't marked const.
    auto& lf = const_cast<logfile&>(ls->ls_file);
    auto schema = all_logs_vtab::statement_schema_at(lf, ls->ls_line);
    if (schema.is<all_logs_vtab::stmt_lookup_busy>()) {
        this->sf_needs_rerun.store(true, std::memory_order_relaxed);
        return false;
    }
    return schema.is<std::string>()
        && schema.get<std::string>() == this->sf_schema;
}

std::string
statement_filter::get_point() const
{
    if (this->sf_ident) {
        return fmt::format(FMT_STRING("{}@{}"),
                           point_for(this->sf_path, this->sf_line),
                           this->sf_ident.value());
    }
    return point_for(this->sf_path, this->sf_line);
}

bool
statement_filter::at_location(const std::string& path, size_t line) const
{
    if (line < this->sf_line || line > this->sf_end_line) {
        return false;
    }
    return is_path_tail(this->sf_path, path)
        || is_path_tail(path, this->sf_path);
}

bool
statement_filter::matches_location(const std::string& point) const
{
    auto pp = parse_point(point);
    if (!pp) {
        return false;
    }
    if (pp->pp_ident && pp->pp_ident != this->sf_ident) {
        return false;
    }
    return this->at_location(pp->pp_path, pp->pp_line);
}

bool
statement_filter::same_statement(const statement_filter& other) const
{
    if (this->sf_state == state_t::resolved
        && other.sf_state == state_t::resolved)
    {
        return this->sf_path == other.sf_path && this->sf_line == other.sf_line;
    }
    return this->at_location(other.sf_path, other.sf_line)
        || other.at_location(this->sf_path, this->sf_line);
}

std::string
statement_filter::get_short_function() const
{
    // The start of the part after the separator before [end], or 0.
    auto part_start = [this](size_t end) -> size_t {
        auto pos = this->sf_name.find_last_of(".:", end == 0 ? 0 : end - 1);
        return pos == std::string::npos ? 0 : pos + 1;
    };
    auto fn_start = part_start(this->sf_name.size());
    if (fn_start == 0) {
        return this->sf_name;
    }
    // Back over the separator, one character or "::".
    auto sep = fn_start - 1;
    if (sep > 0 && this->sf_name.compare(sep - 1, 2, "::") == 0) {
        sep -= 1;
    }
    if (sep == 0) {
        return this->sf_name.substr(fn_start);
    }
    return this->sf_name.substr(part_start(sep));
}

std::string
statement_filter::get_short_location() const
{
    return lnav::statement_filters::short_location(this->sf_path,
                                                   this->sf_line);
}

std::string
statement_filter::to_command() const
{
    return (this->lf_type == INCLUDE ? "filter-in-statement "
                                     : "filter-out-statement ")
        + this->get_point();
}

statement_filter::resolve_result
statement_filter::resolve()
{
    resolve_result retval;

#ifdef HAVE_RUST_DEPS
    auto busy = false;
    auto full_path
        = (std::string) lnav_rs_ext::try_find_src_file(this->sf_path, busy);
    if (busy) {
        retval.rr_busy = true;
        return retval;
    }
    if (full_path.empty()) {
        retval.rr_no_file = true;
        if (this->sf_state == state_t::pending) {
            // A scan may be about to add the file; otherwise, it may be a
            // typo, or a source that hasn't been added yet.
            this->sf_reason = scan_running()
                ? std::string()
                : fmt::format(FMT_STRING("the source has no file {}"),
                              this->sf_path);
        }
        if (this->sf_state == state_t::resolved) {
            // The source it came from is gone (the path was dropped, or the
            // file moved), which is the same as the statement being gone.
            this->sf_state = state_t::unresolved;
            this->sf_reason
                = fmt::format(FMT_STRING("the source has no file {} any more"),
                              this->sf_path);
            retval.rr_changed = true;
            retval.rr_message
                = lnav::console::user_message::warning(
                      attr_line_t("the statement filtered by ")
                          .append(lnav::roles::symbol(":" + this->to_command()))
                          .append(" can't be found, so the filter does "
                                  "nothing"))
                      .with_reason(this->sf_reason);
        }
        return retval;
    }

    const auto stmts = lnav_rs_ext::try_get_log_statements_for(full_path, busy);
    if (busy) {
        retval.rr_busy = true;
        return retval;
    }
    const lnav_rs_ext::FindLogResult* found = nullptr;
    auto distance = [this](const lnav_rs_ext::FindLogResult& stmt) {
        return stmt.src.begin_line > this->sf_line
            ? stmt.src.begin_line - this->sf_line
            : this->sf_line - stmt.src.begin_line;
    };
    for (const auto& stmt : stmts) {
        if ((std::string) stmt.src.file != full_path) {
            continue;
        }
        auto name = (std::string) stmt.src.qualified_name;
        auto pattern = (std::string) stmt.pattern;
        bool same;
        if (!this->sf_name.empty() || !this->sf_pattern.empty()) {
            same = name == this->sf_name && pattern == this->sf_pattern;
        } else if (this->sf_ident) {
            same = identity_hash(name, pattern) == this->sf_ident.value();
        } else {
            // A new filter, from the user: the statement on that line.
            same = stmt.src.begin_line <= this->sf_line
                && this->sf_line <= stmt.src.end_line;
        }
        if (same && (found == nullptr || distance(stmt) < distance(*found))) {
            found = &stmt;
        }
    }

    if (found == nullptr) {
        if (this->sf_state == state_t::unresolved) {
            return retval;
        }
        auto had_statement = this->sf_state == state_t::resolved
            || this->sf_ident.has_value();
        this->sf_state = state_t::unresolved;
        this->sf_reason = !had_statement
            // Given by line, and the file, once log2src had it, has no
            // statement there.
            ? fmt::format(FMT_STRING("{} has no log statement on line {}"),
                          full_path,
                          this->sf_line)
            : this->sf_name.empty()
                ? fmt::format(
                      FMT_STRING("no statement in {} logs the same message as "
                                 "when it was hidden"),
                      full_path)
                : fmt::format(
                      FMT_STRING("no statement in {} logs the same message any "
                                 "more: its text changed, or it was removed"),
                      this->sf_name);
        log_info("statement filter %s is unresolved: %s",
                 this->get_id().c_str(),
                 this->sf_reason.c_str());
        // A filter that never had a statement hid nothing.
        retval.rr_changed = had_statement;
        retval.rr_message
            = lnav::console::user_message::warning(
                  attr_line_t("the statement filtered by ")
                      .append(lnav::roles::symbol(":" + this->to_command()))
                      .append(" can't be found, so the filter does nothing"))
                  .with_reason(this->sf_reason)
                  .with_help(
                      had_statement
                          ? "use :delete-filter with the same location to "
                            "remove the filter, and filter the statement again "
                            "at its new location"
                          : "use :delete-filter with the same location to "
                            "remove the filter, and give a line of a statement "
                            "that source_log_stmt() lists for the file");
        return retval;
    }

    char schema_buf[hasher::STRING_SIZE];
    all_logs_vtab::statement_schema(string_fragment::from(found->src_json),
                                    string_fragment::from(found->pattern),
                                    schema_buf);
    auto schema = std::string(schema_buf);
    auto new_line = (size_t) found->src.begin_line;
    if (this->sf_state != state_t::resolved || schema != this->sf_schema) {
        retval.rr_changed = true;
    }
    if (new_line != this->sf_line || this->sf_path != full_path) {
        log_info("statement filter %s moved to %s:%zu",
                 this->get_id().c_str(),
                 full_path.c_str(),
                 new_line);
    }
    this->sf_path = full_path;
    this->sf_line = new_line;
    this->sf_end_line = std::max(new_line, (size_t) found->src.end_line);
    this->sf_name = (std::string) found->src.qualified_name;
    this->sf_pattern = (std::string) found->pattern;
    this->sf_ident = identity_hash(this->sf_name, this->sf_pattern);
    this->sf_schema = std::move(schema);
    this->sf_state = state_t::resolved;
    this->sf_reason.clear();
    this->set_id(point_for(this->sf_path, this->sf_line));
#endif

    return retval;
}

namespace lnav::statement_filters {

Result<std::shared_ptr<statement_filter>, lnav::console::user_message>
create(text_filter::type_t type, const std::string& point, size_t index)
{
    auto pp = parse_point(point);
    if (!pp) {
        return Err(lnav::console::user_message::error(
                       attr_line_t("invalid statement location: ")
                           .append_quoted(point))
                       .with_help("expecting a path and line, like "
                                  "src/Server.java:42"));
    }

    auto retval = std::make_shared<statement_filter>(
        type, pp->pp_path, pp->pp_line, pp->pp_ident, index);
#ifdef HAVE_RUST_DEPS
    auto res = retval->resolve();
    if (!res.rr_busy && !res.rr_no_file && !pp->pp_ident
        && retval->get_state() != statement_filter::state_t::resolved)
    {
        return Err(lnav::console::user_message::error(
                       attr_line_t("no log statement at ")
                           .append(lnav::roles::file(point)))
                       .with_help("give a line of a statement that "
                                  "source_log_stmt() lists for the file"));
    }
#endif
    log_info("statement filter %s: %s",
             retval->get_id().c_str(),
             retval->get_state() == statement_filter::state_t::resolved
                 ? "resolved"
                 : "pending until the source is scanned");
    return Ok(retval);
}

std::string
short_location(const std::string& path, size_t line)
{
#ifdef HAVE_RUST_DEPS
    // From the file's name outward: the first tail that log2src resolves to
    // this file is one the commands will too.
    auto pos = path.size();
    while (pos > 0) {
        auto slash = path.rfind('/', pos - 1);
        auto tail = slash == std::string::npos ? path : path.substr(slash + 1);
        if (!tail.empty()) {
            auto busy = false;
            auto found
                = (std::string) lnav_rs_ext::try_find_src_file(tail, busy);
            if (busy) {
                break;
            }
            if (found == path) {
                return point_for(tail, line);
            }
        }
        if (slash == std::string::npos || slash == 0) {
            break;
        }
        pos = slash;
    }
#endif
    return point_for(path, line);
}

std::optional<std::string>
location_at(::logfile& lf, ::logfile::const_iterator ll)
{
    auto src = all_logs_vtab::statement_src_at(lf, ll);
    if (!src.is<std::string>()) {
        return std::nullopt;
    }
    const auto& src_json = src.get<std::string>();

    static const char* FILE_PATH[] = {"file", nullptr};
    static const char* LINE_PATH[] = {"begin_line", nullptr};
    char errbuf[256];
    auto_mem<yajl_val_s> tree(yajl_tree_free);
    tree = yajl_tree_parse(src_json.c_str(), errbuf, sizeof(errbuf));
    if (tree.in() == nullptr) {
        log_warning("unable to read a statement's source: %s", errbuf);
        return std::nullopt;
    }
    auto* file = yajl_tree_get(tree.in(), FILE_PATH, yajl_t_string);
    auto* line = yajl_tree_get(tree.in(), LINE_PATH, yajl_t_number);
    if (file == nullptr || line == nullptr || !YAJL_IS_INTEGER(line)) {
        return std::nullopt;
    }
    return short_location(YAJL_GET_STRING(file),
                          (size_t) YAJL_GET_INTEGER(line));
}

std::optional<std::string>
location_at_row(logfile_sub_source& lss, vis_line_t row)
{
    if (row >= vis_line_t(lss.text_line_count())) {
        return std::nullopt;
    }
    auto win = lss.window_at(row);
    for (const auto& msg : *win) {
        auto* lf = msg.get_file_ptr();
        auto loc = location_at(*lf, lf->begin() + msg.get_file_line_number());
        if (loc) {
            return loc;
        }
    }
    return std::nullopt;
}

std::shared_ptr<text_filter>
find(filter_stack& fs, const std::string& point)
{
    std::vector<std::shared_ptr<text_filter>> found;
    for (auto& tf : fs) {
        const auto* sf = dynamic_cast<const statement_filter*>(tf.get());
        if (sf != nullptr && sf->matches_location(point)) {
            found.emplace_back(tf);
        }
    }
    if (found.size() <= 1) {
        return found.empty() ? nullptr : found.front();
    }

#ifdef HAVE_RUST_DEPS
    // A path tail that more than one filter's file ends with: take the file
    // log2src finds for it, as :filter-in-statement would.
    // matches_location() parsed the point, so it parses.
    auto pp = parse_point(point);
    auto busy = false;
    auto full_path
        = (std::string) lnav_rs_ext::try_find_src_file(pp->pp_path, busy);
    for (const auto& tf : found) {
        const auto* sf = dynamic_cast<const statement_filter*>(tf.get());
        if (!full_path.empty() && sf->get_path() == full_path) {
            return tf;
        }
    }
#endif
    return found.front();
}

std::shared_ptr<text_filter>
find_same(filter_stack& fs,
          const statement_filter& sf,
          const text_filter* ignore)
{
    for (auto& tf : fs) {
        if (tf.get() == ignore || tf->lf_deleted) {
            continue;
        }
        const auto* other = dynamic_cast<const statement_filter*>(tf.get());
        if (other != nullptr && other->same_statement(sf)) {
            return tf;
        }
    }
    return nullptr;
}

std::string
already_filtered_message(const text_filter& existing)
{
    return fmt::format(
        FMT_STRING("the statement at {} is already filtered {}; delete that "
                   "filter first"),
        existing.get_id(),
        existing.get_type() == text_filter::INCLUDE ? "in" : "out");
}

check_result
check(filter_stack& fs)
{
    check_result retval;

#ifdef HAVE_RUST_DEPS
    static size_t last_version = 0;

    // Resolving or running a filter again during a scan would only find
    // log2src busy, so both wait for it to finish.
    auto status = lnav_rs_ext::get_status();
    if (status.status == lnav_rs_ext::Status::working) {
        return retval;
    }
    const auto rescanned = status.version != last_version;
    auto busy = false;

    for (auto& tf : fs) {
        if (tf->get_lang() != filter_lang_t::STATEMENT) {
            continue;
        }
        auto* sf = dynamic_cast<statement_filter*>(tf.get());
        if (sf == nullptr) {
            continue;
        }
        auto changed = false;
        if (rescanned) {
            auto res = sf->resolve();
            busy = busy || res.rr_busy;
            changed = res.rr_changed;
            if (res.rr_message) {
                retval.cr_messages.emplace_back(
                    std::move(res.rr_message.value()));
            }
        }
        if (sf->take_needs_rerun()) {
            log_info("statement filter %s skipped messages during a scan",
                     sf->get_id().c_str());
            changed = true;
        }
        if (changed) {
            retval.cr_changed.emplace_back(sf->get_index());
        }
    }
    // Something wrote log2src's statements after all; try again next time.
    if (!busy) {
        last_version = status.version;
    }
#endif

    return retval;
}

}  // namespace lnav::statement_filters
