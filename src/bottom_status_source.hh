/**
 * Copyright (c) 2007-2012, Timothy Stack
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

#ifndef _bottom_status_source_hh
#define _bottom_status_source_hh

#include <string>

#include "grep_proc.hh"
#include "textview_curses.hh"
#include "logfile_sub_source.hh"
#include "status_controllers.hh"

class bottom_status_source
    : public status_data_source,
      public grep_proc_control {
public:

    typedef listview_curses::action::mem_functor_t<
            bottom_status_source> lv_functor_t;

    typedef enum {
        BSF_LINE_NUMBER,
        BSF_PERCENT,
        BSF_HITS,
        BSF_FILTERED,
        BSF_LOADING,
        BSF_HELP,

        BSF__MAX
    } field_t;

    bottom_status_source()
        : line_number_wire(*this, &bottom_status_source::update_line_number),
          percent_wire(*this, &bottom_status_source::update_percent),
          marks_wire(*this, &bottom_status_source::update_marks),
          bss_prompt(1024, view_colors::VCR_STATUS),
          bss_error(1024, view_colors::VCR_ALERT_STATUS),
          bss_hit_spinner(0),
          bss_load_percent(0),
          bss_last_filtered_count(0),
          bss_filter_counter(0)
    {
        this->bss_fields[BSF_LINE_NUMBER].set_width(11);
        this->bss_fields[BSF_PERCENT].set_width(4);
        this->bss_fields[BSF_HITS].set_width(36);
        this->bss_fields[BSF_FILTERED].set_width(20);
        this->bss_fields[BSF_FILTERED].set_role(view_colors::VCR_BOLD_STATUS);
        this->bss_fields[BSF_LOADING].set_width(13);
        this->bss_fields[BSF_LOADING].set_cylon(true);
        this->bss_fields[BSF_LOADING].right_justify(true);
        this->bss_fields[BSF_HELP].set_width(14);
        this->bss_fields[BSF_HELP].set_value("?:View Help");
        this->bss_fields[BSF_HELP].right_justify(true);
        this->bss_prompt.set_min_width(35);
        this->bss_prompt.set_share(1);
        this->bss_error.set_min_width(35);
        this->bss_error.set_share(1);
    };

    virtual ~bottom_status_source() { };

    lv_functor_t line_number_wire;
    lv_functor_t percent_wire;
    lv_functor_t marks_wire;

    status_field &get_field(field_t id) { return this->bss_fields[id]; };

    void set_prompt(const std::string &prompt)
    {
        this->bss_prompt.set_value(prompt);
    };

    void grep_error(std::string msg)
    {
        this->bss_error.set_value(msg);
    };

    size_t statusview_fields(void)
    {
        size_t retval;

        if (this->bss_prompt.empty() && this->bss_error.empty()) {
            retval = BSF__MAX;
        }
        else{
            retval = 1;
        }

        return retval;
    };

    status_field &statusview_value_for_field(int field)
    {
        if (!this->bss_error.empty()) {
            return this->bss_error;
        }
        else if (!this->bss_prompt.empty()) {
            return this->bss_prompt;
        }
        else {
            return this->get_field((field_t)field);
        }
    };

    void update_line_number(listview_curses *lc)
    {
        status_field &sf = this->bss_fields[BSF_LINE_NUMBER];

        if (lc->get_inner_height() == 0) {
            sf.set_value("L0");
        }
        else {
            sf.set_value("L%'d", (int)lc->get_top());
        }
    };

    void update_percent(listview_curses *lc)
    {
        status_field &sf  = this->bss_fields[BSF_PERCENT];
        vis_line_t    top = lc->get_top();
        vis_line_t    bottom, height;
        unsigned long width;
        double        percent;

        lc->get_dimensions(height, width);

        if (lc->get_inner_height() > 0) {
            bottom = std::min(top + height - vis_line_t(1),
                              vis_line_t(lc->get_inner_height() - 1));
            percent  = (double)(bottom + 1);
            percent /= (double)lc->get_inner_height();
            percent *= 100.0;
        }
        else {
            percent = 0.0;
        }
        sf.set_value("%3d%%", (int)percent);
    };

    void update_marks(listview_curses *lc)
    {
        textview_curses *tc = static_cast<textview_curses *>(lc);
        vis_bookmarks   &bm = tc->get_bookmarks();
        status_field    &sf = this->bss_fields[BSF_HITS];

        if (bm.find(&textview_curses::BM_SEARCH) != bm.end()) {
            bookmark_vector<vis_line_t> &bv = bm[&textview_curses::BM_SEARCH];
            bookmark_vector<vis_line_t>::iterator lb;

            lb = std::lower_bound(bv.begin(), bv.end(), tc->get_top());
            if (lb != bv.end() && *lb == tc->get_top()) {
                sf.set_value(" Hit %'d of %'d",
                    std::distance(bv.begin(), lb) + 1, tc->get_match_count());
            } else {
                sf.set_value("%'9d hits", tc->get_match_count());
            }
        }
        else {
            sf.clear();
        }
    };

    void update_hits(textview_curses *tc)
    {
        status_field &      sf = this->bss_fields[BSF_HITS];
        view_colors::role_t new_role;

        if (tc->is_searching()) {
            this->bss_hit_spinner += 1;
            if (this->bss_hit_spinner % 2) {
                new_role = view_colors::VCR_ACTIVE_STATUS;
            }
            else{
                new_role = view_colors::VCR_ACTIVE_STATUS2;
            }
            sf.set_cylon(true);
        }
        else {
            new_role = view_colors::VCR_STATUS;
            sf.set_cylon(false);
        }
        // this->bss_error.clear();
        sf.set_role(new_role);
        this->update_marks(tc);
    };

    void update_loading(off_t off, size_t total)
    {
        status_field &sf = this->bss_fields[BSF_LOADING];

        if (total == 0 || (size_t)off == total) {
            sf.set_role(view_colors::VCR_STATUS);
            sf.clear();
        }
        else {
            int pct = (int)(((double)off / (double)total) * 100.0);

            if (this->bss_load_percent != pct) {
                this->bss_load_percent = pct;

                sf.set_role(view_colors::VCR_ACTIVE_STATUS2);
                sf.set_value(" Loading %2d%% ", pct);
            }
        }
    };

    void update_filtered(text_sub_source *tss)
    {
        status_field &sf = this->bss_fields[BSF_FILTERED];

        if (tss == NULL || tss->get_filtered_count() == 0) {
            sf.clear();
        }
        else {
            ui_periodic_timer &timer = ui_periodic_timer::singleton();

            if (tss->get_filtered_count() == this->bss_last_filtered_count) {

                if (timer.fade_diff(this->bss_filter_counter) == 0) {
                    this->bss_fields[BSF_FILTERED].set_role(
                        view_colors::VCR_BOLD_STATUS);
                }
            }
            else {
                this->bss_fields[BSF_FILTERED].set_role(
                    view_colors::VCR_ALERT_STATUS);
                this->bss_last_filtered_count = tss->get_filtered_count();
                timer.start_fade(this->bss_filter_counter, 3);
            }
            sf.set_value("%'9d Not Shown", tss->get_filtered_count());
        }
    };

private:
    status_field bss_prompt;
    status_field bss_error;
    status_field bss_fields[BSF__MAX];
    int          bss_hit_spinner;
    int          bss_load_percent;
    int          bss_last_filtered_count;
    sig_atomic_t bss_filter_counter;
};
#endif
