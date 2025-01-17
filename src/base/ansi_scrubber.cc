/**
 * Copyright (c) 2013, Timothy Stack
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
 *
 * @file ansi_scrubber.cc
 */

#include <algorithm>

#include "ansi_scrubber.hh"

#include "ansi_vars.hh"
#include "base/lnav_log.hh"
#include "base/opt_util.hh"
#include "config.h"
#include "pcrepp/pcre2pp.hh"
#include "scn/scan.h"

static const lnav::pcre2pp::code&
ansi_regex()
{
    static const auto retval = lnav::pcre2pp::code::from_const(
        R"(\x1b\[([\d=;\?]*)([a-zA-Z])|\x1b\](\d+);(.*?)(?:\x07|\x1b\\)|(?:\X\x08\X)+|(\x16+))");

    return retval;
}

size_t
erase_ansi_escapes(string_fragment input)
{
    thread_local auto md = lnav::pcre2pp::match_data::unitialized();

    const auto& regex = ansi_regex();
    std::optional<int> move_start;
    size_t fill_index = 0;

    auto matcher = regex.capture_from(input).into(md);
    while (true) {
        auto match_res = matcher.matches(PCRE2_NO_UTF_CHECK);

        if (match_res.is<lnav::pcre2pp::matcher::not_found>()) {
            break;
        }
        if (match_res.is<lnav::pcre2pp::matcher::error>()) {
            log_error("ansi scrub regex failure");
            break;
        }

        auto sf = md[0].value();
        auto bs_index_res = sf.codepoint_to_byte_index(1);

        if (move_start) {
            auto move_len = sf.sf_begin - move_start.value();
            memmove(input.writable_data(fill_index),
                    input.data() + move_start.value(),
                    move_len);
            fill_index += move_len;
        } else {
            fill_index = sf.sf_begin;
        }

        if (sf.length() >= 3 && bs_index_res.isOk()
            && sf[bs_index_res.unwrap()] == '\b')
        {
            static const auto OVERSTRIKE_RE
                = lnav::pcre2pp::code::from_const(R"((\X)\x08(\X))");

            auto loop_res = OVERSTRIKE_RE.capture_from(sf).for_each(
                [&fill_index, &input](lnav::pcre2pp::match_data& over_md) {
                    auto lhs = over_md[1].value();
                    if (lhs == "_") {
                        auto rhs = over_md[2].value();
                        memmove(input.writable_data(fill_index),
                                rhs.data(),
                                rhs.length());
                        fill_index += rhs.length();
                    } else {
                        memmove(input.writable_data(fill_index),
                                lhs.data(),
                                lhs.length());
                        fill_index += lhs.length();
                    }
                });
        }
        move_start = md.remaining().sf_begin;
    }

    memmove(input.writable_data(fill_index),
            md.remaining().data(),
            md.remaining().length());
    fill_index += md.remaining().length();

    return fill_index;
}

