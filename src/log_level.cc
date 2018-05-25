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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "pcrepp.hh"
#include "log_level.hh"

const char *level_names[LEVEL__MAX + 1] = {
    "unknown",
    "trace",
    "debug5",
    "debug4",
    "debug3",
    "debug2",
    "debug",
    "info",
    "stats",
    "warning",
    "error",
    "critical",
    "fatal",

    NULL
};

static pcrepp LEVEL_RE(
    "(?i)(TRACE|DEBUG\\d*|INFO|NOTICE|STATS|WARN(?:ING)?|ERR(?:OR)?|CRITICAL|SEVERE|FATAL)");

log_level_t string2level(const char *levelstr, ssize_t len, bool exact)
{
    log_level_t retval = LEVEL_UNKNOWN;

    if (len == (ssize_t)-1) {
        len = strlen(levelstr);
    }

    if (((len == 1) || ((len > 1) && (levelstr[1] == ' '))) &&
        (retval = abbrev2level(levelstr, 1)) != LEVEL_UNKNOWN) {
        return retval;
    }

    pcre_input pi(levelstr, 0, len);
    pcre_context_static<10> pc;

    if (LEVEL_RE.match(pc, pi)) {
        auto iter = pc.begin();
        if (!exact || pc[0]->c_begin == 0) {
            retval = abbrev2level(pi.get_substr_start(iter),
                                  pi.get_substr_len(iter));
        }
    }

    return retval;
}

log_level_t abbrev2level(const char *levelstr, ssize_t len)
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
        case 'N': // NOTICE
            return LEVEL_INFO;
        case 'S':
            return LEVEL_STATS;
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

int levelcmp(const char *l1, ssize_t l1_len, const char *l2, ssize_t l2_len)
{
    return abbrev2level(l1, l1_len) - abbrev2level(l2, l2_len);
}
