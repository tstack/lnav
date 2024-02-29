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

#include "filter_status_source.hh"

#include "base/ansi_scrubber.hh"
#include "base/opt_util.hh"
#include "config.h"
#include "files_sub_source.hh"
#include "filter_sub_source.hh"
#include "lnav.hh"

static auto TOGGLE_MSG = "Press " ANSI_BOLD("TAB") " to edit ";
static auto EXIT_MSG = "Press " ANSI_BOLD("ESC") " to exit ";

static auto CREATE_HELP = ANSI_BOLD("i") "/" ANSI_BOLD("o") ": Create in/out";
static auto ENABLE_HELP = ANSI_BOLD("SPC") ": ";
static auto EDIT_HELP = ANSI_BOLD("ENTER") ": Edit";
static auto TOGGLE_HELP = ANSI_BOLD("t") ": To ";
static auto DELETE_HELP = ANSI_BOLD("D") ": Delete";
static auto FILTERING_HELP = ANSI_BOLD("f") ": ";
static auto JUMP_HELP = ANSI_BOLD("ENTER") ": Jump To";
static auto CLOSE_HELP = ANSI_BOLD("X") ": Close";

filter_status_source::filter_status_source()
{
    this->tss_fields[TSF_TITLE].set_width(14);
    this->tss_fields[TSF_TITLE].set_role(role_t::VCR_STATUS_TITLE);
    this->tss_fields[TSF_TITLE].set_value(" " ANSI_ROLE("T") "ext Filters ",
                                          role_t::VCR_STATUS_TITLE_HOTKEY);

    this->tss_fields[TSF_STITCH_TITLE].set_width(2);
    this->tss_fields[TSF_STITCH_TITLE].set_stitch_value(
        role_t::VCR_STATUS_STITCH_TITLE_TO_NORMAL,
        role_t::VCR_STATUS_STITCH_NORMAL_TO_TITLE);

    this->tss_fields[TSF_COUNT].set_min_width(16);
    this->tss_fields[TSF_COUNT].set_share(1);
    this->tss_fields[TSF_COUNT].set_role(role_t::VCR_STATUS);

    this->tss_fields[TSF_FILTERED].set_min_width(20);
    this->tss_fields[TSF_FILTERED].set_share(1);
    this->tss_fields[TSF_FILTERED].set_role(role_t::VCR_STATUS);

    this->tss_fields[TSF_FILES_TITLE].set_width(7);
    this->tss_fields[TSF_FILES_TITLE].set_role(
        role_t::VCR_STATUS_DISABLED_TITLE);
    this->tss_fields[TSF_FILES_TITLE].set_value(" " ANSI_ROLE("F") "iles ",
                                                role_t::VCR_STATUS_HOTKEY);

    this->tss_fields[TSF_FILES_RIGHT_STITCH].set_width(2);
    this->tss_fields[TSF_FILES_RIGHT_STITCH].set_stitch_value(
        role_t::VCR_STATUS, role_t::VCR_STATUS);

    this->tss_fields[TSF_HELP].right_justify(true);
    this->tss_fields[TSF_HELP].set_width(20);
    this->tss_fields[TSF_HELP].set_value(TOGGLE_MSG);
    this->tss_fields[TSF_HELP].set_left_pad(1);

    this->tss_error.set_min_width(20);
    this->tss_error.set_share(1);
    this->tss_error.set_role(role_t::VCR_ALERT_STATUS);
}

