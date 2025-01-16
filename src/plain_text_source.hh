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

#ifndef LNAV_PLAIN_TEXT_SOURCE_HH
#define LNAV_PLAIN_TEXT_SOURCE_HH

#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "base/file_range.hh"
#include "document.sections.hh"
#include "textview_curses.hh"

class plain_text_source
    : public text_sub_source
    , public vis_location_history
    , public text_anchors {
public:
    struct text_line {
        text_line(file_off_t off, attr_line_t value)
            : tl_offset(off), tl_value(std::move(value))
        {
        }

        bool contains_offset(file_off_t off) const
        {
            return (this->tl_offset <= off
                    && off < this->tl_offset + this->tl_value.length());
        }

        file_off_t tl_offset;
        attr_line_t tl_value;
    };

    plain_text_source() = default;

    plain_text_source(const std::string& text);

    plain_text_source(const std::vector<std::string>& text_lines);

    plain_text_source(const std::vector<attr_line_t>& text_lines);

    plain_text_source& set_reverse_selection(bool val)
    {
        this->tds_reverse_selection = val;
        return *this;
    }

    plain_text_source& replace_with_mutable(attr_line_t& text_lines,
                                            text_format_t tf);

    plain_text_source& replace_with(const attr_line_t& text_lines);

    plain_text_source& replace_with(const std::vector<std::string>& text_lines);

    plain_text_source& replace_with(const std::vector<attr_line_t>& text_lines);

    plain_text_source& replace_with(const char *str)
    {
        return this->replace_with(attr_line_t::from_ansi_str(str));
    }

    void clear();

    plain_text_source& truncate_to(size_t max_lines);

    size_t text_line_count() override { return this->tds_lines.size(); }

    bool empty() const { return this->tds_lines.empty(); }

    size_t text_line_width(textview_curses& curses) override;

    line_info text_value_for_line(textview_curses& tc,
                                  int row,
                                  std::string& value_out,
                                  line_flags_t flags) override;

    void text_attrs_for_line(textview_curses& tc,
                             int line,
                             string_attrs_t& value_out) override;

    size_t text_size_for_line(textview_curses& tc,
                              int row,
                              line_flags_t flags) override;

    text_format_t get_text_format() const override;

    const std::vector<text_line>& get_lines() const { return this->tds_lines; }

    plain_text_source& set_text_format(text_format_t format)
    {
        this->tds_text_format = format;
        return *this;
    }

    std::optional<location_history*> get_location_history() override
    {
        return this;
    }

    void text_crumbs_for_line(int line,
                              std::vector<breadcrumb::crumb>& crumbs) override;

    std::optional<vis_line_t> row_for_anchor(const std::string& id) override;
    std::optional<std::string> anchor_for_row(vis_line_t vl) override;
    std::unordered_set<std::string> get_anchors() override;
    std::optional<vis_line_t> adjacent_anchor(vis_line_t vl,
                                              direction dir) override;

protected:
    size_t compute_longest_line();

    std::optional<vis_line_t> line_for_offset(file_off_t off) const;

    std::vector<text_line> tds_lines;
    text_format_t tds_text_format{text_format_t::TF_UNKNOWN};
    size_t tds_longest_line{0};
    bool tds_reverse_selection{false};
    size_t tds_line_indent_size{0};
    lnav::document::metadata tds_doc_sections;
};

#endif  // LNAV_PLAIN_TEXT_SOURCE_HH
