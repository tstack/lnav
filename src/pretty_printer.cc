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

#include "pretty_printer.hh"

#include <sys/types.h>

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/string_util.hh"
#include "config.h"

void
pretty_printer::append_to(attr_line_t& al)
{
    if (this->pp_scanner->get_init_offset() > 0) {
        data_scanner::capture_t leading_cap = {
            0,
            this->pp_scanner->get_init_offset(),
        };

        // this->pp_stream << pi.get_substr(&leading_cap);
        this->pp_values.emplace_back(DT_WORD, leading_cap);
    }

    this->pp_scanner->reset();
    while (true) {
        auto tok_res = this->pp_scanner->tokenize2();
        if (!tok_res) {
            break;
        }

        element el(tok_res->tr_token, tok_res->tr_capture);

        switch (el.e_token) {
            case DT_XML_DECL_TAG:
            case DT_XML_EMPTY_TAG:
                if (this->pp_is_xml && this->pp_line_length > 0) {
                    this->start_new_line();
                }
                this->pp_values.emplace_back(el);
                if (this->pp_is_xml) {
                    this->start_new_line();
                }
                continue;
            case DT_XML_OPEN_TAG:
                if (this->pp_is_xml) {
                    this->start_new_line();
                    this->write_element(el);
                    this->pp_interval_state.back().is_start
                        = this->pp_stream.tellp();
                    this->pp_interval_state.back().is_name
                        = tok_res->to_string();
                    this->descend(DT_XML_CLOSE_TAG);
                } else {
                    this->pp_values.emplace_back(el);
                }
                continue;
            case DT_XML_CLOSE_TAG:
                this->flush_values();
                this->ascend(el.e_token);
                this->append_child_node();
                this->write_element(el);
                this->start_new_line();
                continue;
            case DT_LCURLY:
            case DT_LSQUARE:
            case DT_LPAREN:
                this->flush_values(true);
                this->pp_values.emplace_back(el);
                this->descend(to_closer(el.e_token));
                this->pp_interval_state.back().is_start
                    = this->pp_stream.tellp();
                continue;
            case DT_RCURLY:
            case DT_RSQUARE:
            case DT_RPAREN:
                this->flush_values();
                if (this->pp_body_lines.top()) {
                    this->start_new_line();
                }
                this->ascend(el.e_token);
                this->write_element(el);
                continue;
            case DT_COMMA:
                if (this->pp_depth > 0) {
                    this->flush_values(true);
                    if (!this->pp_is_xml) {
                        this->append_child_node();
                    }
                    this->write_element(el);
                    this->start_new_line();
                    this->pp_interval_state.back().is_start
                        = this->pp_stream.tellp();
                    continue;
                }
                break;
            case DT_WHITE:
                if (this->pp_values.empty() && this->pp_depth == 0
                    && this->pp_line_length == 0)
                {
                    this->pp_leading_indent = el.e_capture.length();
                    auto shift_cover
                        = line_range::empty_at(this->pp_stream.tellp());
                    shift_string_attrs(
                        this->pp_attrs, shift_cover, -el.e_capture.length());
                    continue;
                }
                break;
            default:
                break;
        }
        this->pp_values.emplace_back(el);
    }
    while (this->pp_depth > 0) {
        this->ascend(this->pp_container_tokens.back());
    }
    this->flush_values();

    attr_line_t combined;
    combined.get_string() = this->pp_stream.str();
    combined.get_attrs() = this->pp_attrs;

    if (!al.empty()) {
        al.append("\n");
    }
    al.append(combined);

    if (this->pp_hier_stage != nullptr) {
        this->pp_hier_stage->hn_parent = this->pp_hier_nodes.back().get();
        this->pp_hier_nodes.back()->hn_children.push_back(
            std::move(this->pp_hier_stage));
    }
    this->pp_hier_stage = std::move(this->pp_hier_nodes.back());
    this->pp_hier_nodes.pop_back();
    if (this->pp_hier_stage->hn_children.size() == 1
        && this->pp_hier_stage->hn_named_children.empty())
    {
        this->pp_hier_stage
            = std::move(this->pp_hier_stage->hn_children.front());
        this->pp_hier_stage->hn_parent = nullptr;
    }
}

