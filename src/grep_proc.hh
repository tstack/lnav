/**
 * Copyright (c) 2007-2012, Timothy Stack
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
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file grep_proc.hh
 */

#ifndef grep_proc_hh
#define grep_proc_hh

#include <array>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "base/auto_fd.hh"
#include "base/auto_pid.hh"
#include "base/lnav_log.hh"
#include "line_buffer.hh"
#include "mapbox/variant.hpp"
#include "pcrepp/pcre2pp.hh"
#include "pollable.hh"

template<typename LineType>
class grep_proc;

/**
 * The maximum number of patterns a single grep_proc can search for at once.
 * Each pattern occupies a slot and is identified by its bit in a
 * grep_pattern_mask_t.
 */
constexpr size_t GREP_MAX_PATTERNS = 32;

/**
 * A bitmask over the pattern slots of a grep_proc.  Requests carry a mask of
 * the patterns they should be matched against and matches report the mask of
 * the patterns that hit, so that a single pass over the source can service
 * several searches at once.
 */
using grep_pattern_mask_t = uint32_t;

static_assert(GREP_MAX_PATTERNS
              <= sizeof(grep_pattern_mask_t) * 8);

/** @return The mask bit for the pattern in the given slot. */
constexpr grep_pattern_mask_t
grep_pattern_bit(size_t slot)
{
    return static_cast<grep_pattern_mask_t>(1) << slot;
}

/**
 * Data source for lines to be searched using a grep_proc.
 */
template<typename LineType>
class grep_proc_source {
public:
    virtual ~grep_proc_source() = default;

    virtual void register_proc(grep_proc<LineType>* proc)
    {
        this->gps_proc = proc;
    }

    /**
     * Get the value for a particular line in the source.
     *
     * @param line The line to retrieve.
     * @param value_out The destination for the line value.
     */
    virtual std::optional<line_info> grep_value_for_line(LineType line,
                                                         std::string& value_out)
        = 0;

    virtual LineType grep_initial_line(LineType start, LineType highest)
    {
        if (start == -1) {
            return highest;
        }
        return start;
    }

    virtual void grep_next_line(LineType& line) { line = line + LineType(1); }

    grep_proc<LineType>* gps_proc;
};

/**
 * Delegate interface for control messages from the grep_proc.
 */
class grep_proc_control {
public:
    virtual ~grep_proc_control() = default;

    /** @param msg The error encountered while attempting the grep. */
    virtual void grep_error(const std::string& msg) {}
};

/**
 * Sink for matches produced by a grep_proc instance.
 */
template<typename LineType>
class grep_proc_sink {
public:
    virtual ~grep_proc_sink() = default;

    virtual void grep_quiesce() {}

    /**
     * Called at the start of a new grep run.
     *
     * @param patterns The patterns that this run will match against.  Any
     * results previously recorded for these patterns in [start, stop) are
     * about to be replaced.
     */
    virtual void grep_begin(grep_proc<LineType>& gp,
                            LineType start,
                            LineType stop,
                            grep_pattern_mask_t patterns)
    {
    }

    /** Called periodically between grep_begin and grep_end. */
    virtual void grep_end_batch(grep_proc<LineType>& gp) {}

    /** Called at the end of a grep run. */
    virtual void grep_end(grep_proc<LineType>& gp) {}

    /**
     * Called when a match is found on 'line'.
     *
     * @param line The line number that matched.
     * @param patterns The patterns that matched the line.
     */
    virtual void grep_match(grep_proc<LineType>& gp,
                            LineType line,
                            grep_pattern_mask_t patterns)
        = 0;
};

/**
 * "Grep" that runs in a separate process so it doesn't stall user-interaction.
 * This class manages the child process and any interactions between the parent
 * and child.  The source data to be matched comes from the grep_proc_source
 * delegate and the results are sent to the grep_proc_sink delegate in the
 * parent process.
 *
 * Note: The "grep" executable is not actually used, instead we use the pcre(3)
 * library directly.
 */
template<typename LineType>
class grep_proc : public pollable {
public:
    class error : public std::exception {
    public:
        error(int err) : e_err(err) {};

        int e_err;
    };

    /**
     * Construct a grep_proc object.  You must call the start() method
     * to fork off the child process and begin processing.
     *
     * @param code The pcre code to run over the lines of input.  It is
     * installed in slot zero; use set_pattern() to add more.
     * @param gps The source of the data to match.
     */
    grep_proc(std::shared_ptr<lnav::pcre2pp::code> code,
              grep_proc_source<LineType>& gps,
              std::shared_ptr<pollable_supervisor> ps);

    grep_proc(std::shared_ptr<pollable_supervisor>);

    grep_proc(const grep_proc&) = delete;
    grep_proc& operator=(const grep_proc&) = delete;

    using injectable = grep_proc(std::shared_ptr<pollable_supervisor>);

    ~grep_proc() override;

    /** @param gpd The sink to send results to. */
    void set_sink(grep_proc_sink<LineType>* gpd) { this->gp_sink = gpd; }

