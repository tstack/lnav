/**
 * Copyright (c) 2019, Timothy Stack
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

#ifndef styling_hh
#define styling_hh

#include <map>
#include <string>

#include "log_level.hh"
#include "intern_string.hh"

struct rgb_color {
    static bool from_str(const string_fragment &color,
                         rgb_color &rgb_out,
                         std::string &errmsg);

    explicit rgb_color(short r = -1, short g = -1, short b = -1)
        : rc_r(r), rc_g(g), rc_b(b) {
    }

    bool empty() const {
        return this->rc_r == -1 && this->rc_g == -1 && this->rc_b == -1;
    }

    short rc_r;
    short rc_g;
    short rc_b;
};

struct style_config {
    std::string sc_color;
    std::string sc_background_color;
    std::string sc_selected_color;
    bool sc_underline{false};
    bool sc_bold{false};
};

struct lnav_theme {
    std::map<std::string, std::string> lt_vars;
    style_config lt_style_identifier;
    style_config lt_style_text;
    style_config lt_style_alt_text;
    style_config lt_style_ok;
    style_config lt_style_error;
    style_config lt_style_warning;
    style_config lt_style_popup;
    style_config lt_style_scrollbar;
    style_config lt_style_hidden;
    style_config lt_style_adjusted_time;
    style_config lt_style_skewed_time;
    style_config lt_style_offset_time;
    style_config lt_style_status_title;
    style_config lt_style_status_subtitle;
    style_config lt_style_keyword;
    style_config lt_style_string;
    style_config lt_style_comment;
    style_config lt_style_variable;
    style_config lt_style_symbol;
    style_config lt_style_number;
    style_config lt_style_re_special;
    style_config lt_style_re_repeat;
    style_config lt_style_diff_delete;
    style_config lt_style_diff_add;
    style_config lt_style_diff_section;
    style_config lt_style_low_threshold;
    style_config lt_style_med_threshold;
    style_config lt_style_high_threshold;
    style_config lt_style_status;
    style_config lt_style_warn_status;
    style_config lt_style_alert_status;
    style_config lt_style_active_status;
    style_config lt_style_inactive_status;
    style_config lt_style_file;
    std::map<log_level_t, style_config> lt_level_styles;
};

#endif
