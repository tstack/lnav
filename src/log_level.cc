/**
 * Copyright (c) 2018, Timothy Stack
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

#include "log_level.hh"

#include <ctype.h>

#include "config.h"

constexpr std::array<const char*, LEVEL__MAX> level_names = {
    "unknown",
    "trace",
    "debug5",
    "debug4",
    "debug3",
    "debug2",
    "debug",
    "info",
    "stats",
    "notice",
    "warning",
    "error",
    "critical",
    "fatal",
    "invalid",
};

log_level_t
abbrev2level(const char* levelstr, ssize_t len)
{
    if (len == 0 || levelstr[0] == '\0') {
        return LEVEL_UNKNOWN;
    }

    switch (toupper(levelstr[0])) {
        case 'T':
            return LEVEL_TRACE;
        case 'D':
        case 'V':
            if (len > 1) {
                switch (levelstr[len - 1]) {
                    case '2':
                        return LEVEL_DEBUG2;
                    case '3':
                        return LEVEL_DEBUG3;
                    case '4':
                        return LEVEL_DEBUG4;
                    case '5':
                        return LEVEL_DEBUG5;
                }
            }
            return LEVEL_DEBUG;
        case 'I':
            if (len == 7 && toupper(levelstr[1]) == 'N'
                && toupper(levelstr[2]) == 'V' && toupper(levelstr[3]) == 'A'
                && toupper(levelstr[4]) == 'L' && toupper(levelstr[5]) == 'I'
                && toupper(levelstr[6]) == 'D')
            {
                return LEVEL_INVALID;
            }
            return LEVEL_INFO;
        case 'S':
            return LEVEL_STATS;
        case 'N':
            return LEVEL_NOTICE;
        case 'W':
            return LEVEL_WARNING;
        case 'E':
            return LEVEL_ERROR;
        case 'C':
            return LEVEL_CRITICAL;
        case 'F':
            return LEVEL_FATAL;
        default:
            return LEVEL_UNKNOWN;
    }
}

int
levelcmp(const char* l1, ssize_t l1_len, const char* l2, ssize_t l2_len)
{
    return abbrev2level(l1, l1_len) - abbrev2level(l2, l2_len);
}
