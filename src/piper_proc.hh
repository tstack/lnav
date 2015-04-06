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
 * @file piper_proc.hh
 */

#ifndef __piper_proc_hh
#define __piper_proc_hh

#include <string>
#include <sys/types.h>
#include "auto_fd.hh"

/**
 * Creates a subprocess that reads data from a pipe and writes it to a file so
 * lnav can treat it like any other file and do preads.
 *
 * TODO: Add support for gzipped files.
 */
class piper_proc {
public:
    class error
        : public std::exception {
public:
        error(int err)
            : e_err(err) { };

        int e_err;
    };

    /**
     * Forks a subprocess that will read data from the given file descriptor
     * and write it to a temporary file.
     *
     * @param pipefd The file descriptor to read the file contents from.
     * @param timestamp True if an ISO 8601 timestamp should be prepended onto
     *   the lines read from pipefd.
     * @param filename The name of the file to save the input to, otherwise a
     *   temporary file will be created.
     */
    piper_proc(int pipefd, bool timestamp, const char *filename = NULL);

    bool has_exited();

    /**
     * Terminates the child process.
     */
    virtual ~piper_proc();

    /** @return The file descriptor for the temporary file. */
    int get_fd() { return this->pp_fd.release(); };

    pid_t get_child_pid() const { return this->pp_child; };

private:
    /** A file descriptor that refers to the temporary file. */
    auto_fd pp_fd;

    /** The child process' pid. */
    pid_t pp_child;
};
#endif
