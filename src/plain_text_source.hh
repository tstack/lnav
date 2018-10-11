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

#ifndef LNAV_PLAIN_TEXT_SOURCE_HH
#define LNAV_PLAIN_TEXT_SOURCE_HH

#include <string>
#include <vector>

#include "attr_line.hh"
#include "textview_curses.hh"

class plain_text_source
        : public text_sub_source {
public:
    plain_text_source()
        : tds_text_format(TF_UNKNOWN), tds_longest_line(0) {
    };

    plain_text_source(const std::string &text) : tds_text_format(TF_UNKNOWN) {
        size_t start = 0, end;

        while ((end = text.find('\n', start)) != std::string::npos) {
            size_t len = (end - start);
            this->tds_lines.emplace_back(text.substr(start, len));
            start = end + 1;
        }
        if (start < text.length()) {
            this->tds_lines.emplace_back(text.substr(start));
        }
        this->tds_longest_line = this->compute_longest_line();
    };

    plain_text_source(const std::vector<std::string> &text_lines)
        : tds_text_format(TF_UNKNOWN) {
        this->replace_with(text_lines);
    };

    plain_text_source(const std::vector<attr_line_t> &text_lines)
        : tds_text_format(TF_UNKNOWN) {
        this->tds_lines = text_lines;
        this->tds_longest_line = this->compute_longest_line();
    };

    plain_text_source &replace_with(const attr_line_t &text_lines) {
        this->tds_lines.clear();
        text_lines.split_lines(this->tds_lines);
        this->tds_longest_line = this->compute_longest_line();
        return *this;
    };

    plain_text_source &replace_with(const std::vector<std::string> &text_lines) {
        for (auto &str : text_lines) {
            this->tds_lines.emplace_back(str);
        }
        this->tds_longest_line = this->compute_longest_line();
        return *this;
    };

    void clear() {
        this->tds_lines.clear();
        this->tds_longest_line = 0;
        this->tds_text_format = TF_UNKNOWN;
    };

    plain_text_source &truncate_to(size_t max_lines) {
        while (this->tds_lines.size() > max_lines) {
            this->tds_lines.pop_back();
        }
        return *this;
    };

    size_t text_line_count() {
        return this->tds_lines.size();
    };

    bool empty() const {
        return this->tds_lines.empty();
    };

    size_t text_line_width(textview_curses &curses) {
        return this->tds_longest_line;
    };

    void text_value_for_line(textview_curses &tc,
                             int row,
                             std::string &value_out,
                             line_flags_t flags) {
        value_out = this->tds_lines[row].get_string();
    };

    void text_attrs_for_line(textview_curses &tc, int line,
                             string_attrs_t &value_out) {
        value_out = this->tds_lines[line].get_attrs();
    };

    size_t text_size_for_line(textview_curses &tc, int row, line_flags_t flags) {
        return this->tds_lines[row].length();
    };

    text_format_t get_text_format() const {
        return this->tds_text_format;
    };

    plain_text_source &set_text_format(text_format_t format) {
        this->tds_text_format = format;
        return *this;
    };

private:
    size_t compute_longest_line() {
        size_t retval = 0;
        for (auto &iter : this->tds_lines) {
            retval = std::max(retval, (size_t) iter.length());
        }
        return retval;
    };

    std::vector<attr_line_t> tds_lines;
    text_format_t tds_text_format;
    size_t tds_longest_line;
};

#endif //LNAV_PLAIN_TEXT_SOURCE_HH