void
pretty_printer::write_element(const element& el)
{
    ssize_t start_size = this->pp_stream.tellp();
    if (this->pp_leading_indent == 0 && this->pp_line_length == 0
        && el.e_token == DT_WHITE)
    {
        if (this->pp_depth == 0) {
            this->pp_soft_indent += el.e_capture.length();
        } else {
            auto shift_cover = line_range{
                (int) start_size,
                (int) start_size + el.e_capture.length(),
            };
            shift_string_attrs(
                this->pp_attrs, shift_cover, -el.e_capture.length());
        }
        return;
    }
    if (((this->pp_leading_indent == 0)
         || (this->pp_line_length <= this->pp_leading_indent))
        && el.e_token == DT_LINE)
    {
        this->pp_soft_indent = 0;
        if (this->pp_line_length > 0) {
            this->pp_line_length = 0;
            this->pp_stream << std::endl;
            this->pp_body_lines.top() += 1;
        } else {
            auto shift_cover = line_range::empty_at(start_size);
            shift_string_attrs(this->pp_attrs, shift_cover, -1);
        }
        return;
    }
    int indent_size = 0;
    if (this->pp_line_length == 0) {
        indent_size = this->append_indent();
    }
    if (el.e_token == DT_QUOTED_STRING) {
        auto unquoted_str = auto_mem<char>::malloc(el.e_capture.length() + 1);
        const char* start
            = this->pp_scanner->to_string_fragment(el.e_capture).data();
        auto unq_len = unquote(unquoted_str.in(), start, el.e_capture.length());
        data_scanner ds(
            string_fragment::from_bytes(unquoted_str.in(), unq_len));
        string_attrs_t sa;
        pretty_printer str_pp(
            &ds, sa, this->pp_leading_indent + this->pp_depth * 4);
        attr_line_t result;
        str_pp.append_to(result);
        if (result.get_string().find('\n') != std::string::npos) {
            switch (start[0]) {
                case 'r':
                case 'u':
                    this->pp_stream << start[0];
                    this->pp_stream << start[1] << start[1];
                    break;
                default:
                    this->pp_stream << start[0] << start[0];
                    break;
            }
            this->pp_stream << std::endl << result.get_string();
            if (result.empty() || result.get_string().back() != '\n') {
                this->pp_stream << std::endl;
            }
            this->pp_stream << start[el.e_capture.length() - 1]
                            << start[el.e_capture.length() - 1];
        } else {
            this->pp_stream
                << this->pp_scanner->to_string_fragment(el.e_capture);
        }
    } else {
        this->pp_stream << this->pp_scanner->to_string_fragment(el.e_capture);
    }
    auto shift_cover = line_range::empty_at(start_size);
    shift_string_attrs(this->pp_attrs, shift_cover, indent_size);
    this->pp_line_length += el.e_capture.length();
    if (el.e_token == DT_LINE) {
        this->pp_line_length = 0;
        this->pp_body_lines.top() += 1;
    }
}

int
pretty_printer::append_indent()
{
    auto start_size = this->pp_stream.tellp();
    auto prefix_size = this->pp_leading_indent + this->pp_soft_indent;
    this->pp_stream << std::string(prefix_size, ' ');
    this->pp_soft_indent = 0;
    if (this->pp_stream.tellp() != this->pp_leading_indent) {
        for (int lpc = 0; lpc < this->pp_depth; lpc++) {
            this->pp_stream << "    ";
        }
        if (this->pp_depth > 0) {
            this->pp_indents.insert(this->pp_leading_indent
                                    + 4 * this->pp_depth);
        }
    }
    return (this->pp_stream.tellp() - start_size);
}

