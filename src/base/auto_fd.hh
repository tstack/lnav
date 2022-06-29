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

#include <exception>
#include <new>
#include <string>

#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include "base/lnav_log.hh"
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
    static int pipe(auto_fd* af)
    {
        int retval, fd[2];

        require(af != nullptr);

        if ((retval = ::pipe(fd)) == 0) {
            af[0] = fd[0];
            af[1] = fd[1];
        }

        return retval;
    }

    /**
     * dup(2) the given file descriptor and wrap it in an auto_fd.
     *
     * @param fd The file descriptor to duplicate.
     * @return A new auto_fd that contains the duplicated file descriptor.
     */
    static auto_fd dup_of(int fd)
    {
        if (fd == -1) {
            return auto_fd{};
        }

        auto new_fd = ::dup(fd);

        if (new_fd == -1) {
            throw std::bad_alloc();
        }

        return auto_fd(new_fd);
    }

    /**
     * Construct an auto_fd to manage the given file descriptor.
     *
     * @param fd The file descriptor to be managed.
     */
    explicit auto_fd(int fd = -1) : af_fd(fd) { require(fd >= -1); }

    /**
     * Non-const copy constructor.  Management of the file descriptor will be
     * transferred from the source to this object and the source will be
     * cleared.
     *
     * @param af The source of the file descriptor.
     */
    auto_fd(auto_fd&& af) noexcept : af_fd(af.release()) {}

    /**
     * Const copy constructor.  The file descriptor from the source will be
     * dup(2)'d and the new descriptor stored in this object.
     *
     * @param af The source of the file descriptor.
     */
    auto_fd(const auto_fd& af) = delete;

    auto_fd dup() const
    {
        int new_fd;

        if (this->af_fd == -1 || (new_fd = ::dup(this->af_fd)) == -1) {
            throw std::bad_alloc();
        }

        return auto_fd{new_fd};
    }

    /**
     * Destructor that will close the file descriptor managed by this object.
     */
    ~auto_fd() { this->reset(); }

    /** @return The file descriptor as a plain integer. */
    operator int() const { return this->af_fd; }

    /**
     * Replace the current descriptor with the given one.  The current
     * descriptor will be closed.
     *
     * @param fd The file descriptor to store in this object.
     * @return *this
     */
    auto_fd& operator=(int fd)
    {
        require(fd >= -1);

        this->reset(fd);
        return *this;
    }

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

    /**
     * @return The file descriptor.
     */
    int get() const { return this->af_fd; }

    /**
     * Closes the current file descriptor and replaces its value with the given
     * one.
     *
     * @param fd The new file descriptor to be managed.
     */
    void reset(int fd = -1)
    {
        require(fd >= -1);

        if (this->af_fd != fd) {
            if (this->af_fd != -1) {
                switch (this->af_fd) {
                    case STDIN_FILENO:
                    case STDOUT_FILENO:
                    case STDERR_FILENO:
                        break;
                    default:
                        close(this->af_fd);
                        break;
                }
            }
            this->af_fd = fd;
        }
    }

    void close_on_exec() const
    {
        if (this->af_fd == -1) {
            return;
        }
        log_perror(fcntl(this->af_fd, F_SETFD, FD_CLOEXEC));
    }

private:
    int af_fd; /*< The managed file descriptor. */
};

class auto_pipe {
public:
    static Result<auto_pipe, std::string> for_child_fd(int child_fd)
    {
        auto_pipe retval(child_fd);

        if (retval.open() == -1) {
            return Err(std::string(strerror(errno)));
        }

        return Ok(std::move(retval));
    }

    explicit auto_pipe(int child_fd = -1, int child_flags = O_RDONLY)
        : ap_child_flags(child_flags), ap_child_fd(child_fd)
    {
        switch (child_fd) {
            case STDIN_FILENO:
                this->ap_child_flags = O_RDONLY;
                break;
            case STDOUT_FILENO:
            case STDERR_FILENO:
                this->ap_child_flags = O_WRONLY;
                break;
        }
    }

    int open() { return auto_fd::pipe(this->ap_fd); }

    void close()
    {
        this->ap_fd[0].reset();
        this->ap_fd[1].reset();
    }

    auto_fd& read_end() { return this->ap_fd[0]; }

    auto_fd& write_end() { return this->ap_fd[1]; }

    void after_fork(pid_t child_pid)
    {
        int new_fd;

        switch (child_pid) {
            case -1:
                this->close();
                break;
            case 0:
                if (this->ap_child_flags == O_RDONLY) {
                    this->write_end().reset();
                    if (this->read_end().get() == -1) {
                        this->read_end() = ::open("/dev/null", O_RDONLY);
                    }
                    new_fd = this->read_end().get();
                } else {
                    this->read_end().reset();
                    if (this->write_end().get() == -1) {
                        this->write_end() = ::open("/dev/null", O_WRONLY);
                    }
                    new_fd = this->write_end().get();
                }
                if (this->ap_child_fd != -1) {
                    if (new_fd != this->ap_child_fd) {
                        dup2(new_fd, this->ap_child_fd);
                        this->close();
                    }
                }
                break;
            default:
                if (this->ap_child_flags == O_RDONLY) {
                    this->read_end().reset();
                } else {
                    this->write_end().reset();
                }
                break;
        }
    }

    int ap_child_flags;
    int ap_child_fd;
    auto_fd ap_fd[2];
};

#endif
