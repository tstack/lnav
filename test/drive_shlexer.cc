/**
 * Copyright (c) 2015, Timothy Stack
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

#include <stdlib.h>

#include "shlex.hh"

using namespace std;

const char *ST_TOKEN_NAMES[] = {
        "err",
        "esc",
        "dst",
        "den",
        "sst",
        "sen",
        "ref",
};

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "error: expecting an argument to parse\n");
        exit(EXIT_FAILURE);
    }

    shlex lexer(argv[1], strlen(argv[1]));
    pcre_context::capture_t cap;
    shlex_token_t token;

    printf("    %s\n", argv[1]);
    while (lexer.tokenize(cap, token)) {
        int lpc;

        printf("%s ", ST_TOKEN_NAMES[token]);
        for (lpc = 0; lpc < cap.c_end; lpc++) {
            if (lpc == cap.c_begin) {
                fputc('^', stdout);
            }
            else if (lpc == (cap.c_end - 1)) {
                fputc('^', stdout);
            }
            else if (lpc > cap.c_begin) {
                fputc('-', stdout);
            }
            else{
                fputc(' ', stdout);
            }
        }
        printf("\n");
    }

    lexer.reset();
    std::string result;
    if (lexer.eval(result, map<string, string>())) {
        printf("eval -- %s\n", result.c_str());
    }

    return EXIT_SUCCESS;
}
