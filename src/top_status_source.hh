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
        : filename_wire(*this, &top_status_source::update_filename)
    {
        this->tss_fields[TSF_TIME].set_width(24);
        this->tss_fields[TSF_PARTITION_NAME].set_width(34);
        this->tss_fields[TSF_VIEW_NAME].set_width(6);
        this->tss_fields[TSF_VIEW_NAME].right_justify(true);
        this->tss_fields[TSF_STITCH_VIEW_FORMAT].set_width(2);
        this->tss_fields[TSF_STITCH_VIEW_FORMAT].set_stitch_value(
            view_colors::ansi_color_pair_index(COLOR_CYAN, COLOR_BLUE));
        this->tss_fields[TSF_STITCH_VIEW_FORMAT].right_justify(true);
        this->tss_fields[TSF_FORMAT].set_width(20);
        this->tss_fields[TSF_FORMAT].right_justify(true);
        this->tss_fields[TSF_STITCH_FORMAT_FILENAME].set_width(2);
        this->tss_fields[TSF_STITCH_FORMAT_FILENAME].set_stitch_value(
            view_colors::ansi_color_pair_index(COLOR_WHITE, COLOR_CYAN));
        this->tss_fields[TSF_STITCH_FORMAT_FILENAME].right_justify(true);
        this->tss_fields[TSF_FILENAME].set_min_width(35); /* XXX */
        this->tss_fields[TSF_FILENAME].set_share(1);
        this->tss_fields[TSF_FILENAME].right_justify(true);
    };

    lv_functor_t filename_wire;

    size_t statusview_fields(void) { return TSF__MAX; };

    status_field &statusview_value_for_field(int field)
    {
        return this->tss_fields[field];
    };

    void update_time(void)
    {
        status_field &sf           = this->tss_fields[TSF_TIME];
        time_t        current_time = time(NULL);
        char          buffer[32];

        strftime(buffer, sizeof(buffer),
                 "%a %b %d %H:%M:%S %Z",
                 localtime(&current_time));
        sf.set_value(buffer);
    };

    void update_filename(listview_curses *lc)
    {
        status_field &    sf_partition = this->tss_fields[TSF_PARTITION_NAME];
        status_field &    sf_format   = this->tss_fields[TSF_FORMAT];
        status_field &    sf_filename = this->tss_fields[TSF_FILENAME];
        struct line_range lr(0);

        if (lc->get_inner_height() > 0) {
            string_attrs_t::const_iterator line_attr;
            attr_line_t           al;

            lc->get_data_source()->
            listview_value_for_row(*lc, lc->get_top(), al);
            string_attrs_t &sa = al.get_attrs();
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

                sf_filename.set_value(lf->get_filename());
            }
            else {
                sf_format.clear();
                sf_filename.clear();
            }

            line_attr = find_string_attr(sa, &logline::L_PARTITION);
            if (line_attr != sa.end()) {
                bookmark_metadata *bm = (bookmark_metadata *)line_attr->sa_value.sav_ptr;

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
        sf_format.get_value().get_attrs().push_back(
            string_attr(lr, &view_curses::VC_STYLE,
                A_REVERSE | view_colors::ansi_color_pair(COLOR_CYAN, COLOR_BLACK)));
    };

private:
    status_field tss_fields[TSF__MAX];
};
#endif