size_t
filter_status_source::statusview_fields()
{
    switch (lnav_data.ld_mode) {
        case ln_mode_t::SEARCH_FILTERS:
        case ln_mode_t::SEARCH_FILES:
            this->tss_fields[TSF_HELP].set_value("");
            break;
        case ln_mode_t::FILTER:
        case ln_mode_t::FILES:
            this->tss_fields[TSF_HELP].set_value(EXIT_MSG);
            break;
        default:
            this->tss_fields[TSF_HELP].set_value(TOGGLE_MSG);
            break;
    }

    if (lnav_data.ld_mode == ln_mode_t::FILES
        || lnav_data.ld_mode == ln_mode_t::SEARCH_FILES)
    {
        this->tss_fields[TSF_FILES_TITLE].set_value(
            " " ANSI_ROLE("F") "iles ", role_t::VCR_STATUS_TITLE_HOTKEY);
        this->tss_fields[TSF_FILES_TITLE].set_role(role_t::VCR_STATUS_TITLE);
        this->tss_fields[TSF_FILES_RIGHT_STITCH].set_stitch_value(
            role_t::VCR_STATUS_STITCH_TITLE_TO_NORMAL,
            role_t::VCR_STATUS_STITCH_NORMAL_TO_TITLE);
        this->tss_fields[TSF_TITLE].set_value(" " ANSI_ROLE("T") "ext Filters ",
                                              role_t::VCR_STATUS_HOTKEY);
        this->tss_fields[TSF_TITLE].set_role(role_t::VCR_STATUS_DISABLED_TITLE);
        this->tss_fields[TSF_STITCH_TITLE].set_stitch_value(role_t::VCR_STATUS,
                                                            role_t::VCR_STATUS);
    } else {
        this->tss_fields[TSF_FILES_TITLE].set_value(" " ANSI_ROLE("F") "iles ",
                                                    role_t::VCR_STATUS_HOTKEY);
        if (lnav_data.ld_active_files.fc_name_to_errors->readAccess()->empty())
        {
            this->tss_fields[TSF_FILES_TITLE].set_role(
                role_t::VCR_STATUS_DISABLED_TITLE);
        } else {
            this->tss_fields[TSF_FILES_TITLE].set_role(
                role_t::VCR_ALERT_STATUS);

            auto& fc = lnav_data.ld_active_files;
            if (fc.fc_name_to_errors->readAccess()->size() == 1) {
                this->tss_error.set_value(" error: a file cannot be opened ");
            } else {
                this->tss_error.set_value(
                    " error: %u files cannot be opened ",
                    lnav_data.ld_active_files.fc_name_to_errors->readAccess()
                        ->size());
            }
        }
        this->tss_fields[TSF_FILES_RIGHT_STITCH].set_stitch_value(
            role_t::VCR_STATUS_STITCH_NORMAL_TO_TITLE,
            role_t::VCR_STATUS_STITCH_TITLE_TO_NORMAL);
        this->tss_fields[TSF_TITLE].set_value(" " ANSI_ROLE("T") "ext Filters ",
                                              role_t::VCR_STATUS_TITLE_HOTKEY);
        this->tss_fields[TSF_TITLE].set_role(role_t::VCR_STATUS_TITLE);
        this->tss_fields[TSF_STITCH_TITLE].set_stitch_value(
            role_t::VCR_STATUS_STITCH_TITLE_TO_NORMAL,
            role_t::VCR_STATUS_STITCH_NORMAL_TO_TITLE);
    }

    lnav_data.ld_view_stack.top() | [this](auto tc) {
        text_sub_source* tss = tc->get_sub_source();
        if (tss == nullptr) {
            return;
        }

        filter_stack& fs = tss->get_filters();
        auto enabled_count = 0, filter_count = 0;

        for (const auto& tf : fs) {
            if (tf->is_enabled()) {
                enabled_count += 1;
            }
            filter_count += 1;
        }
        if (filter_count == 0) {
            this->tss_fields[TSF_COUNT].set_value("");
        } else {
            this->tss_fields[TSF_COUNT].set_value(
                " " ANSI_BOLD("%d") " of " ANSI_BOLD("%d") " enabled ",
                enabled_count,
                filter_count);
        }
    };

    return TSF__MAX;
}

status_field&
filter_status_source::statusview_value_for_field(int field)
{
    if (field == TSF_FILTERED
        && !lnav_data.ld_active_files.fc_name_to_errors->readAccess()->empty())
    {
        return this->tss_error;
    }

    return this->tss_fields[field];
}

