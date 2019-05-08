/**
 * Copyright (c) 2017, Timothy Stack
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
 * @file text_format.cc
 */

#include "config.h"

#include "pcrepp/pcrepp.hh"

#include "text_format.hh"

text_format_t detect_text_format(const char *str, size_t len)
{
    // XXX This is a pretty crude way of detecting format...
    static pcrepp PYTHON_MATCHERS = pcrepp(
        "(?:"
            "^\\s*def\\s+\\w+\\([^)]*\\):[^\\n]*$|"
            "^\\s*try:[^\\n]*$"
            ")",
        PCRE_MULTILINE);

    static pcrepp C_LIKE_MATCHERS = pcrepp(
        "(?:"
            "^#\\s*include\\s+|"
            "^#\\s*define\\s+|"
            "^\\s*if\\s+\\([^)]+\\)[^\\n]*$|"
            "^\\s*(?:\\w+\\s+)*class \\w+ {"
            ")",
        PCRE_MULTILINE);

    static pcrepp SQL_MATCHERS = pcrepp(
        "(?:"
            "select\\s+.+\\s+from\\s+|"
            "insert\\s+into\\s+.+\\s+values"
            ")",
        PCRE_MULTILINE|PCRE_CASELESS);

    text_format_t retval = TF_UNKNOWN;
    pcre_input pi(str, 0, len);
    pcre_context_static<30> pc;

    if (PYTHON_MATCHERS.match(pc, pi)) {
        return TF_PYTHON;
    }

    if (C_LIKE_MATCHERS.match(pc, pi)) {
        return TF_C_LIKE;
    }

    if (SQL_MATCHERS.match(pc, pi)) {
        return TF_SQL;
    }

    return retval;
}
