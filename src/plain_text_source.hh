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

class plain_text_source
        : public text_sub_source {
public:
    plain_text_source(std::string text)
    {
        size_t start = 0, end;

        while ((end = text.find('\n', start)) != std::string::npos) {
            size_t len = (end - start);
            this->tds_lines.push_back(text.substr(start, len));
            start = end + 1;
        }
        if (start < text.length()) {
            this->tds_lines.push_back(text.substr(start));
        }
        this->tds_longest_line = this->compute_longest_line();
    };

    plain_text_source(const std::vector<std::string> &text_lines) {
        this->tds_lines = text_lines;
        this->tds_longest_line = this->compute_longest_line();
    };

    size_t text_line_count()
    {
        return this->tds_lines.size();
    };

    size_t text_line_width() {
        return this->tds_longest_line;
    };

    void text_value_for_line(textview_curses &tc,
                             int row,
                             std::string &value_out,
                             bool no_scrub)
    {
        value_out = this->tds_lines[row];
    };

    size_t text_size_for_line(textview_curses &tc, int row, bool raw) {
        return this->tds_lines[row].length();
    };

private:
    size_t compute_longest_line() {
        size_t retval = 0;
        for (std::vector<std::string>::iterator iter = this->tds_lines.begin();
             iter != this->tds_lines.end();
             ++iter) {
            retval = std::max(retval, iter->length());
        }
        return retval;
    };

    std::vector<std::string> tds_lines;
    size_t tds_longest_line;
};

#endif //LNAV_PLAIN_TEXT_SOURCE_HH
