/**
 * Copyright (c) 2020, Timothy Stack
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

#ifndef files_sub_source_hh
#define files_sub_source_hh

#include "file_collection.hh"
#include "plain_text_source.hh"
#include "textview_curses.hh"

class files_sub_source
    : public text_sub_source
    , public list_input_delegate
    , public text_delegate {
public:
    files_sub_source();

    bool list_input_handle_key(listview_curses& lv, int ch) override;

    void list_input_handle_scroll_out(listview_curses& lv) override;

    size_t text_line_count() override;

    size_t text_line_width(textview_curses& curses) override;

    void text_value_for_line(textview_curses& tc,
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

    void text_selection_changed(textview_curses& tc) override;

    size_t fss_last_line_len{0};
    attr_line_t fss_curr_line;
    plain_text_source* fss_details_source{nullptr};
};

struct files_overlay_source : public list_overlay_source {
    bool list_static_overlay(const listview_curses& lv,
                             int y,
                             int bottom,
                             attr_line_t& value_out) override;
};

namespace files_model {

struct no_selection {};

template<typename C, typename T>
struct selection_base {
    int sb_index{0};
    T sb_iter;

    static C build(int index, T iter)
    {
        C retval;

        retval.sb_index = index;
        retval.sb_iter = iter;
        return retval;
    }
};

struct error_selection
    : public selection_base<error_selection,
                            std::pair<std::string, std::string>> {};

struct other_selection
    : public selection_base<
          other_selection,
          std::map<std::string, other_file_descriptor>::iterator> {};

struct file_selection
    : public selection_base<file_selection,
                            std::vector<std::shared_ptr<logfile>>::iterator> {};

using files_list_selection = mapbox::util::
    variant<no_selection, error_selection, other_selection, file_selection>;

files_list_selection from_selection(vis_line_t sel_vis);

}  // namespace files_model

#endif
