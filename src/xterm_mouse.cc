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
xterm_mouse::handle_mouse(notcurses* nc, const ncinput& nci)
{
    if (this->xm_behavior) {
        int bstate = XT_BUTTON1;
        auto release = nci.evtype == NCTYPE_RELEASE;
        switch (nci.id) {
            case NCKEY_BUTTON1:
                bstate = XT_BUTTON1;
                break;
            case NCKEY_BUTTON2:
                bstate = XT_BUTTON2;
                break;
            case NCKEY_BUTTON3:
                bstate = XT_BUTTON3;
                break;
            case NCKEY_SCROLL_UP:
                bstate = XT_SCROLL_UP;
                break;
            case NCKEY_SCROLL_DOWN:
                bstate = XT_SCROLL_DOWN;
                break;
            default:
                // XXX ignore other stuff
                return;
        }
        if (ncinput_alt_p(&nci)) {
            bstate |= XT_MODIFIER_META;
        }
        if (ncinput_ctrl_p(&nci)) {
            bstate |= XT_MODIFIER_CTRL;
        }
        if (ncinput_shift_p(&nci)) {
            bstate |= XT_MODIFIER_SHIFT;
        }
        if (nci.modifiers & NCKEY_MOD_MOTION) {
            bstate |= XT_DRAG_FLAG;
        }
        this->xm_behavior->mouse_event(nc, bstate, release, nci.x, nci.y + 1);
    }
}

void
xterm_mouse::set_enabled(notcurses* nc, bool enable)
{
    if (enable) {
        if (!this->xm_enabled) {
            if (notcurses_mice_enable(nc,
                                      NCMICE_BUTTON_EVENT | NCMICE_DRAG_EVENT)
                == 0)
            {
                this->xm_enabled = true;
            } else {
                log_warning("unable to enable mouse support");
            }
        }
    } else if (this->xm_enabled) {
        notcurses_mice_disable(nc);
        this->xm_enabled = false;
    }
}