    /**
     * Install a pattern in the given slot.  Slots are allocated by the owner
     * of this object.  The pattern set is captured by the child processes when
     * start() is called, so a change only takes effect on the next run.
     *
     * @return The mask bit for the slot.
     */
    grep_pattern_mask_t set_pattern(size_t slot,
                                    std::shared_ptr<lnav::pcre2pp::code> code)
    {
        require(slot < GREP_MAX_PATTERNS);

        this->gp_patterns[slot] = std::move(code);
        return grep_pattern_bit(slot);
    }

    /** Remove the pattern in the given slot, if any. */
    void clear_pattern(size_t slot)
    {
        require(slot < GREP_MAX_PATTERNS);

        this->gp_patterns[slot] = nullptr;
    }

    /** @return The pattern in the given slot, or nullptr. */
    const std::shared_ptr<lnav::pcre2pp::code>& get_pattern(size_t slot) const
    {
        require(slot < GREP_MAX_PATTERNS);

        return this->gp_patterns[slot];
    }

    /** @return The mask of every slot that currently holds a pattern. */
    grep_pattern_mask_t all_patterns_mask() const
    {
        grep_pattern_mask_t retval = 0;

        for (size_t slot = 0; slot < GREP_MAX_PATTERNS; slot++) {
            if (this->gp_patterns[slot] != nullptr) {
                retval |= grep_pattern_bit(slot);
            }
        }

        return retval;
    }

    /**
     * Drop any pending work for the given patterns.  Requests that cover other
     * patterns are re-queued with the remaining bits.
     *
     * Note that the children are killed outright, so work that has already been
     * dispatched for the surviving patterns is lost and must be queued again by
     * the caller.
     */
    grep_proc& invalidate(grep_pattern_mask_t patterns);

    grep_proc& invalidate() { return this->invalidate(~0U); }

    /** @param gpc The control to send results to. */
    void set_control(grep_proc_control* gpc) { this->gp_control = gpc; }

    /** @return The sink to send results to. */
    grep_proc_sink<LineType>* get_sink() { return this->gp_sink; }

    enum class until_type_t {
        line,
        eof,
    };

    struct request_until_t {
        until_type_t ru_type;
        LineType ru_line;
    };

    template<typename T>
    static request_until_t until_line(T line)
    {
        return {until_type_t::line, LineType(line)};
    }

    template<typename T>
    static request_until_t until_eof(T line)
    {
        return {until_type_t::eof, LineType(line)};
    }

    struct request_t {
        LineType r_start;
        request_until_t r_until;
        grep_pattern_mask_t r_patterns;
    };

    /**
     * Queue a request to search the input between the given line numbers.
     *
     * @param start The line number to start the search at.
     * @param stop The stop condition.
     * @param patterns The patterns to match against in this range.  Defaults
     * to every pattern that is currently installed.
     */
    grep_proc& queue_request(LineType start,
                             request_until_t stop,
                             std::optional<grep_pattern_mask_t> patterns
                             = std::nullopt);

    /**
     * Start the search requests that have been queued up with queue_request.
     */
    void start();

    void update_poll_set(std::vector<struct pollfd>& pollfds) override;

    /**
     * Check the fd_set to see if there is any new data to be processed.
     *
     * @param ready_rfds The set of ready-to-read file descriptors.
     */
    void check_poll_set(const std::vector<struct pollfd>& pollfds) override;

    bool children_active() const { return !this->gp_children.empty(); }

    /** Check the invariants for this object. */
    bool invariant() { return true; }

    struct child_state {
        auto_pid<process_state::running> cs_child;
        auto_fd cs_err_pipe;
        line_buffer cs_line_buffer;
        file_range cs_pipe_range;

        explicit child_state(auto_pid<process_state::running> child)
            : cs_child(std::move(child))
        {
        }
    };

protected:
    /**
     * Dispatch a line received from the child.
     */
    void dispatch_line(const string_fragment& line);

    /**
     * Free any resources used by the object and make sure the child has been
     * terminated.
     */
    void cleanup();

    void child_loop();

    virtual void child_init() {};

    virtual void child_batch() { fflush(stdout); }

    virtual void child_term() { fflush(stdout); }

    /** The patterns to match, indexed by slot. */
    std::array<std::shared_ptr<lnav::pcre2pp::code>, GREP_MAX_PATTERNS>
        gp_patterns;
    grep_proc_source<LineType>& gp_source; /*< The data source delegate. */

    std::vector<std::unique_ptr<child_state>> gp_children;
    size_t gp_child_queue_size{0};

    /** The queue of search requests. */
    std::deque<request_t> gp_queue;
    /**
     * The requests handed to the current set of children.  Retained so that
     * invalidate() can re-queue the work for any patterns that it is not
     * cancelling.
     */
    std::deque<request_t> gp_dispatched;
    LineType gp_highest_line{0}; /*< The highest numbered line processed
                                  * by the grep child process.  This
                                  * value is used when the start line
                                  * for a queued request is -1.
                                  */
    grep_proc_sink<LineType>* gp_sink{nullptr}; /*< The sink delegate. */
    grep_proc_control* gp_control{nullptr}; /*< The control delegate. */
};

#endif
