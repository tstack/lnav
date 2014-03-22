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
 * @file ptimec.cc
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const char *PRELUDE = "\
#include <time.h>\n\
#include <sys/types.h>\n\
#include \"ptimec.hh\"\n\
\n\
";

char *escape_char(char ch)
{
    static char charstr[4];

    if (ch == '\'') {
        strcpy(charstr, "\\'");
    } else {
        charstr[0] = ch;
        charstr[1] = '\0';
    }

    return charstr;
}

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;

    for (int lpc = 1; lpc < argc; lpc++) {
        const char *arg = argv[lpc];

        fputs(PRELUDE, stdout);
        printf("bool ptime_f%d(struct tm *dst, const char *str, off_t &off, size_t len) {\n",
            lpc);
        for (int index = 0; arg[index]; arg++) {
            if (arg[index] == '%') {
                switch (arg[index + 1]) {
                case 'a':
                case 'Z':
                    printf("    if (!ptime_upto('%s', str, off, len)) return false;\n",
                        escape_char(arg[index + 2]));
                    arg += 1;
                    break;
                default:
                    printf("    if (!ptime_%c(dst, str, off, len)) return false;\n",
                        arg[index + 1]);
                    arg += 1;
                    break;
                }
            } else {
                printf("    if (!ptime_char('%s', str, off, len)) return false;\n",
                    escape_char(arg[index]));
            }
        }
        printf("    return true;\n");
        printf("}\n\n");
    }

    printf("struct ptime_fmt PTIMEC_FORMATS[] = {\n");
    for (int lpc = 1; lpc < argc; lpc++) {
        printf("    { \"%s\", ptime_f%d },\n", argv[lpc], lpc);
    }
    printf("\n");
    printf("    { NULL, NULL }\n");
    printf("};\n");

    printf("const char *PTIMEC_FORMAT_STR[] = {\n");
    for (int lpc = 1; lpc < argc; lpc++) {
        printf("    \"%s\",\n", argv[lpc]);
    }
    printf("\n");
    printf("    NULL\n");
    printf("};\n");

    return retval;
}
