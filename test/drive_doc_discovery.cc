/**
 * Copyright (c) 2024, Timothy Stack
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

#include "base/fs_util.hh"
#include "document.sections.hh"
#include "fmt/color.h"

int
main(int argc, char* argv[])
{
    int retval = EXIT_SUCCESS;

    if (argc < 2) {
        fprintf(stderr, "error: expecting file to discover\n");
        retval = EXIT_FAILURE;
    } else {
        const auto fn = std::filesystem::path(argv[1]);
        auto read_res = lnav::filesystem::read_file(fn);
        if (read_res.isErr()) {
            fprintf(stderr,
                    "error: unable to read %s -- %s\n",
                    fn.c_str(),
                    read_res.unwrapErr().c_str());
            retval = EXIT_FAILURE;
        } else {
            auto content = attr_line_t(read_res.unwrap());
            const auto& content_sf
                = string_fragment::from_str(content.get_string());
            auto tf = detect_text_format(content_sf, fn);
            auto lr = line_range{0, static_cast<int>(content.length())};
            auto meta = lnav::document::discover_structure(content, lr, tf);

            auto remaining = content_sf;
            while (!remaining.empty()) {
                auto line_pair
                    = remaining.split_when(string_fragment::tag1{'\n'});
                auto line_sf = line_pair.first;
                fmt::print(FMT_STRING("{}\n"), line_sf);
                size_t indent = 0;
                meta.m_sections_tree.visit_overlapping(
                    line_sf.sf_begin,
                    line_sf.sf_end,
                    [&line_sf, &indent](const auto& iv) {
                        if (iv.start < line_sf.sf_begin) {
                            return;
                        }
                        auto this_indent = iv.start - line_sf.sf_begin;
                        if (this_indent < indent) {
                            return;
                        }
                        auto indent_diff = this_indent - indent;
                        indent = this_indent;
                        fmt::print(FMT_STRING("{}^"),
                                   std::string(indent_diff, ' '));
                        if (iv.stop >= line_sf.sf_end + 1) {
                            fmt::print(FMT_STRING("  [{}:{}) - {}"),
                                       iv.start,
                                       iv.stop,
                                       iv.value);
                            return;
                        }
                        auto dot_len = iv.stop - iv.start - 1;
                        fmt::print(FMT_STRING("{}^"),
                                   std::string(dot_len, '-'));
                        fmt::print(FMT_STRING("  [{}:{})"), iv.start, iv.stop);
                    });
                fmt::print(FMT_STRING("\nPath for line[{}:{}): "),
                           line_sf.sf_begin,
                           line_sf.sf_end);
                meta.m_sections_tree.visit_overlapping(
                    line_sf.sf_begin,
                    line_sf.sf_end,
                    [&line_sf](const auto& iv) {
                        fmt::print(
                            fmt::fg(iv.start < line_sf.sf_begin
                                        ? fmt::terminal_color::yellow
                                        : fmt::terminal_color::green),
                            FMT_STRING("\uff1a{}"),
                            iv.value.match(
                                [](const std::string& str) { return str; },
                                [](size_t ind) {
                                    return fmt::format(FMT_STRING("[{}]"), ind);
                                }));
                    });
                fmt::print(FMT_STRING("\n"));
                remaining = line_pair.second;
            }
        }
    }

    return retval;
}
