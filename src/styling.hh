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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef styling_hh
#define styling_hh

#include <map>
#include <string>
#include <vector>

#include "base/color_spaces.hh"
#include "base/intern_string.hh"
#include "base/lnav.resolver.hh"
#include "base/log_level_enum.hh"
#include "base/string_util.hh"
#include "yajlpp/yajlpp.hh"

struct term_color {
    short xc_id;
    std::string xc_name;
    std::string xc_hex;
    rgb_color xc_color;
    lab_color xc_lab_color;
};

struct term_color_palette {
    term_color_palette(const char* name, string_fragment_producer& json);

    uint8_t match_color(const lab_color& to_match) const;

    std::vector<term_color> tc_palette;
};

struct style_config {
    std::optional<text_align_t> sc_text_align;
    std::string sc_color;
    std::string sc_background_color;
    bool sc_underline{false};
    bool sc_bold{false};
    bool sc_italic{false};
    bool sc_strike{false};

    bool empty() const
    {
        return this->sc_color.empty() && this->sc_background_color.empty()
            && !this->sc_underline && !this->sc_bold && !this->sc_italic
            && !this->sc_strike;
    }
};

struct highlighter_config {
    factory_container<lnav::pcre2pp::code> hc_regex;
    style_config hc_style;
};

struct icon_config {
    std::optional<std::string> ic_value;
};

struct lnav_theme {
    std::map<std::string, scoped_value_t> lt_vars;

    positioned_property<icon_config> lt_icon_hidden;
    positioned_property<icon_config> lt_icon_ok;
    positioned_property<icon_config> lt_icon_info;
    positioned_property<icon_config> lt_icon_warning;
    positioned_property<icon_config> lt_icon_error;

    positioned_property<icon_config> lt_icon_log_level_trace;
    positioned_property<icon_config> lt_icon_log_level_debug;
    positioned_property<icon_config> lt_icon_log_level_info;
    positioned_property<icon_config> lt_icon_log_level_stats;
    positioned_property<icon_config> lt_icon_log_level_notice;
    positioned_property<icon_config> lt_icon_log_level_warning;
    positioned_property<icon_config> lt_icon_log_level_error;
    positioned_property<icon_config> lt_icon_log_level_critical;
    positioned_property<icon_config> lt_icon_log_level_fatal;

    positioned_property<icon_config> lt_icon_play;
    positioned_property<icon_config> lt_icon_edit;

    positioned_property<style_config> lt_style_identifier;
    positioned_property<style_config> lt_style_text;
    positioned_property<style_config> lt_style_alt_text;
    positioned_property<style_config> lt_style_ok;
    positioned_property<style_config> lt_style_info;
    positioned_property<style_config> lt_style_error;
    positioned_property<style_config> lt_style_warning;
    positioned_property<style_config> lt_style_popup;
    positioned_property<style_config> lt_style_popup_border;
    positioned_property<style_config> lt_style_focused;
    positioned_property<style_config> lt_style_disabled_focused;
    positioned_property<style_config> lt_style_scrollbar;
    positioned_property<style_config> lt_style_hidden;
    positioned_property<style_config> lt_style_cursor_line;
    positioned_property<style_config> lt_style_disabled_cursor_line;
    positioned_property<style_config> lt_style_adjusted_time;
    positioned_property<style_config> lt_style_skewed_time;
    positioned_property<style_config> lt_style_offset_time;
    positioned_property<style_config> lt_style_time_column;
    positioned_property<style_config> lt_style_file_offset;
    positioned_property<style_config> lt_style_invalid_msg;
    positioned_property<style_config> lt_style_status_title;
    positioned_property<style_config> lt_style_status_title_hotkey;
    positioned_property<style_config> lt_style_status_disabled_title;
    positioned_property<style_config> lt_style_status_subtitle;
    positioned_property<style_config> lt_style_status_info;
    positioned_property<style_config> lt_style_status_hotkey;
    positioned_property<style_config> lt_style_inline_code;
    positioned_property<style_config> lt_style_quoted_code;
    positioned_property<style_config> lt_style_code_border;
    positioned_property<style_config> lt_style_keyword;
    positioned_property<style_config> lt_style_string;
    positioned_property<style_config> lt_style_comment;
    positioned_property<style_config> lt_style_doc_directive;
    positioned_property<style_config> lt_style_variable;
    positioned_property<style_config> lt_style_symbol;
    positioned_property<style_config> lt_style_null;
    positioned_property<style_config> lt_style_ascii_ctrl;
    positioned_property<style_config> lt_style_non_ascii;
    positioned_property<style_config> lt_style_number;
    positioned_property<style_config> lt_style_function;
    positioned_property<style_config> lt_style_type;
    positioned_property<style_config> lt_style_sep_ref_acc;
    positioned_property<style_config> lt_style_suggestion;
    positioned_property<style_config> lt_style_selected_text;
    positioned_property<style_config> lt_style_fuzzy_match;
    positioned_property<style_config> lt_style_re_special;
    positioned_property<style_config> lt_style_re_repeat;
    positioned_property<style_config> lt_style_diff_delete;
    positioned_property<style_config> lt_style_diff_add;
    positioned_property<style_config> lt_style_diff_section;
    positioned_property<style_config> lt_style_low_threshold;
    positioned_property<style_config> lt_style_med_threshold;
    positioned_property<style_config> lt_style_high_threshold;
    positioned_property<style_config> lt_style_status;
    positioned_property<style_config> lt_style_warn_status;
    positioned_property<style_config> lt_style_alert_status;
    positioned_property<style_config> lt_style_active_status;
    positioned_property<style_config> lt_style_inactive_status;
    positioned_property<style_config> lt_style_inactive_alert_status;
    positioned_property<style_config> lt_style_file;
    positioned_property<style_config> lt_style_header[6];
    positioned_property<style_config> lt_style_hr;
    positioned_property<style_config> lt_style_hyperlink;
    positioned_property<style_config> lt_style_list_glyph;
    positioned_property<style_config> lt_style_breadcrumb;
    positioned_property<style_config> lt_style_table_border;
    positioned_property<style_config> lt_style_table_header;
    positioned_property<style_config> lt_style_quote_border;
    positioned_property<style_config> lt_style_quoted_text;
    positioned_property<style_config> lt_style_footnote_border;
    positioned_property<style_config> lt_style_footnote_text;
    positioned_property<style_config> lt_style_snippet_border;
    positioned_property<style_config> lt_style_indent_guide;
    std::map<log_level_t, positioned_property<style_config>> lt_level_styles;
    std::map<std::string, highlighter_config> lt_highlights;
};

extern term_color_palette* xterm_colors();
extern term_color_palette* ansi_colors();

extern const json_path_container style_config_handlers;

#endif
