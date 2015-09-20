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
 * @file termios_guard.hh
 */

#ifndef __termios_guard_hh
#define __termios_guard_hh

#include <stdio.h>
#include <termios.h>
#include <unistd.h>

/**
 * RAII class that saves the current termios for a tty and then restores them
 * during destruction.
 */
class guard_termios {
public:

    /**
     * Store the TTY termios settings in this object.
     *
     * @param fd The tty file descriptor.
     */
    guard_termios(const int fd) : gt_fd(fd)
    {
        memset(&this->gt_termios, 0, sizeof(this->gt_termios));
        if (isatty(this->gt_fd) &&
            tcgetattr(this->gt_fd, &this->gt_termios) == -1) {
            perror("tcgetattr");
        }
    };

    /**
     * Restore the TTY termios settings that were captured when this object was
     * instantiated.
     */
    ~guard_termios()
    {
        if (isatty(this->gt_fd) &&
            tcsetattr(this->gt_fd, TCSANOW, &this->gt_termios) == -1) {
            perror("tcsetattr");
        }
    };

    const struct termios *get_termios() const { return &this->gt_termios; };

private:
    const int      gt_fd;
    struct termios gt_termios;
};
#endif
