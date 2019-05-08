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
    static clip_command OSX_CMDS[] = {
            { { "pbcopy > /dev/null 2>&1",
                    "pbpaste -Prefer txt 2>/dev/null", } },
            { { "pbcopy -pboard find > /dev/null 2>&1",
                    "pbpaste -pboard find -Prefer txt 2>/dev/null" } },
    };
    static clip_command WINDOWS_CMDS[] = {
            { { "clip.exe > /dev/null 2>&1",
                    nullptr } },
            { { nullptr, nullptr } },
    };
    static clip_command X_CMDS[] = {
            { { "xclip -i > /dev/null 2>&1",
                    "xclip -o < /dev/null 2>/dev/null" } },
            { { nullptr, nullptr } },
    };
    if (system("which pbcopy > /dev/null 2>&1") == 0) {
        return OSX_CMDS;
    }
    if (system("which xclip > /dev/null 2>&1") == 0) {
        return X_CMDS;
    }
    /*
     * xclip and clip.exe may coexist on Windows Subsystem for Linux
     */
    if (system("which clip.exe > /dev/null 2>&1") == 0) {
        return WINDOWS_CMDS;
    }
    return nullptr;
}

/* XXX For one, this code is kinda crappy.  For two, we should probably link
 * directly with X so we don't need to have xclip installed and it'll work if
 * we're ssh'd into a box.
 */
FILE *open_clipboard(clip_type_t type, clip_op_t op)
{
    const char *mode = op == CO_WRITE ? "w" : "r";
    clip_command *cc = get_commands();
    FILE *pfile = nullptr;

    if (cc != nullptr && cc[type].cc_cmd[op] != nullptr) {
        pfile = popen(cc[type].cc_cmd[op], mode);
    }

    return pfile;
}
