/**
 * Copyright (c) 2020, Timothy Stack
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

#ifndef lnav_log_level_enum_hh
#define lnav_log_level_enum_hh

/**
 * The logging level identifiers for a line(s).
 */
enum log_level_t : int {
    LEVEL_UNKNOWN,
    LEVEL_TRACE,
    LEVEL_DEBUG5,
    LEVEL_DEBUG4,
    LEVEL_DEBUG3,
    LEVEL_DEBUG2,
    LEVEL_DEBUG,
    LEVEL_INFO,
    LEVEL_STATS,
    LEVEL_NOTICE,
    LEVEL_WARNING,
    LEVEL_ERROR,
    LEVEL_CRITICAL,
    LEVEL_FATAL,
    LEVEL_INVALID,

    LEVEL__MAX,

    LEVEL_IGNORE = 0x10, /*< Ignore */
    LEVEL_TIME_SKEW = 0x20, /*< Received after timestamp. */
    LEVEL_MARK = 0x40, /*< Bookmarked line. */
    LEVEL_CONTINUED = 0x80, /*< Continuation of multiline entry. */

    /** Mask of flags for the level field. */
    LEVEL__FLAGS
        = (LEVEL_IGNORE | LEVEL_TIME_SKEW | LEVEL_MARK | LEVEL_CONTINUED)
};

#endif
