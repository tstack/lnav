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

#ifndef _top_status_source_hh
#define _top_status_source_hh

#include <string>

#include "lnav_config.hh"
#include "logfile_sub_source.hh"
#include "statusview_curses.hh"

class top_status_source
    : public status_data_source {
public:

    typedef listview_curses::action::mem_functor_t<
            top_status_source> lv_functor_t;

    typedef enum {
        TSF_TIME,
        TSF_PARTITION_NAME,
        TSF_VIEW_NAME,
        TSF_STITCH_VIEW_FORMAT,
        TSF_FORMAT,
        TSF_STITCH_FORMAT_FILENAME,
        TSF_FILENAME,

        TSF__MAX
    } field_t;

    top_status_source()
        : filename_wire(*this, &top_status_source::update_filename),
          view_name_wire(*this, &top_status_source::update_view_name)
    {
        this->tss_fields[TSF_TIME].set_width(28);
        this->tss_fields[TSF_PARTITION_NAME].set_width(34);
        this->tss_fields[TSF_PARTITION_NAME].set_left_pad(1);
        this->tss_fields[TSF_VIEW_NAME].set_width(8);
        this->tss_fields[TSF_VIEW_NAME].set_role(view_colors::VCR_STATUS_TITLE);
        this->tss_fields[TSF_VIEW_NAME].right_justify(true);
        this->tss_fields[TSF_STITCH_VIEW_FORMAT].set_width(2);
        this->tss_fields[TSF_STITCH_VIEW_FORMAT].set_stitch_value(
            view_colors::VCR_STATUS_STITCH_SUB_TO_TITLE,
            view_colors::VCR_STATUS_STITCH_TITLE_TO_SUB);
        this->tss_fields[TSF_STITCH_VIEW_FORMAT].right_justify(true);
        this->tss_fields[TSF_FORMAT].set_width(20);
        this->tss_fields[TSF_FORMAT].set_role(view_colors::VCR_STATUS_SUBTITLE);
        this->tss_fields[TSF_FORMAT].right_justify(true);
        this->tss_fields[TSF_STITCH_FORMAT_FILENAME].set_width(2);
        this->tss_fields[TSF_STITCH_FORMAT_FILENAME].set_stitch_value(
            view_colors::VCR_STATUS_STITCH_NORMAL_TO_SUB,
            view_colors::VCR_STATUS_STITCH_SUB_TO_NORMAL);
        this->tss_fields[TSF_STITCH_FORMAT_FILENAME].right_justify(true);
        this->tss_fields[TSF_FILENAME].set_min_width(35); /* XXX */
        this->tss_fields[TSF_FILENAME].set_share(1);
        this->tss_fields[TSF_FILENAME].right_justify(true);
    };

    lv_functor_t filename_wire;
    lv_functor_t view_name_wire;

    size_t statusview_fields(void) { return TSF__MAX; };

    status_field &statusview_value_for_field(int field)
    {
        return this->tss_fields[field];
    };

    void update_time(const struct timeval &current_time)
    {
        status_field &sf           = this->tss_fields[TSF_TIME];
        char          buffer[32];

        buffer[0] = ' ';
        strftime(&buffer[1], sizeof(buffer) - 1,
                 lnav_config.lc_ui_clock_format.c_str(),
                 localtime(&current_time.tv_sec));
        sf.set_value(buffer);
    };

    void update_time() {
        struct timeval tv;

        gettimeofday(&tv, nullptr);
        this->update_time(tv);
    };

    void update_filename(listview_curses *lc)
    {
        status_field &    sf_partition = this->tss_fields[TSF_PARTITION_NAME];
        status_field &    sf_format   = this->tss_fields[TSF_FORMAT];
        status_field &    sf_filename = this->tss_fields[TSF_FILENAME];
        struct line_range lr(0);

        if (lc->get_inner_height() > 0) {
            string_attrs_t::const_iterator line_attr;
            std::vector<attr_line_t> rows(1);

            lc->get_data_source()->
            listview_value_for_rows(*lc, lc->get_top(), rows);
            string_attrs_t &sa = rows[0].get_attrs();
            line_attr = find_string_attr(sa, &logline::L_FILE);
            if (line_attr != sa.end()) {
                logfile *lf = (logfile *)line_attr->sa_value.sav_ptr;

                if (lf->get_format()) {
                    sf_format.set_value("% 13s",
                                        lf->get_format()->get_name().get());
                }
                else if (!lf->get_filename().empty()) {
                    sf_format.set_value("% 13s", "plain text");
                }
                else{
                    sf_format.clear();
                }

                if (sf_filename.get_width() > (ssize_t) lf->get_filename().length()) {
                    sf_filename.set_value(lf->get_filename());
                } else {
                    sf_filename.set_value(lf->get_unique_path());
                }
            }
            else {
                sf_format.clear();
                sf_filename.clear();
            }

            line_attr = find_string_attr(sa, &logline::L_PARTITION);
            if (line_attr != sa.end()) {
                auto bm = (bookmark_metadata *)line_attr->sa_value.sav_ptr;

                sf_partition.set_value(bm->bm_name.c_str());
            }
            else {
                sf_partition.clear();
            }
        }
        else {
            sf_format.clear();
            if (lc->get_data_source() != NULL) {
                sf_filename.set_value(lc->get_data_source()->listview_source_name(*lc));
            }
        }
    };

    void update_view_name(listview_curses *lc) {
        status_field &sf_view_name = this->tss_fields[TSF_VIEW_NAME];

        sf_view_name.set_value("%s ", lc->get_title().c_str());
    };

private:
    status_field tss_fields[TSF__MAX];
};
#endif
