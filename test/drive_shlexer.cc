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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>

#include "config.h"
#include "shlex.hh"

using namespace std;

const char* ST_TOKEN_NAMES[] = {
    "eof",
    "wsp",
    "esc",
    "dst",
    "den",
    "sst",
    "sen",
    "ref",
    "qrf",
    "til",
};

static void
put_underline(FILE* file, string_fragment frag)
{
    for (int lpc = 0; lpc < frag.sf_end; lpc++) {
        if (lpc == frag.sf_begin) {
            fputc('^', stdout);
        } else if (lpc == (frag.sf_end - 1)) {
            fputc('^', stdout);
        } else if (lpc > frag.sf_begin) {
            fputc('-', stdout);
        } else {
            fputc(' ', stdout);
        }
    }
}

int
main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "error: expecting an argument to parse\n");
        exit(EXIT_FAILURE);
    }

    shlex lexer(argv[1], strlen(argv[1]));
    bool done = false;

    printf("    %s\n", argv[1]);
    while (!done) {
        auto tokenize_res = lexer.tokenize();
        if (tokenize_res.isErr()) {
            auto te = tokenize_res.unwrapErr();

            printf("err ");
            put_underline(stdout, te.te_source);
            printf(" -- %s\n", te.te_msg);
            break;
        }

        auto tr = tokenize_res.unwrap();
        if (tr.tr_token == shlex_token_t::eof) {
            done = true;
        }
        printf("%s ", ST_TOKEN_NAMES[(int) tr.tr_token]);
        put_underline(stdout, tr.tr_frag);
        printf("\n");
    }

    lexer.reset();
    std::string result;
    std::map<std::string, scoped_value_t> vars;
    if (lexer.eval(result, scoped_resolver{&vars})) {
        printf("eval -- %s\n", result.c_str());
    }
    lexer.reset();
    std::vector<shlex::split_element_t> sresult;
    auto split_res = lexer.split(scoped_resolver{&vars});
    if (split_res.isOk()) {
        sresult = split_res.unwrap();
    } else {
        auto split_err = split_res.unwrapErr();

        printf("split-error: %s\n", split_err.se_error.te_msg);
        sresult = std::move(split_err.se_elements);
    }
    printf("split:\n");
    for (size_t lpc = 0; lpc < sresult.size(); lpc++) {
        printf("% 3zu ", lpc);
        put_underline(stdout, sresult[lpc].se_origin);
        printf(" -- %s\n", sresult[lpc].se_value.c_str());
    }

    return EXIT_SUCCESS;
}
