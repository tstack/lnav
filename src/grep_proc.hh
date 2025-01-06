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

#include <deque>
#include <exception>
#include <string>
#include <vector>

#include <poll.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include "base/auto_fd.hh"
#include "base/lnav_log.hh"
#include "line_buffer.hh"
#include "pcrepp/pcre2pp.hh"
#include "pollable.hh"

template<typename LineType>
class grep_proc;

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

    /** Called at the start of a new grep run. */
    virtual void grep_begin(grep_proc<LineType>& gp,
                            LineType start,
                            LineType stop)
    {
    }

    /** Called periodically between grep_begin and grep_end. */
    virtual void grep_end_batch(grep_proc<LineType>& gp) {}

    /** Called at the end of a grep run. */
    virtual void grep_end(grep_proc<LineType>& gp) {}

    /**
     * Called when a match is found on 'line' and between [start, end).
     *
     * @param line The line number that matched.
     * @param start The offset within the line where the match begins.
     * @param end The offset of the character after the last character in the
     * match.
     */
    virtual void grep_match(grep_proc<LineType>& gp, LineType line) = 0;
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
     * @param code The pcre code to run over the lines of input.
     * @param gps The source of the data to match.
     */
    grep_proc(std::shared_ptr<lnav::pcre2pp::code> code,
              grep_proc_source<LineType>& gps,
              std::shared_ptr<pollable_supervisor> ps);

    grep_proc(std::shared_ptr<pollable_supervisor>);

    using injectable = grep_proc(std::shared_ptr<pollable_supervisor>);

    ~grep_proc() override;

    /** @param gpd The sink to send results to. */
    void set_sink(grep_proc_sink<LineType>* gpd) { this->gp_sink = gpd; }

    grep_proc& invalidate();

    /** @param gpc The control to send results to. */
    void set_control(grep_proc_control* gpc) { this->gp_control = gpc; }

    /** @return The sink to send results to. */
    grep_proc_sink<LineType>* get_sink() { return this->gp_sink; }

    /**
     * Queue a request to search the input between the given line numbers.
     *
     * @param start The line number to start the search at.
     * @param stop The line number to stop the search at (exclusive) or -1 to
     * read until the end-of-file.
     */
    grep_proc& queue_request(LineType start = LineType(0),
                             LineType stop = LineType(-1))
    {
        require(start != -1 || stop == -1);
        require(stop == -1 || start < stop);

        this->gp_queue.emplace_back(start, stop);
        if (this->gp_sink) {
            this->gp_sink->grep_begin(*this, start, stop);
        }

        return *this;
    }

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

    /** Check the invariants for this object. */
    bool invariant()
    {
        if (this->gp_child_started) {
            require(this->gp_child > 0);
            require(this->gp_line_buffer.get_fd() != -1);
        } else {
            /* require(this->gp_child == -1); XXX doesnt work with static destr
             */
            require(this->gp_line_buffer.get_fd() == -1);
        }

        return true;
    }

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

    std::shared_ptr<lnav::pcre2pp::code> gp_pcre;
    grep_proc_source<LineType>& gp_source; /*< The data source delegate. */

    auto_fd gp_err_pipe; /*< Standard error from the child. */
    line_buffer gp_line_buffer; /*< Standard out from the child. */
    file_range gp_pipe_range;

    pid_t gp_child{-1}; /*<
                         * The child's pid or zero in the
                         * child.
                         */
    bool gp_child_started{false}; /*< True if the child was start()'d. */
    size_t gp_child_queue_size{0};

    /** The queue of search requests. */
    std::deque<std::pair<LineType, LineType> > gp_queue;
    LineType gp_last_line{0}; /*<
                               * The last line number received from
                               * the child.  For multiple matches,
                               * the line number is only sent once.
                               */
    LineType gp_highest_line; /*< The highest numbered line processed
                               * by the grep child process.  This
                               * value is used when the start line
                               * for a queued request is -1.
                               */
    grep_proc_sink<LineType>* gp_sink{nullptr}; /*< The sink delegate. */
    grep_proc_control* gp_control{nullptr}; /*< The control delegate. */
};

#endif
