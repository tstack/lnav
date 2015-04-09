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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file grep_proc.hh
 */

#ifndef __grep_proc_hh
#define __grep_proc_hh

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>

#ifdef HAVE_PCRE_H
#include <pcre.h>
#elif HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#endif

#include <deque>
#include <string>
#include <vector>
#include <exception>

#include "pcrepp.hh"
#include "auto_fd.hh"
#include "auto_mem.hh"
#include "lnav_log.hh"
#include "strong_int.hh"
#include "line_buffer.hh"

/** Strongly-typed integer for matched line numbers. */
STRONG_INT_TYPE(int, grep_line);

class grep_proc;

/**
 * Data source for lines to be searched using a grep_proc.
 */
class grep_proc_source {
public:
    virtual ~grep_proc_source() { };

    /**
     * Get the value for a particular line in the source.
     *
     * @param line The line to retrieve.
     * @param value_out The destination for the line value.
     */
    virtual bool grep_value_for_line(int line, std::string &value_out) = 0;
};

/**
 * Delegate interface for control messages from the grep_proc.
 */
class grep_proc_control {
public:

    virtual ~grep_proc_control() { };

    /** @param msg The error encountered while attempting the grep. */
    virtual void grep_error(std::string msg) { };
};

/**
 * Sink for matches produced by a grep_proc instance.
 */
class grep_proc_sink {
public:
    virtual ~grep_proc_sink() { };

    /** Called at the start of a new grep run. */
    virtual void grep_begin(grep_proc &gp) { };

    /** Called periodically between grep_begin and grep_end. */
    virtual void grep_end_batch(grep_proc &gp) { };

    /** Called at the end of a grep run. */
    virtual void grep_end(grep_proc &gp) { };

    /**
     * Called when a match is found on 'line' and between [start, end).
     *
     * @param line The line number that matched.
     * @param start The offset within the line where the match begins.
     * @param end The offset of the character after the last character in the
     * match.
     */
    virtual void grep_match(grep_proc &gp,
                            grep_line_t line,
                            int start,
                            int end) = 0;

    /**
     * Called for each captured substring in the line.
     *
     * @param line The line number that matched.
     * @param start The offset within the line where the capture begins.
     * @param end The offset of the character after the last character in the
     * capture.
     * @param capture The captured substring itself.
     */
    virtual void grep_capture(grep_proc &gp,
                              grep_line_t line,
                              int start,
                              int end,
                              char *capture) { };

    virtual void grep_match_end(grep_proc &gp, grep_line_t line) { };
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
class grep_proc {
public:
    class error
        : public std::exception {
public:
        error(int err)
            : e_err(err) { };

        int e_err;
    };

    /**
     * Construct a grep_proc object.  You must call the start() method
     * to fork off the child process and begin processing.
     *
     * @param code The pcre code to run over the lines of input.
     * @param gps The source of the data to match.
     */
    grep_proc(pcre *code, grep_proc_source &gps);

    virtual ~grep_proc();

    /** @return The code passed in to the constructor. */
    pcre *get_code() { return this->gp_code; };

    /** @param gpd The sink to send resuls to. */
    void set_sink(grep_proc_sink *gpd)
    {
        this->gp_sink = gpd;
        this->reset();
    };

    void reset()
    {
        if (this->gp_sink != NULL) {
            this->gp_sink->grep_begin(*this);
        }
    };

    void invalidate() {
        this->cleanup();
    };

    /** @param gpd The sink to send results to. */
    void set_control(grep_proc_control *gpc)
    {
        this->gp_control = gpc;
    };

    /** @return The sink to send resuls to. */
    grep_proc_sink *get_sink() { return this->gp_sink; };

    /**
     * Queue a request to search the input between the given line numbers.
     *
     * @param start The line number to start the search at.
     * @param stop The line number to stop the search at or -1 to read until
     * the end-of-file.
     */
    void queue_request(grep_line_t start = grep_line_t(0),
                       grep_line_t stop = grep_line_t(-1))
    {
        require(start != -1 || stop == -1);
        require(stop == -1 || start < stop);

        this->gp_queue.push_back(std::make_pair(start, stop));
    };

    /**
     * Start the search requests that have been queued up with queue_request.
     */
    void start(void);

    void update_poll_set(std::vector<struct pollfd> &pollfds)
    {
        if (this->gp_line_buffer.get_fd() != -1) {
            pollfds.push_back((struct pollfd) {
                    this->gp_line_buffer.get_fd(),
                    POLLIN,
                    0
            });
        }
        if (this->gp_err_pipe != -1) {
            pollfds.push_back((struct pollfd) {
                    this->gp_err_pipe,
                    POLLIN,
                    0
            });
        }
    };

    /**
     * Check the fd_set to see if there is any new data to be processed.
     *
     * @param ready_rfds The set of ready-to-read file descriptors.
     */
    void check_poll_set(const std::vector<struct pollfd> &pollfds);

    /** Check the invariants for this object. */
    bool invariant(void)
    {
        require(this->gp_code != NULL);
        if (this->gp_child_started) {
            require(this->gp_child > 0);
            require(this->gp_line_buffer.get_fd() != -1);
        }
        else {
            require(this->gp_pipe_offset == 0);
            /* require(this->gp_child == -1); XXX doesnt work with static destr */
            require(this->gp_line_buffer.get_fd() == -1);
        }

        return true;
    };

protected:

    /**
     * Dispatch a line received from the child.
     */
    void dispatch_line(char *line);

    /**
     * Free any resources used by the object and make sure the child has been
     * terminated.
     */
    void cleanup(void);

    void child_loop(void);

    virtual void child_init(void) { };

    virtual void child_batch(void) { fflush(stdout); };

    virtual void child_term(void) { fflush(stdout); };

    virtual void handle_match(int line,
                              std::string &line_value,
                              int off,
                              int *matches,
                              int count);

    pcrepp             gp_pcre;
    pcre *             gp_code;          /*< The compiled pattern. */
    grep_proc_source & gp_source;        /*< The data source delegate. */

    auto_fd     gp_err_pipe;             /*< Standard error from the child. */
    line_buffer gp_line_buffer;          /*< Standard out from the child. */
    off_t       gp_pipe_offset;

    pid_t gp_child;                     /*<
                                         * The child's pid or zero in the
                                         * child.
                                         */
    bool     gp_child_started;          /*< True if the child was start()'d. */

    /** The queue of search requests. */
    std::deque<std::pair<grep_line_t, grep_line_t> > gp_queue;
    grep_line_t gp_last_line;           /*<
                                         * The last line number received from
                                         * the child.  For multiple matches,
                                         * the line number is only sent once.
                                         */
    grep_line_t gp_highest_line;        /*< The highest numbered line processed
                                         * by the grep child process.  This
                                         * value is used when the start line
                                         * for a queued request is -1.
                                         */
    grep_proc_sink *   gp_sink;         /*< The sink delegate. */
    grep_proc_control *gp_control;      /*< The control delegate. */
};
#endif
