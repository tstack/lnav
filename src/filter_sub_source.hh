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
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/attr_line.hh"
#include "textinput.history.hh"
#include "textinput_curses.hh"
#include "textview_curses.hh"
#include "view_curses.hh"

class textinput_curses;

class filter_sub_source
    : public text_sub_source
    , public list_input_delegate
    , public text_delegate {
public:
    explicit filter_sub_source(std::shared_ptr<textinput_curses> editor);

    using injectable
        = filter_sub_source(std::shared_ptr<textinput_curses> editor);

    filter_sub_source(const filter_sub_source&) = delete;
    filter_sub_source(filter_sub_source&&) = delete;
    filter_sub_source& operator=(const filter_sub_source&) = delete;
    filter_sub_source& operator=(filter_sub_source&&) = delete;

    ~filter_sub_source() override = default;

    bool empty() const override { return false; }

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

    struct render_state {
        textview_curses* rs_top_view;
        bool rs_editing{false};
    };

    struct filter_row {
        filter_row() = default;
        virtual ~filter_row() = default;

        filter_row(const filter_row&) = delete;
        filter_row(filter_row&&) = delete;

        virtual void value_for(const render_state& rs, attr_line_t& al) = 0;
        virtual bool handle_key(textview_curses* top_view, const ncinput& ch)
            = 0;
        virtual bool prime_text_input(textview_curses* top_view,
                                      textinput_curses& ti,
                                      filter_sub_source& parent) = 0;
        virtual void ti_change(textview_curses* top_view, textinput_curses& rc)
            = 0;
        virtual void ti_completion_request(textview_curses* top_view,
                                           textinput_curses& tc,
                                           completion_request_type_t crt) = 0;
        virtual void ti_perform(textview_curses* top_view,
                                textinput_curses& tc,
                                filter_sub_source& parent) = 0;
        virtual void ti_abort(textview_curses* top_view,
                              textinput_curses& tc,
                              filter_sub_source& parent) = 0;
    };

    struct level_filter_row : filter_row {
        void value_for(const render_state& rs, attr_line_t& al) override;
        bool handle_key(textview_curses* top_view, const ncinput& ch) override;
        bool prime_text_input(textview_curses* top_view,
                              textinput_curses& ti,
                              filter_sub_source& parent) override;
        void ti_change(textview_curses* top_view,
                       textinput_curses& rc) override;
        void ti_completion_request(textview_curses* top_view,
                                   textinput_curses& tc,
                                   completion_request_type_t crt) override;
        void ti_perform(textview_curses* top_view,
                        textinput_curses& tc,
                        filter_sub_source& parent) override;
        void ti_abort(textview_curses* top_view,
                      textinput_curses& tc,
                      filter_sub_source& parent) override;
    };

    struct time_filter_row : filter_row {
        explicit time_filter_row(const timeval& tv) : tfr_time(tv) {}
        bool prime_text_input(textview_curses* top_view,
                              textinput_curses& ti,
                              filter_sub_source& parent) override;
        void ti_completion_request(textview_curses* top_view,
                                   textinput_curses& tc,
                                   completion_request_type_t crt) override;

        Result<timeval, std::string> parse_time(textview_curses* top_view,
                                                textinput_curses& tc);

        timeval tfr_time;
    };

    struct min_time_filter_row : time_filter_row {
        explicit min_time_filter_row(const timeval& tv) : time_filter_row(tv) {}
        bool prime_text_input(textview_curses* top_view,
                              textinput_curses& ti,
                              filter_sub_source& parent) override
        {
            parent.fss_min_time = this->tfr_time;
            return time_filter_row::prime_text_input(top_view, ti, parent);
        }
        bool handle_key(textview_curses* top_view, const ncinput& ch) override;
        void value_for(const render_state& rs, attr_line_t& al) override;
        void ti_change(textview_curses* top_view,
                       textinput_curses& rc) override;
        void ti_perform(textview_curses* top_view,
                        textinput_curses& tc,
                        filter_sub_source& parent) override;
        void ti_abort(textview_curses* top_view,
                      textinput_curses& tc,
                      filter_sub_source& parent) override;
    };

    struct max_time_filter_row : time_filter_row {
        explicit max_time_filter_row(const timeval& tv) : time_filter_row(tv) {}
        bool prime_text_input(textview_curses* top_view,
                              textinput_curses& ti,
                              filter_sub_source& parent) override
        {
            parent.fss_max_time = this->tfr_time;
            return time_filter_row::prime_text_input(top_view, ti, parent);
        }
        bool handle_key(textview_curses* top_view, const ncinput& ch) override;
        void value_for(const render_state& rs, attr_line_t& al) override;
        void ti_change(textview_curses* top_view,
                       textinput_curses& rc) override;
        void ti_perform(textview_curses* top_view,
                        textinput_curses& tc,
                        filter_sub_source& parent) override;
        void ti_abort(textview_curses* top_view,
                      textinput_curses& tc,
                      filter_sub_source& parent) override;
    };

    struct text_filter_row : filter_row {
        explicit text_filter_row(const std::shared_ptr<text_filter>& tf)
            : tfr_filter(tf)
        {
        }

        void value_for(const render_state& rs, attr_line_t& al) override;
        bool handle_key(textview_curses* top_view, const ncinput& ch) override;
        bool prime_text_input(textview_curses* top_view,
                              textinput_curses& ti,
                              filter_sub_source& parent) override;
        void ti_change(textview_curses* top_view,
                       textinput_curses& rc) override;
        void ti_completion_request(textview_curses* top_view,
                                   textinput_curses& tc,
                                   completion_request_type_t crt) override;
        void ti_perform(textview_curses* top_view,
                        textinput_curses& tc,
                        filter_sub_source& parent) override;
        void ti_abort(textview_curses* top_view,
                      textinput_curses& tc,
                      filter_sub_source& parent) override;

        std::shared_ptr<text_filter> tfr_filter;
    };

    using row_vector = std::vector<std::unique_ptr<filter_row>>;

    row_vector rows_for(textview_curses* tc) const;

    template<typename T>
    std::pair<vis_line_t, std::unique_ptr<filter_row>> find_row(
        textview_curses* tc)
    {
        auto rows = this->rows_for(tc);
        auto index = 0_vl;
        for (auto& row : rows) {
            if (dynamic_cast<T*>(row.get()) != nullptr) {
                return {index, std::move(row)};
            }
            index += 1_vl;
        }
        ensure(false);
    }

    std::shared_ptr<textinput_curses> fss_editor;
    std::unordered_set<std::string> fss_view_text_possibilities;
    attr_line_t fss_curr_line;
    log_level_t fss_curr_level;
    std::optional<timeval> fss_min_time;
    std::optional<timeval> fss_max_time;

    bool fss_editing{false};
    bool fss_filter_state{false};
};

#endif
