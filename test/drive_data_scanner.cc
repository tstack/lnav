/**
 * Copyright (c) 2007-2012, Timothy Stack
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

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <iostream>

#include "pcrepp.hh"
#include "data_scanner.hh"
#include "data_parser.hh"
#include "log_format.hh"

using namespace std;

const char *TMP_NAME = "scanned.tmp";

int main(int argc, char *argv[])
{
    int  c, retval = EXIT_SUCCESS;
    bool prompt = false;

    while ((c = getopt(argc, argv, "p")) != -1) {
        switch (c) {
        case 'p':
            prompt = true;
            break;

        default:
            retval = EXIT_FAILURE;
            break;
        }
    }

    argc -= optind;
    argv += optind;

    if (retval != EXIT_SUCCESS) {}
    else if (argc < 1) {
        fprintf(stderr, "error: expecting file name argument(s)\n");
        retval = EXIT_FAILURE;
    }
    else {
        for (int lpc = 0; lpc < argc; lpc++) {
            istream *in;
            FILE *   out;

            if (strcmp(argv[lpc], "-") == 0) {
                in = &cin;
            }
            else {
                ifstream *ifs = new ifstream(argv[lpc]);

                if (!ifs->is_open()) {
                    fprintf(stderr, "error: unable to open file\n");
                    retval = EXIT_FAILURE;
                }
                else {
                    in = ifs;
                }
            }

            if ((out = fopen(TMP_NAME, "w")) == NULL) {
                fprintf(stderr, "error: unable to temporary file for writing\n");
                retval = EXIT_FAILURE;
            }
            else {
                auto_ptr<log_format> format;
                char log_line[2048];
                bool found = false;
                char   cmd[2048];
                string line;
                int    rc;

                getline(*in, line);
                if (strcmp(argv[lpc], "-") == 0) {
                    line = "             " + line;
                }

                strcpy(log_line, &line[13]);

                vector<log_format *> &root_formats = log_format::get_root_formats();
                vector<log_format *>::iterator iter;
                vector<logline> index;

                for (iter = root_formats.begin();
                     iter != root_formats.end() && !found;
                     ++iter) {
                    (*iter)->clear();
                    if ((*iter)->scan(index, 13, log_line, strlen(log_line))) {
                        format = (*iter)->specialized();
                        found = true;
                    }
                }

                string sub_line = line.substr(13);
                struct line_range body = { 0, sub_line.length() };

                if (format.get() != NULL) {
                    vector<logline_value> ll_values;
                    string_attrs_t sa;

                    format->annotate(sub_line, sa, ll_values);
                    body = find_string_attr_range(sa, "body");
                }

                data_scanner ds(sub_line, body.lr_start, sub_line.length());
                data_parser  dp(&ds);

                dp.parse();
                dp.print(out, dp.dp_pairs);
                fclose(out);

                sprintf(cmd, "diff -u %s %s", argv[lpc], TMP_NAME);
                rc = system(cmd);
                if (rc != 0) {
                    if (prompt) {
                        char resp[4];

                        printf("Would you like to update the original file? (y/N) ");
                        fflush(stdout);
                        scanf("%3s", resp);
                        if (strcasecmp(resp, "y") == 0) {
                            rename(TMP_NAME, argv[lpc]);
                        }
                        else{
                            retval = EXIT_FAILURE;
                        }
                    }
                    else {
                        fprintf(stderr, "error: mismatch\n");
                        retval = EXIT_FAILURE;
                    }
                }
            }
        }
    }

    return retval;
}
