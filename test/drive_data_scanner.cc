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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <fstream>
#include <iostream>

#include <stdio.h>
#include <stdlib.h>

#include "base/injector.bind.hh"
#include "base/injector.hh"
#include "config.h"
#include "data_parser.hh"
#include "data_scanner.hh"
#include "elem_to_json.hh"
#include "log_format.hh"
#include "log_format_loader.hh"
#include "logfile.hh"
#include "pretty_printer.hh"
#include "shared_buffer.hh"
#include "view_curses.hh"

const char* TMP_NAME = "scanned.tmp";

static auto bound_file_options_hier
    = injector::bind<lnav::safe_file_options_hier>::to_singleton();

int
main(int argc, char* argv[])
{
    int c, retval = EXIT_SUCCESS;
    bool prompt = false, is_log = false, pretty_print = false;
    bool scanner_details = false;

    {
        static auto builtin_formats
            = injector::get<std::vector<std::shared_ptr<log_format>>>();
        auto& root_formats = log_format::get_root_formats();

        log_format::get_root_formats().insert(root_formats.begin(),
                                              builtin_formats.begin(),
                                              builtin_formats.end());
        builtin_formats.clear();
    }

    {
        std::vector<std::filesystem::path> paths;
        std::vector<lnav::console::user_message> errors;

        load_formats(paths, errors);
    }

    while ((c = getopt(argc, argv, "pPls")) != -1) {
        switch (c) {
            case 'p':
                prompt = true;
                break;

            case 'P':
                pretty_print = true;
                break;

            case 'l':
                is_log = true;
                break;

            case 's':
                scanner_details = true;
                break;

            default:
                retval = EXIT_FAILURE;
                break;
        }
    }

    argc -= optind;
    argv += optind;

    if (retval != EXIT_SUCCESS) {
    } else if (argc < 1) {
        fprintf(stderr, "error: expecting file name argument(s)\n");
        retval = EXIT_FAILURE;
    } else {
        for (int lpc = 0; lpc < argc; lpc++) {
            std::unique_ptr<std::ifstream> in_ptr;
            std::istream* in;
            FILE* out;

            if (strcmp(argv[lpc], "-") == 0) {
                in = &std::cin;
            } else {
                auto ifs = std::make_unique<std::ifstream>(argv[lpc]);

                if (!ifs->is_open()) {
                    fprintf(stderr, "error: unable to open file\n");
                    retval = EXIT_FAILURE;
                } else {
                    in_ptr = std::move(ifs);
                    in = in_ptr.get();
                }
            }

            if ((out = fopen(TMP_NAME, "w")) == nullptr) {
                fprintf(stderr,
                        "error: unable to temporary file for writing\n");
                retval = EXIT_FAILURE;
            } else {
                std::shared_ptr<log_format> format;
                bool found = false;
                char cmd[2048];
                std::string line;
                int rc;

                getline(*in, line);
                if (strcmp(argv[lpc], "-") == 0) {
                    line = "             " + line;
                }

                auto sub_line = line.substr(13);
                struct line_range body(0, sub_line.length());
                shared_buffer share_manager;
                logline_value_vector ll_values;
                auto& sbr = ll_values.lvv_sbr;

                sbr.share(
                    share_manager, (char*) sub_line.c_str(), sub_line.size());

                auto& root_formats = log_format::get_root_formats();
                std::vector<std::shared_ptr<log_format>>::iterator iter;

                if (is_log) {
                    std::vector<logline> index;
                    logfile_open_options loo;
                    auto open_res = logfile::open(argv[lpc], loo);
                    auto lf = open_res.unwrap();
                    ArenaAlloc::Alloc<char> allocator;
                    scan_batch_context sbc{allocator};
                    for (iter = root_formats.begin();
                         iter != root_formats.end() && !found;
                         ++iter)
                    {
                        line_info li = {{13}};

                        (*iter)->clear();
                        if ((*iter)
                                ->scan(*lf, index, li, sbr, sbc)
                                .is<log_format::scan_match>())
                        {
                            format = (*iter)->specialized();
                            found = true;
                        }
                    }

                    if (!found) {
                        fprintf(stderr, "error: log sample does not match\n");
                        return EXIT_FAILURE;
                    }
                }

                string_attrs_t sa;

                if (format.get() != nullptr) {
                    format->annotate(nullptr, 0, sa, ll_values, false);
                    body = find_string_attr_range(sa, &SA_BODY);
                }

                data_parser::TRACE_FILE = fopen("scanned.dpt", "w");
                setvbuf(data_parser::TRACE_FILE, nullptr, _IONBF, 0);

                data_scanner ds(sub_line, body.lr_start);

                if (scanner_details) {
                    fprintf(out,
                            "             %s\n",
                            ds.get_input().to_string().c_str());
                    while (true) {
                        auto tok_res = ds.tokenize2();

                        if (!tok_res) {
                            break;
                        }

                        fprintf(out,
                                "%4s %3d:%-3d ",
                                data_scanner::token2name(tok_res->tr_token),
                                tok_res->tr_capture.c_begin,
                                tok_res->tr_capture.c_end);
                        size_t cap_index = 0;
                        for (; cap_index < tok_res->tr_capture.c_end;
                             cap_index++)
                        {
                            if (cap_index == tok_res->tr_capture.c_begin) {
                                fputc('^', out);
                            } else if (cap_index
                                       == (tok_res->tr_capture.c_end - 1))
                            {
                                fputc('^', out);
                            } else if (cap_index > tok_res->tr_capture.c_begin)
                            {
                                fputc('-', out);
                            } else {
                                fputc(' ', out);
                            }
                        }
                        for (; cap_index < (int) ds.get_input().length();
                             cap_index++)
                        {
                            fputc(' ', out);
                        }

                        auto sub = tok_res->to_string();
                        fprintf(out, "  %s\n", sub.c_str());
                    }
                }

                ds.reset();
                data_parser dp(&ds);
                std::string msg_format;

                dp.dp_msg_format = &msg_format;
                dp.parse();
                dp.print(out, dp.dp_pairs);
                fprintf(
                    out, "msg         :%s\n", sub_line.c_str() + body.lr_start);
                fprintf(out, "format      :%s\n", msg_format.c_str());

                if (pretty_print) {
                    data_scanner ds2(sub_line, body.lr_start);
                    pretty_printer pp(&ds2, sa);
                    attr_line_t pretty_out;

                    pp.append_to(pretty_out);
                    fprintf(out, "\n--\n%s", pretty_out.get_string().c_str());
                }

                auto_mem<yajl_gen_t> gen(yajl_gen_free);

                gen = yajl_gen_alloc(nullptr);
                yajl_gen_config(gen.in(), yajl_gen_beautify, true);

                elements_to_json(gen, dp, &dp.dp_pairs);

                const unsigned char* buf;
                size_t len;

                yajl_gen_get_buf(gen, &buf, &len);
                fwrite(buf, 1, len, out);

                fclose(out);

                snprintf(
                    cmd, sizeof(cmd), "diff -u %s %s", argv[lpc], TMP_NAME);
                rc = system(cmd);
                if (rc != 0) {
                    if (prompt) {
                        char resp[4];

                        printf("\nOriginal line:\n%s\n",
                               sub_line.c_str() + body.lr_start);
                        printf(
                            "Would you like to update the original file? "
                            "(y/N) ");
                        fflush(stdout);
                        log_perror(scanf("%3s", resp));
                        if (strcasecmp(resp, "y") == 0) {
                            rename(TMP_NAME, argv[lpc]);
                        } else {
                            retval = EXIT_FAILURE;
                        }
                    } else {
                        fprintf(stderr, "error: mismatch\n");
                        retval = EXIT_FAILURE;
                    }
                }

                fclose(data_parser::TRACE_FILE);
                data_parser::TRACE_FILE = nullptr;
            }
        }
    }

    return retval;
}
