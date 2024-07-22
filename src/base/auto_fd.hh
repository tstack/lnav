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
 * @file auto_fd.hh
 */

#ifndef auto_fd_hh
#define auto_fd_hh

#include <string>

#include <fcntl.h>

#include "base/intern_string.hh"
#include "base/result.h"

/**
 * Resource management class for file descriptors.
 *
 * @see auto_ptr
 */
class auto_fd {
public:
    /**
     * Wrapper for the posix pipe(2) function that stores the file descriptor
     * results in an auto_fd array.
     *
     * @param af An array of at least two auto_fd elements, where the first
     * contains the reader end of the pipe and the second contains the writer.
     * @return The result of the pipe(2) function.
     */
    static int pipe(auto_fd* af);

    /**
     * dup(2) the given file descriptor and wrap it in an auto_fd.
     *
     * @param fd The file descriptor to duplicate.
     * @return A new auto_fd that contains the duplicated file descriptor.
     */
    static auto_fd dup_of(int fd);

    static Result<auto_fd, std::string> openpt(int flags);

    /**
     * Construct an auto_fd to manage the given file descriptor.
     *
     * @param fd The file descriptor to be managed.
     */
    explicit auto_fd(int fd = -1);

    /**
     * Non-const copy constructor.  Management of the file descriptor will be
     * transferred from the source to this object and the source will be
     * cleared.
     *
     * @param af The source of the file descriptor.
     */
    auto_fd(auto_fd&& af) noexcept;

    /**
     * Const copy constructor.  The file descriptor from the source will be
     * dup(2)'d and the new descriptor stored in this object.
     *
     * @param af The source of the file descriptor.
     */
    auto_fd(const auto_fd& af) = delete;

    auto_fd dup() const;

    /**
     * Destructor that will close the file descriptor managed by this object.
     */
    ~auto_fd();

    /** @return The file descriptor as a plain integer. */
    operator int() const { return this->af_fd; }

    /**
     * Replace the current descriptor with the given one.  The current
     * descriptor will be closed.
     *
     * @param fd The file descriptor to store in this object.
     * @return *this
     */
    auto_fd& operator=(int fd);

    /**
     * Transfer management of the given file descriptor to this object.
     *
     * @param af The old manager of the file descriptor.
     * @return *this
     */
    auto_fd& operator=(auto_fd&& af) noexcept
    {
        this->reset(af.release());
        return *this;
    }

    /**
     * Return a pointer that can be passed to functions that require an out
     * parameter for file descriptors (e.g. openpty).
     *
     * @return A pointer to the internal integer.
     */
    int* out()
    {
        this->reset();
        return &this->af_fd;
    }

    /**
     * Stop managing the file descriptor in this object and return its value.
     *
     * @return The file descriptor.
     */
    int release()
    {
        int retval = this->af_fd;

        this->af_fd = -1;
        return retval;
    }

    void copy_to(int fd) const;

    /**
     * @return The file descriptor.
     */
    int get() const { return this->af_fd; }

    bool has_value() const { return this->af_fd != -1; }

    /**
     * Closes the current file descriptor and replaces its value with the given
     * one.
     *
     * @param fd The new file descriptor to be managed.
     */
    void reset(int fd = -1);

    Result<void, std::string> write_fully(string_fragment sf);

    void close_on_exec() const;

    void non_blocking() const;

private:
    int af_fd; /*< The managed file descriptor. */
};

class auto_pipe {
public:
    static Result<auto_pipe, std::string> for_child_fd(int child_fd);

    template<typename... ARGS>
    static Result<std::array<auto_pipe, sizeof...(ARGS)>, std::string>
    for_child_fds(ARGS... args)
    {
        std::array<auto_pipe, sizeof...(ARGS)> retval;

        size_t index = 0;
        for (const auto child_fd : {args...}) {
            auto open_res = for_child_fd(child_fd);
            if (open_res.isErr()) {
                return Err(open_res.unwrapErr());
            }

            retval[index++] = open_res.unwrap();
        }

        return Ok(std::move(retval));
    }

    explicit auto_pipe(int child_fd = -1, int child_flags = O_RDONLY);

    int open();

    void close()
    {
        this->ap_fd[0].reset();
        this->ap_fd[1].reset();
    }

    auto_fd& read_end() { return this->ap_fd[0]; }

    auto_fd& write_end() { return this->ap_fd[1]; }

    void after_fork(pid_t child_pid);

    int ap_child_flags;
    int ap_child_fd;
    auto_fd ap_fd[2];
};

#endif
