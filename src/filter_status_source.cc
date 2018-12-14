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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "lnav.hh"
#include "filter_status_source.hh"

static auto TOGGLE_MSG = "Press " ANSI_BOLD("TAB") " to edit ";
static auto HOTKEY_HELP =
    ANSI_BOLD("SPC") ": Enable/Disable | "
    ANSI_BOLD("ENTER") ": Edit | "
    ANSI_BOLD("t") ": Toggle type | "
    ANSI_BOLD("i") "/" ANSI_BOLD("o") ": Create in/out | "
    ANSI_BOLD("D") ": Delete ";

filter_status_source::filter_status_source()
{
    this->tss_fields[TSF_TITLE].set_width(9);
    this->tss_fields[TSF_TITLE].set_role(view_colors::VCR_VIEW_STATUS);
    this->tss_fields[TSF_TITLE].set_value(" Filters ");

    this->tss_fields[TSF_STITCH_TITLE].set_width(2);
    this->tss_fields[TSF_STITCH_TITLE].set_stitch_value(
        view_colors::ansi_color_pair_index(COLOR_BLUE, COLOR_WHITE));

    this->tss_fields[TSF_COUNT].set_width(22);
    this->tss_fields[TSF_COUNT].set_role(view_colors::VCR_STATUS);

    this->tss_fields[TSF_FILTERED].set_width(30);
    this->tss_fields[TSF_FILTERED].set_role(view_colors::VCR_BOLD_STATUS);

    this->tss_fields[TSF_HELP].right_justify(true);
    this->tss_fields[TSF_HELP].set_min_width(80);
    this->tss_fields[TSF_HELP].set_width(1024);
    this->tss_fields[TSF_HELP].set_value(TOGGLE_MSG);
    this->tss_fields[TSF_HELP].set_left_pad(1);
    this->tss_fields[TSF_HELP].set_share(1);

    this->tss_prompt.set_left_pad(1);
    this->tss_prompt.set_min_width(35);
    this->tss_prompt.set_share(1);
    this->tss_error.set_left_pad(1);
    this->tss_error.set_min_width(35);
    this->tss_error.set_share(1);
}

size_t filter_status_source::statusview_fields()
{
    if (lnav_data.ld_mode == LNM_FILTER) {
        this->tss_fields[TSF_HELP].set_value(HOTKEY_HELP);
    } else {
        this->tss_fields[TSF_HELP].set_value(TOGGLE_MSG);
    }

    if (this->tss_prompt.empty() && this->tss_error.empty()) {
        lnav_data.ld_view_stack.top() | [this] (auto tc) {
            text_sub_source *tss = tc->get_sub_source();
            if (tss == nullptr) {
                return;
            }

            filter_stack &fs = tss->get_filters();
            auto enabled_count = 0, filter_count = 0;

            for (const auto &tf : fs) {
                if (tf->is_enabled()) {
                    enabled_count += 1;
                }
                filter_count += 1;
            }
            if (filter_count == 0) {
                this->tss_fields[TSF_COUNT].set_value("");
            } else {
                this->tss_fields[TSF_COUNT].set_value(
                    " " ANSI_BOLD("%d")
                    " of " ANSI_BOLD("%d")
                    " enabled ", enabled_count, filter_count);
            }
        };

        return TSF__MAX;
    }

    return 3;
}

status_field &filter_status_source::statusview_value_for_field(int field)
{
    if (field <= 1) {
        return this->tss_fields[field];
    }

    if (!this->tss_error.empty()) {
        return this->tss_error;
    }

    if (!this->tss_prompt.empty()) {
        return this->tss_prompt;
    }

    return this->tss_fields[field];
}

void filter_status_source::update_filtered(text_sub_source *tss)
{
    status_field &sf = this->tss_fields[TSF_FILTERED];

    if (tss == nullptr || tss->get_filtered_count() == 0) {
        sf.clear();
    }
    else {
        ui_periodic_timer &timer = ui_periodic_timer::singleton();

        if (tss->get_filtered_count() == this->bss_last_filtered_count) {

            if (timer.fade_diff(this->bss_filter_counter) == 0) {
                this->tss_fields[TSF_FILTERED].set_role(
                    view_colors::VCR_BOLD_STATUS);
            }
        }
        else {
            this->tss_fields[TSF_FILTERED].set_role(
                view_colors::VCR_ALERT_STATUS);
            this->bss_last_filtered_count = tss->get_filtered_count();
            timer.start_fade(this->bss_filter_counter, 3);
        }
        sf.set_value("%'9d Lines not shown", tss->get_filtered_count());
    }
}
