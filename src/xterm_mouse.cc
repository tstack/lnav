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
 */

#include "xterm_mouse.hh"

#include <unistd.h>

#include "base/lnav_log.hh"
#include "config.h"

const char* xterm_mouse::XT_TERMCAP = "\033[?1000%?%p1%{1}%=%th%el%;";
const char* xterm_mouse::XT_TERMCAP_TRACKING = "\033[?1002%?%p1%{1}%=%th%el%;";
const char* xterm_mouse::XT_TERMCAP_SGR = "\033[?1006%?%p1%{1}%=%th%el%;";

void
xterm_mouse::handle_mouse()
{
    bool release = false;
    size_t index = 0;
    int bstate, x, y;
    char buffer[64];
    bool done = false;

    while (!done) {
        if (index >= sizeof(buffer) - 1) {
            break;
        }
        auto ch = getch();
        switch (ch) {
            case 'm':
                release = true;
                done = true;
                break;
            case 'M':
                done = true;
                break;
            default:
                buffer[index++] = (char) ch;
                break;
        }
    }
    buffer[index] = '\0';

    if (sscanf(buffer, "%d;%d;%d", &bstate, &x, &y) == 3) {
        if (this->xm_behavior) {
            this->xm_behavior->mouse_event(bstate, release, x, y);
        }
    } else {
        log_error("bad mouse escape sequence: %s", buffer);
    }
}

void
xterm_mouse::set_enabled(bool enabled)
{
    if (is_available()) {
        if (this->xm_enabled != enabled) {
            putp(tparm((char*) XT_TERMCAP, enabled ? 1 : 0));
            putp(tparm((char*) XT_TERMCAP_TRACKING, enabled ? 1 : 0));
            putp(tparm((char*) XT_TERMCAP_SGR, enabled ? 1 : 0));
            fflush(stdout);
            this->xm_enabled = enabled;
        }
    } else {
        log_warning("mouse support is not available");
    }
}

bool
xterm_mouse::is_available()
{
    return isatty(STDOUT_FILENO);
}

void
xterm_mouse::log_crash_recover()
{
    this->set_enabled(false);
}
