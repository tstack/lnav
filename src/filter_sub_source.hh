/**
 * Copyright (c) 2018, Timothy Stack
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

#ifndef filter_sub_source_hh
#define filter_sub_source_hh

#include <memory>
#include <unordered_set>

#include "textinput.history.hh"
#include "textview_curses.hh"

class textinput_curses;

class filter_sub_source
    : public text_sub_source
    , public list_input_delegate
    , public text_delegate {
public:
    explicit filter_sub_source(std::shared_ptr<textinput_curses> editor);

    using injectable
        = filter_sub_source(std::shared_ptr<textinput_curses> editor);

    filter_sub_source(const filter_sub_source*) = delete;

    ~filter_sub_source() override = default;

    bool list_input_handle_key(listview_curses& lv, const ncinput& ch) override;

    void list_input_handle_scroll_out(listview_curses& lv) override;

    void register_view(textview_curses* tc) override;

    size_t text_line_count() override;

    size_t text_line_width(textview_curses& curses) override;

    line_info text_value_for_line(textview_curses& tc,
                                  int line,
                                  std::string& value_out,
                                  line_flags_t flags) override;

    void text_attrs_for_line(textview_curses& tc,
                             int line,
                             string_attrs_t& value_out) override;

    size_t text_size_for_line(textview_curses& tc,
                              int line,
                              line_flags_t raw) override;

    bool text_handle_mouse(textview_curses& tc,
                           const listview_curses::display_line_content_t&,
                           mouse_event& me) override;

    void rl_blur(textinput_curses& tc);

    void rl_change(textinput_curses& rc);

    void rl_history(textinput_curses& tc);

    enum class completion_request_type_t {
        partial,
        full,
    };

    void rl_completion_request_int(textinput_curses& tc,
                                   completion_request_type_t crt);

    void rl_completion_request(textinput_curses& tc);

    void rl_completion(textinput_curses& rc);

    void rl_perform(textinput_curses& rc);

    void rl_abort(textinput_curses& rc);

    std::shared_ptr<textinput_curses> fss_editor;
    lnav::textinput::history fss_regexp_history;
    lnav::textinput::history fss_sql_history;
    std::unordered_set<std::string> fss_view_text_possibilities;
    attr_line_t fss_curr_line;

    bool fss_editing{false};
    bool fss_filter_state{false};
};

#endif
