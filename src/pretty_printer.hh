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

#ifndef pretty_printer_hh
#define pretty_printer_hh

#include <deque>
#include <optional>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "base/file_range.hh"
#include "data_scanner.hh"
#include "document.sections.hh"

class pretty_printer {
public:
    struct element {
        element(data_token_t token, data_scanner::capture_t& cap)
            : e_token(token), e_capture(cap)
        {
        }

        data_token_t e_token;
        data_scanner::capture_t e_capture;
    };

    pretty_printer(data_scanner* ds, string_attrs_t sa, int leading_indent = 0)
        : pp_leading_indent(leading_indent), pp_scanner(ds),
          pp_attrs(std::move(sa))
    {
        this->pp_body_lines.push(0);
        this->pp_scanner->reset();
        while (true) {
            auto tok_res = this->pp_scanner->tokenize2();
            if (!tok_res) {
                break;
            }
            if (tok_res->tr_token == DT_XML_CLOSE_TAG
                || tok_res->tr_token == DT_XML_DECL_TAG)
            {
                pp_is_xml = true;
                break;
            }
        }

        this->pp_interval_state.resize(1);
        this->pp_hier_nodes.push_back(
            std::make_unique<lnav::document::hier_node>());
    }

    void append_to(attr_line_t& al);

    std::vector<lnav::document::section_interval_t> take_intervals()
    {
        return std::move(this->pp_intervals);
    }

    std::unique_ptr<lnav::document::hier_node> take_hier_root()
    {
        if (this->pp_hier_stage == nullptr && !this->pp_hier_nodes.empty()) {
            this->pp_hier_stage = std::move(this->pp_hier_nodes.back());
            this->pp_hier_nodes.pop_back();
        }
        return std::move(this->pp_hier_stage);
    }

    std::set<size_t> take_indents() { return std::move(this->pp_indents); }

private:
    void descend(data_token_t dt);

    void ascend(data_token_t dt);

    void start_new_line();

    bool flush_values(bool start_on_depth = false);

    int append_indent();

    void write_element(const element& el);

    void append_child_node();

    struct interval_state {
        std::optional<file_off_t> is_start;
        std::string is_name;
    };

    int pp_leading_indent;
    int pp_depth{0};
    int pp_line_length{0};
    int pp_soft_indent{0};
    std::vector<data_token_t> pp_container_tokens{};
    std::stack<int> pp_body_lines{};
    data_scanner* pp_scanner;
    string_attrs_t pp_attrs;
    string_attrs_t pp_post_attrs;
    std::ostringstream pp_stream;
    std::deque<element> pp_values{};
    int pp_shift_accum{0};
    bool pp_is_xml{false};
    std::vector<interval_state> pp_interval_state;
    std::vector<lnav::document::section_interval_t> pp_intervals;
    std::vector<std::unique_ptr<lnav::document::hier_node>> pp_hier_nodes;
    std::unique_ptr<lnav::document::hier_node> pp_hier_stage;
    std::set<size_t> pp_indents;
};

#endif