void
filter_status_source::update_filtered(text_sub_source* tss)
{
    if (tss == nullptr) {
        return;
    }

    auto& sf = this->tss_fields[TSF_FILTERED];

    if (tss->get_filtered_count() == 0) {
        if (tss->tss_apply_filters) {
            sf.clear();
        } else {
            sf.set_value(
                " \u2718 Filtering disabled, re-enable with " ANSI_BOLD_START
                ":toggle-filtering" ANSI_NORM);
        }
    } else {
        auto& timer = ui_periodic_timer::singleton();
        auto& al = sf.get_value();

        if (tss->get_filtered_count() == this->bss_last_filtered_count) {
            if (timer.fade_diff(this->bss_filter_counter) == 0) {
                this->tss_fields[TSF_FILTERED].set_role(role_t::VCR_STATUS);
                al.with_attr(string_attr(line_range{0, -1},
                                         VC_STYLE.value(text_attrs{A_BOLD})));
            }
        } else {
            this->tss_fields[TSF_FILTERED].set_role(role_t::VCR_ALERT_STATUS);
            this->bss_last_filtered_count = tss->get_filtered_count();
            timer.start_fade(this->bss_filter_counter, 3);
        }
        sf.set_value("%'9d Lines not shown ", tss->get_filtered_count());
    }
}

filter_help_status_source::filter_help_status_source()
{
    this->fss_help.set_min_width(10);
    this->fss_help.set_share(1);
    this->fss_prompt.set_left_pad(1);
    this->fss_prompt.set_min_width(35);
    this->fss_prompt.set_share(1);
    this->fss_error.set_left_pad(25);
    this->fss_error.set_min_width(35);
    this->fss_error.set_share(1);
}

size_t
filter_help_status_source::statusview_fields()
{
    lnav_data.ld_view_stack.top() | [this](auto tc) {
        text_sub_source* tss = tc->get_sub_source();
        if (tss == nullptr) {
            return;
        }

        if (lnav_data.ld_mode == ln_mode_t::FILTER) {
            static auto* editor = injector::get<filter_sub_source*>();
            auto& lv = lnav_data.ld_filter_view;
            auto& fs = tss->get_filters();

            if (editor->fss_editing) {
                auto tf = *(fs.begin() + lv.get_selection());
                auto lang = tf->get_lang() == filter_lang_t::SQL ? "an SQL"
                                                                 : "a regular";

                if (tf->get_type() == text_filter::type_t::INCLUDE) {
                    this->fss_help.set_value(
                        "                        "
                        "Enter %s expression to match lines to filter in:",
                        lang);
                } else {
                    this->fss_help.set_value(
                        "                        "
                        "Enter %s expression to match lines to filter out:",
                        lang);
                }
            } else if (fs.empty()) {
                this->fss_help.set_value("  %s", CREATE_HELP);
            } else {
                auto tf = *(fs.begin() + lv.get_selection());

                this->fss_help.set_value(
                    "  %s  %s%s  %s  %s%s  %s  %s%s",
                    CREATE_HELP,
                    ENABLE_HELP,
                    tf->is_enabled() ? "Disable" : "Enable ",
                    EDIT_HELP,
                    TOGGLE_HELP,
                    tf->get_type() == text_filter::type_t::INCLUDE ? "OUT"
                                                                   : "IN ",
                    DELETE_HELP,
                    FILTERING_HELP,
                    tss->tss_apply_filters ? "Disable Filtering"
                                           : "Enable Filtering");
            }
        } else if (lnav_data.ld_mode == ln_mode_t::FILES
                   && lnav_data.ld_session_loaded)
        {
            auto& lv = lnav_data.ld_files_view;
            auto sel = files_model::from_selection(lv.get_selection());

            sel.match(
                [this](files_model::no_selection) { this->fss_help.clear(); },
                [this](files_model::error_selection) {
                    this->fss_help.set_value("  %s", CLOSE_HELP);
                },
                [this](files_model::other_selection) {
                    this->fss_help.clear();
                },
                [this](files_model::file_selection& fs) {
                    auto& lss = lnav_data.ld_log_source;
                    auto vis_help = "Hide";
                    auto ld_opt = lss.find_data(*fs.sb_iter);
                    if (ld_opt && !ld_opt.value()->ld_visible) {
                        vis_help = "Show";
                    }

                    this->fss_help.set_value(
                        "  %s%s  %s", ENABLE_HELP, vis_help, JUMP_HELP);
                });
        }
    };

    return 1;
}

status_field&
filter_help_status_source::statusview_value_for_field(int field)
{
    if (!this->fss_error.empty()) {
        return this->fss_error;
    }

    if (!this->fss_prompt.empty()) {
        return this->fss_prompt;
    }

    return this->fss_help;
}