void
scrub_ansi_string(std::string& str, string_attrs_t* sa)
{
    static thread_local auto md = lnav::pcre2pp::match_data::unitialized();
    static const auto semi_pred = string_fragment::tag1{';'};

    const auto& regex = ansi_regex();
    std::optional<std::string> href;
    size_t href_start = 0;
    string_attrs_t tmp_sa;
    size_t cp_dst = std::string::npos;
    size_t cp_start = std::string::npos;
    int last_origin_end = 0;
    int erased = 0;

    std::replace(str.begin(), str.end(), '\0', ' ');
    auto matcher = regex.capture_from(str).into(md);
    while (true) {
        auto match_res = matcher.matches(PCRE2_NO_UTF_CHECK);

        if (match_res.is<lnav::pcre2pp::matcher::not_found>()) {
            break;
        }
        if (match_res.is<lnav::pcre2pp::matcher::error>()) {
            log_error("ansi scrub regex failure");
            break;
        }

        const auto sf = md[0].value();
        auto bs_index_res = sf.codepoint_to_byte_index(1);

        if (cp_dst != std::string::npos) {
            auto cp_len = sf.sf_begin - cp_start;
            memmove(&str[cp_dst], &str[cp_start], cp_len);
            cp_dst += cp_len;
        } else {
            cp_dst = sf.sf_begin;
        }

        if (sf.length() >= 3 && bs_index_res.isOk()
            && sf[bs_index_res.unwrap()] == '\b')
        {
            ssize_t fill_index = cp_dst;
            line_range bold_range;
            line_range ul_range;
            auto sub_sf = sf;

            while (!sub_sf.empty()) {
                auto lhs_opt = sub_sf.consume_codepoint();
                if (!lhs_opt) {
                    return;
                }
                auto lhs_pair = lhs_opt.value();
                auto mid_opt = lhs_pair.second.consume_codepoint();
                if (!mid_opt) {
                    return;
                }
                auto mid_pair = mid_opt.value();
                auto rhs_opt = mid_pair.second.consume_codepoint();
                if (!rhs_opt) {
                    return;
                }
                auto rhs_pair = rhs_opt.value();

                if (lhs_pair.first == '_' || rhs_pair.first == '_') {
                    if (sa != nullptr && bold_range.is_valid()) {
                        shift_string_attrs(
                            *sa, bold_range.lr_start, -bold_range.length() * 2);
                        tmp_sa.emplace_back(
                            bold_range,
                            VC_STYLE.value(text_attrs::with_bold()));
                        bold_range.clear();
                    }
                    if (ul_range.is_valid()) {
                        ul_range.lr_end += 1;
                    } else {
                        ul_range.lr_start = fill_index;
                        ul_range.lr_end = fill_index + 1;
                    }
                    auto cp = lhs_pair.first == '_' ? rhs_pair.first
                                                    : lhs_pair.first;
                    ww898::utf::utf8::write(cp, [&str, &fill_index](auto ch) {
                        str[fill_index++] = ch;
                    });
                } else if (lhs_pair.first == rhs_pair.first
                           && !fmt::v10::detail::needs_escape(lhs_pair.first))
                {
                    if (sa != nullptr && ul_range.is_valid()) {
                        shift_string_attrs(
                            *sa, ul_range.lr_start, -ul_range.length() * 2);
                        tmp_sa.emplace_back(
                            ul_range,
                            VC_STYLE.value(text_attrs::with_underline()));
                        ul_range.clear();
                    }
                    if (bold_range.is_valid()) {
                        bold_range.lr_end += 1;
                    } else {
                        bold_range.lr_start = fill_index;
                        bold_range.lr_end = fill_index + 1;
                    }
                    try {
                        ww898::utf::utf8::write(lhs_pair.first,
                                                [&str, &fill_index](auto ch) {
                                                    str[fill_index++] = ch;
                                                });
                    } catch (const std::runtime_error& e) {
                        log_error("invalid UTF-8 at %d", sf.sf_begin);
                        return;
                    }
                } else {
                    break;
                }
                sub_sf = rhs_pair.second;
            }

            auto output_size = fill_index - cp_dst;
            if (sa != nullptr && ul_range.is_valid()) {
                shift_string_attrs(
                    *sa, ul_range.lr_start, -ul_range.length() * 2);
                tmp_sa.emplace_back(
                    ul_range, VC_STYLE.value(text_attrs::with_underline()));
                ul_range.clear();
            }
            if (sa != nullptr && bold_range.is_valid()) {
                shift_string_attrs(
                    *sa, bold_range.lr_start, -bold_range.length() * 2);
                tmp_sa.emplace_back(bold_range,
                                    VC_STYLE.value(text_attrs::with_bold()));
                bold_range.clear();
            }
            if (sa != nullptr && output_size > 0 && cp_dst > 0) {
                tmp_sa.emplace_back(
                    line_range{
                        (int) last_origin_end,
                        (int) cp_dst + (int) output_size,
                    },
                    SA_ORIGIN_OFFSET.value(erased));
            }
            last_origin_end = cp_dst + output_size;
            cp_dst = fill_index;
            cp_start = sub_sf.sf_begin;
            erased += sf.length() - output_size;
            continue;
        }

        struct line_range lr;
        text_attrs attrs;
        bool has_attrs = false;
        std::optional<role_t> role;

        if (md[3]) {
            auto osc_id = scn::scan_value<int32_t>(md[3]->to_string_view());

            if (osc_id) {
                switch (osc_id->value()) {
                    case 8:
                        auto split_res = md[4]->split_pair(semi_pred);
                        if (split_res) {
                            // auto params = split_res->first;
                            auto uri = split_res->second;

                            if (href) {
                                if (sa != nullptr) {
                                    tmp_sa.emplace_back(
                                        line_range{
                                            (int) href_start,
                                            (int) cp_dst,
                                        },
                                        VC_HYPERLINK.value(href.value()));
                                }
                                href = std::nullopt;
                            }
                            if (!uri.empty()) {
                                href = uri.to_string();
                                href_start = cp_dst;
                            }
                        }
                        break;
                }
            }
        } else if (md[1]) {
            auto seq = md[1].value();
            auto terminator = md[2].value();

            switch (terminator[0]) {
                case 'm':
                    while (!seq.empty()) {
                        auto ansi_code_res
                            = scn::scan_value<uint8_t>(seq.to_string_view());

                        if (!ansi_code_res) {
                            break;
                        }
                        auto ansi_code = ansi_code_res->value();
                        if (90 <= ansi_code && ansi_code <= 97) {
                            ansi_code -= 60;
                            // XXX attrs.ta_attrs |= A_STANDOUT;
                        }
                        if (30 <= ansi_code && ansi_code <= 37) {
                            attrs.ta_fg_color = palette_color{
                                static_cast<uint8_t>(ansi_code - 30)};
                        }
                        if (40 <= ansi_code && ansi_code <= 47) {
                            attrs.ta_bg_color = palette_color{
                                static_cast<uint8_t>(ansi_code - 40)};
                        }
                        if (ansi_code == 38 || ansi_code == 48) {
                            auto color_code_pair
                                = seq.split_when(semi_pred).second.split_pair(
                                    semi_pred);
                            if (!color_code_pair) {
                                break;
                            }
                            auto color_type = scn::scan_value<int>(
                                color_code_pair->first.to_string_view());
                            if (!color_type.has_value()) {
                                break;
                            }
                            if (color_type->value() == 2) {
                                auto scan_res
                                    = scn::scan<uint8_t, uint8_t, uint8_t>(
                                        color_code_pair->second
                                            .to_string_view(),
                                        "{};{};{}");
                                if (scan_res) {
                                    auto [r, g, b] = scan_res->values();
                                    attrs.ta_fg_color = rgb_color{r, g, b};
                                }
                            } else if (color_type->value() == 5) {
                                auto color_index_pair
                                    = color_code_pair->second.split_when(
                                        semi_pred);
                                auto color_index = scn::scan_value<short>(
                                    color_index_pair.first.to_string_view());
                                if (!color_index.has_value()
                                    || color_index->value() < 0
                                    || color_index->value() > 255)
                                {
                                    break;
                                }
                                if (ansi_code == 38) {
                                    attrs.ta_fg_color = palette_color{
                                        (uint8_t) color_index->value()};
                                } else {
                                    attrs.ta_bg_color = palette_color{
                                        (uint8_t) color_index->value()};
                                }
                                seq = color_index_pair.second;
                            }
                        }
                        switch (ansi_code) {
                            case 1:
                                attrs |= text_attrs::style::bold;
                                break;

                            case 2:
                                // XXX attrs.ta_attrs |= A_DIM;
                                break;

                            case 3:
                                attrs |= text_attrs::style::italic;
                                break;

                            case 4:
                                attrs |= text_attrs::style::underline;
                                break;

                            case 7:
                                attrs |= text_attrs::style::reverse;
                                break;
                        }
                        auto split_pair = seq.split_when(semi_pred);
                        seq = split_pair.second;
                    }
                    has_attrs = true;
                    break;

#if 0
                case 'C': {
                    auto spaces_res
                        = scn::scan_value<unsigned int>(seq.to_string_view());

                    if (spaces_res && spaces_res.value() > 0) {
                        str.insert((std::string::size_type) sf.sf_end,
                                   spaces_res.value(),
                                   ' ');
                    }
                    break;
                }

                case 'H': {
                    unsigned int row = 0, spaces = 0;

                    if (scn::scan(seq.to_string_view(), "{};{}", row, spaces)
                        && spaces > 1)
                    {
                        int ispaces = spaces - 1;
                        if (ispaces > sf.sf_begin) {
                            str.insert((unsigned long) sf.sf_end,
                                       ispaces - sf.sf_begin,
                                       ' ');
                        }
                    }
                    break;
                }
#endif

                case 'O': {
                    auto role_res = scn::scan_value<int>(seq.to_string_view());

                    if (role_res) {
                        role_t role_tmp = (role_t) role_res->value();
                        if (role_tmp > role_t::VCR_NONE
                            && role_tmp < role_t::VCR__MAX)
                        {
                            role = role_tmp;
                            has_attrs = true;
                        }
                    }
                    break;
                }
            }
        }
        if (md[1] || md[3] || md[5]) {
            if (sa != nullptr) {
                shift_string_attrs(*sa, sf.sf_begin, -sf.length());

                if (has_attrs) {
                    for (auto rit = tmp_sa.rbegin(); rit != tmp_sa.rend();
                         rit++)
                    {
                        if (rit->sa_range.lr_end != -1) {
                            continue;
                        }
                        rit->sa_range.lr_end = cp_dst;
                    }
                    lr.lr_start = cp_dst;
                    lr.lr_end = -1;
                    if (!attrs.empty()) {
                        tmp_sa.emplace_back(lr, VC_STYLE.value(attrs));
                    }
                    role | [&lr, &tmp_sa](role_t r) {
                        tmp_sa.emplace_back(lr, VC_ROLE.value(r));
                    };
                }
                if (cp_dst > 0) {
                    tmp_sa.emplace_back(
                        line_range{
                            (int) last_origin_end,
                            (int) cp_dst,
                        },
                        SA_ORIGIN_OFFSET.value(erased));
                }
                last_origin_end = cp_dst;
            }
            erased += sf.length();
        }
        cp_start = sf.sf_end;
    }

    if (cp_dst != std::string::npos) {
        auto cp_len = str.size() - cp_start;
        memmove(&str[cp_dst], &str[cp_start], cp_len);
        cp_dst += cp_len;
        str.resize(cp_dst);
    }
    if (sa != nullptr && last_origin_end > 0 && last_origin_end != str.size()) {
        tmp_sa.emplace_back(line_range{(int) last_origin_end, (int) str.size()},
                            SA_ORIGIN_OFFSET.value(erased));
    }
    if (sa != nullptr) {
        sa->insert(sa->end(), tmp_sa.begin(), tmp_sa.end());
    }
}

void
add_ansi_vars(std::map<std::string, scoped_value_t>& vars)
{
    vars["ansi_csi"] = ANSI_CSI;
    vars["ansi_norm"] = ANSI_NORM;
    vars["ansi_bold"] = ANSI_BOLD_START;
    vars["ansi_underline"] = ANSI_UNDERLINE_START;
    vars["ansi_black"] = ANSI_COLOR(COLOR_BLACK);
    vars["ansi_red"] = ANSI_COLOR(COLOR_RED);
    vars["ansi_green"] = ANSI_COLOR(COLOR_GREEN);
    vars["ansi_yellow"] = ANSI_COLOR(COLOR_YELLOW);
    vars["ansi_blue"] = ANSI_COLOR(COLOR_BLUE);
    vars["ansi_magenta"] = ANSI_COLOR(COLOR_MAGENTA);
    vars["ansi_cyan"] = ANSI_COLOR(COLOR_CYAN);
    vars["ansi_white"] = ANSI_COLOR(COLOR_WHITE);
}
