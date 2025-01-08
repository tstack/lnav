/**
 * Copyright (c) 2022, Timothy Stack
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

#ifndef lnav_md2attr_line_hh
#define lnav_md2attr_line_hh

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "base/result.h"
#include "md4cpp.hh"

namespace pugi {
class xml_node;
}

class md2attr_line : public md4cpp::typed_event_handler<attr_line_t> {
public:
    md2attr_line() { this->ml_blocks.resize(1); }

    md2attr_line& with_source_path(std::optional<std::filesystem::path> path)
    {
        this->ml_source_path = path;
        return *this;
    }

    Result<void, std::string> enter_block(const block& bl) override;

    Result<void, std::string> leave_block(const block& bl) override;
    Result<void, std::string> enter_span(const span& bl) override;
    Result<void, std::string> leave_span(const span& sp) override;
    Result<void, std::string> text(MD_TEXTTYPE tt,
                                   const string_fragment& sf) override;

    attr_line_t get_result() override { return this->ml_blocks.back(); }

private:
    struct table_t {
        struct cell_t {
            cell_t(MD_ALIGN align, const attr_line_t& contents)
                : c_align(align), c_contents(contents)
            {
            }

            MD_ALIGN c_align;
            attr_line_t c_contents;
        };
        struct row_t {
            std::vector<cell_t> r_columns;
        };

        std::vector<attr_line_t> t_headers;
        std::vector<row_t> t_rows;
    };

    struct cell_lines {
        cell_lines(MD_ALIGN align, std::vector<attr_line_t> lines)
            : cl_align(align), cl_lines(std::move(lines))
        {
        }

        MD_ALIGN cl_align;
        std::vector<attr_line_t> cl_lines;
    };

    using list_block_t
        = mapbox::util::variant<MD_BLOCK_UL_DETAIL*, MD_BLOCK_OL_DETAIL>;

    std::string append_url_footnote(std::string href);
    void flush_footnotes();
    attr_line_t to_attr_line(const pugi::xml_node& doc);

    std::optional<std::filesystem::path> ml_source_path;
    std::vector<attr_line_t> ml_blocks;
    std::vector<list_block_t> ml_list_stack;
    std::vector<table_t> ml_tables;
    std::vector<size_t> ml_span_starts;
    std::vector<std::pair<std::string, size_t>> ml_html_starts;
    std::vector<attr_line_t> ml_footnotes;
    int32_t ml_code_depth{0};
    ssize_t ml_last_superscript_index{-1};
};

#endif
