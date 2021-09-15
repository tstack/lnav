/**
* Copyright (c) 2014, Timothy Stack
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
* @file sysclip.cc
*/

#include "config.h"

#include <stdio.h>

#include "base/lnav_log.hh"
#include "sysclip.hh"

struct clip_command {
    const char *cc_cmd[2];
};

static clip_command *get_commands()
{
    static clip_command NEOVIM_CMDS[] = {
            { { "win32yank.exe -i --crlf > /dev/null 2>&1",
                    "win32yank.exe -o --lf < /dev/null 2>/dev/null" } },
            { { nullptr, nullptr } },
    };
    static clip_command OSX_CMDS[] = {
            { { "pbcopy > /dev/null 2>&1",
                    "pbpaste -Prefer txt 2>/dev/null", } },
            { { "pbcopy -pboard find > /dev/null 2>&1",
                    "pbpaste -pboard find -Prefer txt 2>/dev/null" } },
    };
    static clip_command TMUX_CMDS[] = {
            { { "tmux load-buffer - > /dev/null 2>&1",
                    "tmux save-buffer - < /dev/null 2>/dev/null" } },
            { { nullptr, nullptr } },
    };
    static clip_command WAYLAND_CMDS[] = {
            { { "wl-copy --foreground --type text/plain > /dev/null 2>&1",
                    "wl-paste --no-newline < /dev/null 2>/dev/null" } },
            { { nullptr, nullptr } },
    };
    static clip_command WINDOWS_CMDS[] = {
            { { "clip.exe > /dev/null 2>&1",
                    nullptr } },
            { { nullptr, nullptr } },
    };
    static clip_command XCLIP_CMDS[] = {
            { { "xclip -i > /dev/null 2>&1",
                    "xclip -o < /dev/null 2>/dev/null" } },
            { { nullptr, nullptr } },
    };
    static clip_command XSEL_CMDS[] = {
            { { "xsel --nodetach -i -b > /dev/null 2>&1",
                    "xclip -o -b < /dev/null 2>/dev/null" } },
            { { nullptr, nullptr } },
    };

    clip_command *retval = nullptr;
    if (system("command -v pbcopy > /dev/null 2>&1") == 0) {
        retval = OSX_CMDS;
    } else if (getenv("WAYLAND_DISPLAY") != nullptr) {
        retval = WAYLAND_CMDS;
    } else if (getenv("DISPLAY") != nullptr && system("command -v xclip > /dev/null 2>&1") == 0) {
        retval = XCLIP_CMDS;
    } else if (getenv("DISPLAY") != nullptr && system("command -v xsel > /dev/null 2>&1") == 0) {
        retval = XSEL_CMDS;
    } else if (getenv("TMUX") != nullptr) {
	    retval = TMUX_CMDS;
    } else if (system("command -v win32yank.exe > /dev/null 2>&1") == 0) {
        /*
         * NeoVim's win32yank command is bidirectional, whereas the system-supplied
         * clip.exe is copy-only.
         * xclip and clip.exe may coexist on Windows Subsystem for Linux
         */
        retval = NEOVIM_CMDS;
    } else if (system("command -v clip.exe > /dev/null 2>&1") == 0) {
        retval = WINDOWS_CMDS;
    } else {
        log_error("unable to detect clipboard commands");
    }

    if (retval != nullptr) {
        log_info("detected clipboard copy command: %s", retval[0].cc_cmd[0]);
        log_info("detected clipboard paste command: %s", retval[0].cc_cmd[1]);
    }

    return retval;
}

/* XXX For one, this code is kinda crappy.  For two, we should probably link
 * directly with X so we don't need to have xclip installed and it'll work if
 * we're ssh'd into a box.
 */
FILE *open_clipboard(clip_type_t type, clip_op_t op)
{
    const char *mode = op == CO_WRITE ? "w" : "r";
    static clip_command *cc = get_commands();
    FILE *pfile = nullptr;

    if (cc != nullptr && cc[type].cc_cmd[op] != nullptr) {
        pfile = popen(cc[type].cc_cmd[op], mode);
    }

    return pfile;
}
