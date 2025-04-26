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

#include <string.h>

#include "log_level.hh"

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

#   define YYCTYPE unsigned char
#   define RET(tok) { \
        return tok; \
    }

    const YYCTYPE *YYCURSOR = (const unsigned char *) levelstr;
    const YYCTYPE *YYLIMIT = (const unsigned char *) levelstr + len;
    const YYCTYPE *YYMARKER = YYCURSOR;
    const YYCTYPE *debug_level = nullptr;

#   define YYPEEK()    (YYCURSOR < YYLIMIT ? *YYCURSOR : 0)
#   define YYSKIP()    ++YYCURSOR
#   define YYBACKUP()  YYMARKER = YYCURSOR
#   define YYRESTORE() YYCURSOR = YYMARKER
#   define YYSTAGP(x)  x = YYCURSOR - 1

    /*!stags:re2c format = 'const unsigned char *@@;'; */
    loop:
    /*!re2c
     re2c:tags = 1;
     re2c:yyfill:enable = 0;
     re2c:flags:input = custom;

     EOF = "\x00";

     EOF { RET(LEVEL_UNKNOWN); }
     'trace'|'verbose' { RET(LEVEL_TRACE); }
     'debug' [2-5]? @debug_level {
         if (debug_level == nullptr) {
             RET(LEVEL_DEBUG);
         }
         switch (*debug_level) {
         case '2':
             RET(LEVEL_DEBUG2);
         case '3':
             RET(LEVEL_DEBUG3);
         case '4':
             RET(LEVEL_DEBUG4);
         case '5':
             RET(LEVEL_DEBUG5);
         default:
             RET(LEVEL_DEBUG);
         }
     }
     'info' { RET(LEVEL_INFO); }
     'notice' { RET(LEVEL_NOTICE); }
     'stats' { RET(LEVEL_STATS); }
     'warn'|'warning'|'deprecation' { RET(LEVEL_WARNING); }
     'err'|'error' { RET(LEVEL_ERROR); }
     'critical' { RET(LEVEL_CRITICAL); }
     'severe' { RET(LEVEL_CRITICAL); }
     'fatal' { RET(LEVEL_FATAL); }
     'invalid' { RET(LEVEL_INVALID); }
     * { goto loop; }

     */
}
