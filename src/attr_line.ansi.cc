/**
 * Copyright (c) 2026, Timothy Stack
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

#include <iterator>

#include "attr_line.ansi.hh"

#include "attr_line.render.hh"
#include "config.h"
#include "fmt/format.h"
#include "view_curses.hh"

namespace lnav::ansi {

namespace {

text_attrs
resolve_run(const lnav::render::styled_run& sr)
{
    const auto& vc = view_colors::singleton();
    auto retval = text_attrs{};

    for (const auto& part : sr.sr_parts) {
        retval = lnav::render::apply_attrs(lnav::render::attrs_for_part(part),
                                           retval);
    }

    retval = retval | vc.theme_attrs_for_role(role_t::VCR_TEXT);
    retval.ta_fg_color = vc.ansi_to_theme_rgb(retval.ta_fg_color);
    retval.ta_bg_color = vc.ansi_to_theme_rgb(retval.ta_bg_color);

    return retval;
}

constexpr std::pair<text_attrs::style, const char*> STYLE_CODES[] = {
    {text_attrs::style::bold, "1"},
    {text_attrs::style::italic, "3"},
    {text_attrs::style::underline, "4"},
    {text_attrs::style::undercurl, "4:3"},
    {text_attrs::style::blink, "5"},
    {text_attrs::style::reverse, "7"},
    {text_attrs::style::struck, "9"},
};

/**
 * Drop the parts of the attributes that do not have an SGR equivalent so
 * that runs which would render identically compare equal.
 */
text_attrs
normalize_for_sgr(text_attrs ta)
{
    uint32_t mask = 0;
    for (const auto& [style, code] : STYLE_CODES) {
        mask |= lnav::enums::to_underlying(style);
    }
    ta.ta_attrs &= mask;
    if (!lnav::render::to_rgb(ta.ta_fg_color)) {
        ta.ta_fg_color = styling::color_unit::EMPTY;
    }
    if (!lnav::render::to_rgb(ta.ta_bg_color)) {
        ta.ta_bg_color = styling::color_unit::EMPTY;
    }
    return ta;
}

bool
has_sgr(const text_attrs& ta)
{
    return ta.ta_attrs != 0 || !ta.ta_fg_color.empty()
        || !ta.ta_bg_color.empty();
}

/**
 * Append the SGR sequence for the given normalized attributes.
 */
void
append_sgr(std::string& dst, const text_attrs& ta)
{
    auto out = std::back_inserter(dst);
    auto sep = "\x1b[";

    for (const auto& [style, code] : STYLE_CODES) {
        if (ta.has_style(style)) {
            out = fmt::format_to(out, FMT_STRING("{}{}"), sep, code);
            sep = ";";
        }
    }

    auto fg = lnav::render::to_rgb(ta.ta_fg_color);
    if (fg) {
        out = fmt::format_to(out,
                             FMT_STRING("{}38;2;{};{};{}"),
                             sep,
                             fg->rc_r,
                             fg->rc_g,
                             fg->rc_b);
        sep = ";";
    }
    auto bg = lnav::render::to_rgb(ta.ta_bg_color);
    if (bg) {
        out = fmt::format_to(out,
                             FMT_STRING("{}48;2;{};{};{}"),
                             sep,
                             bg->rc_r,
                             bg->rc_g,
                             bg->rc_b);
    }
    dst.push_back('m');
}

}  // namespace

std::string
to_ansi(const attr_line_t& al)
{
    std::string retval;
    text_attrs curr_attrs;
    const std::string* curr_href = nullptr;

    retval.reserve(al.get_string().size());
    lnav::render::for_each_run(al, [&](const lnav::render::styled_run& sr) {
        auto attrs = normalize_for_sgr(resolve_run(sr));

        if (!lnav::render::same_str(sr.sr_href, curr_href)) {
            if (curr_href != nullptr) {
                retval.append("\x1b]8;;\x1b\\");
            }
            if (sr.sr_href != nullptr) {
                fmt::format_to(std::back_inserter(retval),
                               FMT_STRING("\x1b]8;;{}\x1b\\"),
                               *sr.sr_href);
            }
            curr_href = sr.sr_href;
        }
        if (!(attrs == curr_attrs)) {
            if (has_sgr(curr_attrs)) {
                retval.append("\x1b[0m");
            }
            if (has_sgr(attrs)) {
                append_sgr(retval, attrs);
            }
            curr_attrs = attrs;
        }
        retval.append(sr.sr_text);
    });
    if (curr_href != nullptr) {
        retval.append("\x1b]8;;\x1b\\");
    }
    if (has_sgr(curr_attrs)) {
        retval.append("\x1b[0m");
    }

    return retval;
}

}  // namespace lnav::ansi
