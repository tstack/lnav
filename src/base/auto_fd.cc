/**
 * Copyright (c) 2023, Timothy Stack
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
 * @file auto_fd.cc
 */

#include "auto_fd.hh"

#include <fcntl.h>
#include <unistd.h>

#include "lnav_log.hh"

int
auto_fd::pipe(auto_fd* af)
{
    int retval, fd[2];

    require(af != nullptr);

    if ((retval = ::pipe(fd)) == 0) {
        af[0] = fd[0];
        af[1] = fd[1];
    }

    return retval;
}

auto_fd
auto_fd::dup_of(int fd)
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

Result<auto_fd, std::string>
auto_fd::openpt(int flags)
{
    auto rc = posix_openpt(flags);
    if (rc == -1) {
        return Err(fmt::format(FMT_STRING("posix_openpt() failed: {}"),
                               strerror(errno)));
    }

    return Ok(auto_fd{rc});
}

auto_fd::
auto_fd(int fd)
    : af_fd(fd)
{
    require(fd >= -1);
}

auto_fd::
auto_fd(auto_fd&& af) noexcept
    : af_fd(af.release())
{
}

auto_fd
auto_fd::dup() const
{
    int new_fd;

    if (this->af_fd == -1 || (new_fd = ::dup(this->af_fd)) == -1) {
        throw std::bad_alloc();
    }

    return auto_fd{new_fd};
}

auto_fd::~
auto_fd()
{
    this->reset();
}

void
auto_fd::copy_to(int fd) const
{
    dup2(this->get(), fd);
}

void
auto_fd::reset(int fd)
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

void
auto_fd::close_on_exec() const
{
    if (this->af_fd == -1) {
        return;
    }
    log_perror(fcntl(this->af_fd, F_SETFD, FD_CLOEXEC));
}

void
auto_fd::non_blocking() const
{
    auto fl = fcntl(this->af_fd, F_GETFL, 0);
    if (fl < 0) {
        return;
    }

    log_perror(fcntl(this->af_fd, F_SETFL, fl | O_NONBLOCK));
}

auto_fd&
auto_fd::operator=(int fd)
{
    require(fd >= -1);

    this->reset(fd);
    return *this;
}

Result<void, std::string>
auto_fd::write_fully(string_fragment sf)
{
    while (!sf.empty()) {
        auto rc = write(this->af_fd, sf.data(), sf.length());

        if (rc < 0) {
            return Err(
                fmt::format(FMT_STRING("failed to write {} bytes to FD {}"),
                            sf.length(),
                            this->af_fd));
        }

        sf = sf.substr(rc);
    }

    return Ok();
}

Result<auto_pipe, std::string>
auto_pipe::for_child_fd(int child_fd)
{
    auto_pipe retval(child_fd);

    if (retval.open() == -1) {
        return Err(std::string(strerror(errno)));
    }

    return Ok(std::move(retval));
}

auto_pipe::
auto_pipe(int child_fd, int child_flags)
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

void
auto_pipe::after_fork(pid_t child_pid)
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

int
auto_pipe::open()
{
    int retval = auto_fd::pipe(this->ap_fd);
    this->ap_fd[0].close_on_exec();
    this->ap_fd[1].close_on_exec();
    return retval;
}