bool
pretty_printer::flush_values(bool start_on_depth)
{
    std::optional<data_scanner::capture_t> last_key;
    bool retval = false;

    while (!this->pp_values.empty()) {
        {
            auto& el = this->pp_values.front();
            this->write_element(this->pp_values.front());
            switch (el.e_token) {
                case DT_SYMBOL:
                case DT_CONSTANT:
                case DT_WORD:
                case DT_QUOTED_STRING:
                    last_key = el.e_capture;
                    break;
                case DT_COLON:
                case DT_EQUALS:
                    if (last_key) {
                        this->pp_interval_state.back().is_name
                            = this->pp_scanner
                                  ->to_string_fragment(last_key.value())
                                  .to_string();
                        if (!this->pp_interval_state.back().is_name.empty()) {
                            this->pp_interval_state.back().is_start
                                = static_cast<ssize_t>(this->pp_stream.tellp());
                        }
                        last_key = std::nullopt;
                    }
                    break;
                default:
                    break;
            }
            if (start_on_depth
                && (el.e_token == DT_LSQUARE || el.e_token == DT_LCURLY))
            {
                if (this->pp_line_length > 0) {
                    ssize_t start_size = this->pp_stream.tellp();
                    this->pp_stream << std::endl;

                    auto shift_cover = line_range::empty_at(start_size);
                    shift_string_attrs(this->pp_attrs, shift_cover, 1);
                }
                this->pp_line_length = 0;
            }
        }
        this->pp_values.pop_front();
        retval = true;
    }
    return retval;
}

void
pretty_printer::start_new_line()
{
    bool has_output;

    ssize_t start_size = this->pp_stream.tellp();
    if (this->pp_line_length > 0) {
        this->pp_stream << std::endl;
        auto shift_cover = line_range::empty_at(start_size);
        shift_string_attrs(this->pp_attrs, shift_cover, 1);
        this->pp_line_length = 0;
    }
    has_output = this->flush_values();
    if (has_output && this->pp_line_length > 0) {
        start_size = this->pp_stream.tellp();
        this->pp_stream << std::endl;
        auto shift_cover = line_range::empty_at(start_size);
        shift_string_attrs(this->pp_attrs, shift_cover, 1);
    }
    this->pp_line_length = 0;
    this->pp_body_lines.top() += 1;
}

void
pretty_printer::ascend(data_token_t dt)
{
    if (this->pp_depth > 0) {
        if (this->pp_container_tokens.back() != dt
            && std::find(this->pp_container_tokens.begin(),
                         this->pp_container_tokens.end(),
                         dt)
                == this->pp_container_tokens.end())
        {
            return;
        }

        auto found = false;
        do {
            if (this->pp_container_tokens.back() == dt) {
                found = true;
            }
            int lines = this->pp_body_lines.top();
            this->pp_depth -= 1;
            this->pp_body_lines.pop();
            this->pp_body_lines.top() += lines;

            if (!this->pp_is_xml) {
                this->append_child_node();
            }
            this->pp_interval_state.pop_back();
            this->pp_hier_stage = std::move(this->pp_hier_nodes.back());
            this->pp_hier_nodes.pop_back();
            this->pp_container_tokens.pop_back();
        } while (!found);
    } else {
        this->pp_body_lines.top() = 0;
    }
}

void
pretty_printer::descend(data_token_t dt)
{
    this->pp_depth += 1;
    this->pp_body_lines.push(0);
    this->pp_container_tokens.push_back(dt);
    this->pp_interval_state.resize(this->pp_depth + 1);
    this->pp_hier_nodes.push_back(
        std::make_unique<lnav::document::hier_node>());
}

void
pretty_printer::append_child_node()
{
    auto& ivstate = this->pp_interval_state.back();
    if (!ivstate.is_start) {
        return;
    }

    auto* top_node = this->pp_hier_nodes.back().get();
    auto new_key = ivstate.is_name.empty()
        ? lnav::document::section_key_t{top_node->hn_children.size()}
        : lnav::document::section_key_t{ivstate.is_name};
    this->pp_intervals.emplace_back(
        ivstate.is_start.value(),
        static_cast<ssize_t>(this->pp_stream.tellp()),
        new_key);
    auto new_node = this->pp_hier_stage != nullptr
        ? std::move(this->pp_hier_stage)
        : std::make_unique<lnav::document::hier_node>();
    auto* retval = new_node.get();
    new_node->hn_parent = top_node;
    new_node->hn_start = this->pp_intervals.back().start;
    if (!ivstate.is_name.empty()) {
        top_node->hn_named_children.insert({
            ivstate.is_name,
            retval,
        });
    }
    top_node->hn_children.emplace_back(std::move(new_node));
    ivstate.is_start = std::nullopt;
    ivstate.is_name.clear();
}
