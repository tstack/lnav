/**
 * Copyright (c) 2025, Timothy Stack
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

#ifndef lnav_progress_source_hh
#define lnav_progress_source_hh

#include <map>
#include <chrono>
#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "textview_curses.hh"

class progress_source : public text_sub_source {
public:
    bool poll();
    bool empty() const override;
    size_t text_line_count() override;
    size_t text_line_width(textview_curses& curses) override;
    line_info text_value_for_line(textview_curses& tc,
                                  int line,
                                  std::string& value_out,
                                  line_flags_t flags) override;
    size_t text_size_for_line(textview_curses& tc,
                              int line,
                              line_flags_t raw) override;
    void text_attrs_for_line(textview_curses& tc,
                             int line,
                             string_attrs_t& value_out) override;

private:
    std::vector<attr_line_t> ps_lines;

    struct last_update {
        size_t lu_version{0};
        std::chrono::steady_clock::time_point lu_expire_time;
    };
    std::map<std::string, last_update> ps_last_updates;
};

#endif
