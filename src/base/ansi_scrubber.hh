/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file ansi_scrubber.hh
 */

#ifndef lnav_ansi_scrubber_hh
#define lnav_ansi_scrubber_hh

#include <map>
#include <string>

#include "attr_line.hh"

#define ANSI_CSI             "\x1b["
#define ANSI_CHAR_ATTR       "m"
#define ANSI_BOLD_PARAM      "1"
#define ANSI_BOLD_START      ANSI_CSI ANSI_BOLD_PARAM ANSI_CHAR_ATTR
#define ANSI_UNDERLINE_START ANSI_CSI "4m"
#define ANSI_NORM            ANSI_CSI "0m"
#define ANSI_STRIKE_PARAM    "9"
#define ANSI_STRIKE_START    ANSI_CSI ANSI_STRIKE_PARAM ANSI_CHAR_ATTR

#define ANSI_BOLD(msg)      ANSI_BOLD_START msg ANSI_NORM
#define ANSI_UNDERLINE(msg) ANSI_UNDERLINE_START msg ANSI_NORM

#define ANSI_ROLE(msg)        ANSI_CSI "%dO" msg ANSI_NORM
#define ANSI_ROLE_FMT(msg)    ANSI_CSI "{}O" msg ANSI_NORM
#define XANSI_COLOR(col)      "3" #col
#define ANSI_COLOR_PARAM(col) XANSI_COLOR(col)
#define ANSI_COLOR(col)       ANSI_CSI XANSI_COLOR(col) "m"

/**
 * Check a string for ANSI escape sequences, process them, remove them, and add
 * any style attributes to the given attribute container.
 *
 * @param str The string to check for ANSI escape sequences.
 * @param sa  The container for any style attributes.
 */
void scrub_ansi_string(std::string& str, string_attrs_t* sa);

size_t erase_ansi_escapes(string_fragment input);

#endif
