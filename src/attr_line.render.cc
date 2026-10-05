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

#include <algorithm>
#include <utility>

#include "attr_line.render.hh"

#include "base/lnav.console.hh"
#include "config.h"
#include "log_format_fwd.hh"
#include "logfile.hh"
#include "md4cpp.hh"
#include "styling.hh"
#include "view_curses.hh"
#include "ww898/cp_utf8.hpp"

using namespace md4cpp::literals;

namespace lnav::render {

namespace {

void
append_utf8(std::string& out, char32_t ch)
{
    ww898::utf::utf8::write(ch, [&out](const char b) { out.push_back(b); });
}

/**
 * @return The Unicode glyph for one of the VT100 alternate character set
 *   letters that the NCACS_* constants use.
 */
const char*
acs_to_utf8(const char* acs)
{
    if (acs == nullptr || acs[0] == '\0' || acs[1] != '\0') {
        return acs;
    }

    switch (acs[0]) {
        case 'l':
            return "┌";
        case 'm':
            return "└";
        case 'k':
            return "┐";
        case 'j':
            return "┘";
        case 't':
            return "├";
        case 'u':
            return "┤";
        case 'v':
            return "┴";
        case 'w':
            return "┬";
        case 'q':
            return "─";
        case 'x':
            return "│";
        case 'n':
            return "┼";
        case '`':
            return "◆";
        case ',':
            return "←";
        case '+':
            return "→";
        case '.':
            return "↓";
        case '-':
            return "↑";
        default:
            return acs;
    }
}

void
resolve_semantic(styling::color_unit& cu, string_fragment text)
{
    if (std::holds_alternative<styling::semantic>(cu.cu_value)) {
        cu = view_colors::singleton().color_for_ident(text);
    }
}

/**
 * Append the text, with control characters made visible, up to the first
 * byte that is not valid UTF-8.
 *
 * @return The offset of the invalid byte, or the end.
 */
size_t
append_text(std::string& out, const std::string& str, size_t start, size_t end)
{
    for (auto lpc = start; lpc < end;) {
        auto cp_start = lpc;
        auto read_res = ww898::utf::utf8::read(
            [&str, &lpc, end] { return lpc < end ? str[lpc++] : '\0'; });

        if (read_res.isErr()) {
            return cp_start;
        }

        auto ch = read_res.unwrap();
        switch (ch) {
            case '\b':
                out.append("⌫");
                break;
            case '\x1b':
                out.append("⎋");
                break;
            case '\x07':
                out.append(":bell:"_emoji.to_string_view());
                break;
            case '\t':
            case '\n':
                out.push_back(ch);
                break;
            default:
                if (ch <= 0x1f) {
                    append_utf8(out, 0x2400 + ch);
                } else {
                    out.append(&str[cp_start], lpc - cp_start);
                }
                break;
        }
    }

    return end;
}

bool
covers_whole_line(const line_range& lr, size_t length)
{
    return lr.lr_start == 0
        && (lr.lr_end == -1 || static_cast<size_t>(lr.lr_end) >= length);
}

/**
 * @return The note for an attribute that is not drawn, along with its rank
 *   in the order notes are listed in, or nothing for any other attribute.
 */
std::optional<std::pair<int, std::string>>
note_for_attr(const string_attr& attr)
{
    if (attr.sa_type == &L_FILE) {
        const auto& lf = attr.sa_value.get<std::shared_ptr<logfile>>();

        if (lf == nullptr) {
            return std::nullopt;
        }
        return std::make_pair(0, "File: " + lf->get_filename().string());
    }
    if (attr.sa_type == &SA_FORMAT) {
        return std::make_pair(
            1, "Format: " + attr.sa_value.get<intern_string_t>().to_string());
    }
    if (attr.sa_type == &SA_INVALID) {
        return std::make_pair(2,
                              "Invalid: " + attr.sa_value.get<std::string>());
    }
    if (attr.sa_type == &SA_ERROR) {
        return std::make_pair(3, "Error: " + attr.sa_value.get<std::string>());
    }
    return std::nullopt;
}

}  // namespace

text_attrs
attrs_for_part(const style_part& part)
{
    using kind_t = style_part::kind_t;

    const auto& vc = view_colors::singleton();
    auto retval = text_attrs{};

    switch (part.sp_kind) {
        case kind_t::role:
        case kind_t::role_fg: {
            auto role_attrs = vc.theme_attrs_for_role(part.sp_role);

            if (std::holds_alternative<styling::semantic>(
                    role_attrs.ta_fg_color.cu_value))
            {
                role_attrs.ta_fg_color = part.sp_attrs.ta_fg_color;
            }
            if (std::holds_alternative<styling::semantic>(
                    role_attrs.ta_bg_color.cu_value))
            {
                role_attrs.ta_bg_color = part.sp_attrs.ta_bg_color;
            }
            if (part.sp_kind == kind_t::role) {
                retval = role_attrs;
            } else {
                retval.ta_fg_color = role_attrs.ta_fg_color;
            }
            break;
        }
        case kind_t::level:
            retval = vc.theme_attrs_for_level(part.sp_level);
            break;
        case kind_t::attrs:
            retval = part.sp_attrs;
            break;
        case kind_t::fg:
            retval.ta_fg_color = part.sp_attrs.ta_fg_color;
            break;
        case kind_t::bg:
            retval.ta_bg_color = part.sp_attrs.ta_bg_color;
            break;
    }

    return retval;
}

text_attrs
apply_attrs(const text_attrs& attrs, const text_attrs& resolved)
{
    auto clear_rev = attrs.has_style(text_attrs::style::reverse)
        && resolved.has_style(text_attrs::style::reverse);
    auto retval = attrs | resolved;

    if (clear_rev) {
        retval.clear_style(text_attrs::style::reverse);
    }
    return retval;
}

void
for_each_run(const attr_line_t& al,
             const std::function<void(const styled_run&)>& cb)
{
    const auto& vc = view_colors::singleton();
    const auto& str = al.get_string();
    const auto str_sf = string_fragment::from_str(str);

    // Applied in the same order as on the screen, so that a later attribute
    // takes precedence the same way.
    auto sorted_attrs = std::vector<const string_attr*>{};
    sorted_attrs.reserve(al.get_attrs().size());
    for (const auto& attr : al.get_attrs()) {
        if (attr.sa_range.is_valid()) {
            sorted_attrs.emplace_back(&attr);
        }
    }
    std::stable_sort(
        sorted_attrs.begin(),
        sorted_attrs.end(),
        [](const string_attr* lhs, const string_attr* rhs) {
            return *lhs < *rhs;
        });

    auto attr_ranges = std::vector<std::pair<line_range, const string_attr*>>{};
    attr_ranges.reserve(sorted_attrs.size());
    for (const auto* attr : sorted_attrs) {
        attr_ranges.emplace_back(
            lnav::console::detail::to_byte_range(str, attr->sa_range), attr);
    }

    // Boundaries are moved back to the start of the character they land in
    // so that no character is split between two runs.
    auto points = std::vector<size_t>{};
    points.reserve(attr_ranges.size() * 2 + 2);
    points.emplace_back(0);
    points.emplace_back(str.size());
    for (const auto& ar : attr_ranges) {
        points.emplace_back(str_sf.start_of_codepoint(ar.first.lr_start));
        if (ar.first.lr_end > 0) {
            points.emplace_back(str_sf.start_of_codepoint(ar.first.lr_end));
        }
    }
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());

    // The ranges are admitted by their byte offset, which can be in a
    // different order than the screen order when units are mixed, and are
    // kept in screen order once admitted.
    auto admit_order = std::vector<size_t>(attr_ranges.size());
    for (size_t lpc = 0; lpc < admit_order.size(); lpc++) {
        admit_order[lpc] = lpc;
    }
    std::stable_sort(admit_order.begin(),
                     admit_order.end(),
                     [&attr_ranges](size_t lhs, size_t rhs) {
                         return attr_ranges[lhs].first.lr_start
                             < attr_ranges[rhs].first.lr_start;
                     });
    auto next_admit = size_t{0};
    auto active = std::vector<size_t>{};
    active.reserve(attr_ranges.size());

    auto run = styled_run{};
    auto replacement = std::string{};
    for (size_t point_index = 1; point_index < points.size(); point_index++) {
        const auto start = points[point_index - 1];
        const auto point = points[point_index];
        auto has_replacement = false;

        run.sr_parts.clear();
        run.sr_href = nullptr;
        run.sr_id = nullptr;
        run.sr_notes.clear();
        run.sr_text.clear();

        // Any range that applies to this run starts before its end and does
        // not end before its start.
        while (next_admit < admit_order.size()
               && attr_ranges[admit_order[next_admit]].first.lr_start
                   < static_cast<int>(point))
        {
            const auto index = admit_order[next_admit];
            active.insert(std::lower_bound(active.begin(), active.end(), index),
                          index);
            next_admit += 1;
        }
        active.erase(std::remove_if(active.begin(),
                                    active.end(),
                                    [&attr_ranges, start](size_t index) {
                                        const auto& lr
                                            = attr_ranges[index].first;
                                        return lr.lr_end != -1
                                            && lr.lr_end
                                            <= static_cast<int>(start);
                                    }),
                     active.end());

        for (const auto index : active) {
            const auto& attr_range = attr_ranges[index].first;
            const auto& attr = *attr_ranges[index].second;

            if (!attr_range.contains(start) && !attr_range.contains(point - 1))
            {
                continue;
            }

            auto attr_text = str_sf.sub_range(
                attr_range.lr_start,
                std::min(static_cast<size_t>(attr_range.lr_end), str.size()));
            auto add_role = [&](style_part::kind_t kind, role_t role) {
                auto part = style_part{kind, role};

                if (role != role_t::VCR_NONE) {
                    auto role_attrs = vc.attrs_for_role(role);
                    if (std::holds_alternative<styling::semantic>(
                            role_attrs.ta_fg_color.cu_value))
                    {
                        part.sp_attrs.ta_fg_color
                            = vc.color_for_ident(attr_text);
                    }
                    if (kind == style_part::kind_t::role
                        && std::holds_alternative<styling::semantic>(
                            role_attrs.ta_bg_color.cu_value))
                    {
                        part.sp_attrs.ta_bg_color
                            = vc.color_for_ident(attr_text);
                    }
                }
                run.sr_parts.emplace_back(part);
            };
            auto add_attrs = [&](style_part::kind_t kind, text_attrs ta) {
                resolve_semantic(ta.ta_fg_color, attr_text);
                resolve_semantic(ta.ta_bg_color, attr_text);

                auto part = style_part{kind};
                part.sp_attrs = ta;
                run.sr_parts.emplace_back(part);
            };
            auto add_glyph = [&](const block_elem_t& be) {
                add_role(style_part::kind_t::role, be.role);
                has_replacement = true;
                replacement.clear();
                if (start == static_cast<size_t>(attr_range.lr_start)) {
                    append_utf8(replacement, be.value);
                }
            };

            if (attr.sa_type == &VC_ROLE) {
                add_role(style_part::kind_t::role, attr.sa_value.get<role_t>());
            } else if (attr.sa_type == &VC_ROLE_FG) {
                add_role(style_part::kind_t::role_fg,
                         attr.sa_value.get<role_t>());
            } else if (attr.sa_type == &SA_LEVEL) {
                auto part = style_part{style_part::kind_t::level};
                part.sp_level
                    = static_cast<log_level_t>(attr.sa_value.get<int64_t>());
                run.sr_parts.emplace_back(part);
            } else if (attr.sa_type == &SAT_UNSUPPORTED) {
                add_role(style_part::kind_t::role, role_t::VCR_WARNING);
                add_attrs(style_part::kind_t::attrs,
                          text_attrs::with_reverse());
            } else if (attr.sa_type == &VC_STYLE) {
                add_attrs(style_part::kind_t::attrs,
                          attr.sa_value.get<text_attrs>());
            } else if (attr.sa_type == &VC_FOREGROUND) {
                auto ta = text_attrs{};
                ta.ta_fg_color = attr.sa_value.get<styling::color_unit>();
                add_attrs(style_part::kind_t::fg, ta);
            } else if (attr.sa_type == &VC_BACKGROUND) {
                auto ta = text_attrs{};
                ta.ta_bg_color = attr.sa_value.get<styling::color_unit>();
                add_attrs(style_part::kind_t::bg, ta);
            } else if (attr.sa_type == &VC_HYPERLINK) {
                run.sr_href = &attr.sa_value.get<std::string>();
            } else if (attr.sa_type == &VC_ANCHOR) {
                if (start == static_cast<size_t>(attr_range.lr_start)) {
                    run.sr_id = &attr.sa_value.get<std::string>();
                }
            } else if (attr.sa_type == &VC_ICON) {
                add_glyph(vc.wchar_for_icon(attr.sa_value.get<ui_icon_t>()));
            } else if (attr.sa_type == &VC_BLOCK_ELEM) {
                add_glyph(attr.sa_value.get<block_elem_t>());
            } else if (attr.sa_type == &VC_GRAPHIC) {
                // The glyph takes the place of each character in the run.
                const auto* graphic
                    = acs_to_utf8(attr.sa_value.get<const char*>());
                auto run_sf = str_sf.sub_range(start, point);

                has_replacement = true;
                replacement.clear();
                for (size_t lpc = 0; lpc < run_sf.length(); lpc++) {
                    if ((run_sf.data()[lpc] & 0xc0) != 0x80) {
                        replacement.append(graphic);
                    }
                }
            } else if (!covers_whole_line(attr_range, str.size())) {
                // The notes for the whole line come from line_notes().
                auto note = note_for_attr(attr);

                if (note
                    && std::find(run.sr_notes.begin(),
                                 run.sr_notes.end(),
                                 note->second)
                        == run.sr_notes.end())
                {
                    run.sr_notes.emplace_back(std::move(note->second));
                }
            }
        }

        if (has_replacement) {
            run.sr_text.swap(replacement);
        } else {
            const auto end = std::min(str.size(), point);

            for (auto pos = start; pos < end;) {
                pos = append_text(run.sr_text, str, pos, end);
                if (pos >= end) {
                    break;
                }

                if (!run.sr_text.empty() || run.sr_id != nullptr) {
                    cb(run);
                    run.sr_text.clear();
                    run.sr_id = nullptr;
                }
                run.sr_parts.emplace_back(
                    style_part{style_part::kind_t::role, role_t::VCR_NON_ASCII});
                run.sr_text = "�";
                cb(run);
                run.sr_parts.pop_back();
                run.sr_text.clear();
                pos += 1;
            }
        }

        if (run.sr_text.empty() && run.sr_id == nullptr) {
            continue;
        }
        cb(run);
    }
}

std::vector<std::string>
line_notes(const attr_line_t& al)
{
    const auto& str = al.get_string();
    auto notes = std::vector<std::pair<int, std::string>>{};

    for (const auto& attr : al.get_attrs()) {
        if (!attr.sa_range.is_valid()
            || !covers_whole_line(
                lnav::console::detail::to_byte_range(str, attr.sa_range),
                str.size()))
        {
            continue;
        }

        auto note = note_for_attr(attr);
        if (note) {
            notes.emplace_back(std::move(note.value()));
        }
    }
    std::stable_sort(
        notes.begin(), notes.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

    auto retval = std::vector<std::string>{};
    for (auto& note : notes) {
        if (std::find(retval.begin(), retval.end(), note.second)
            == retval.end())
        {
            retval.emplace_back(std::move(note.second));
        }
    }
    return retval;
}

std::string
to_text(const attr_line_t& al)
{
    std::string retval;

    retval.reserve(al.get_string().size());
    for_each_run(al,
                 [&retval](const styled_run& run) { retval.append(run.sr_text); });

    return retval;
}

std::optional<rgb_color>
to_rgb(const styling::color_unit& cu)
{
    return std::visit(
        styling::overload{
            [](const styling::transparent&) -> std::optional<rgb_color> {
                return std::nullopt;
            },
            [](const styling::semantic&) -> std::optional<rgb_color> {
                return std::nullopt;
            },
            // Color names in themes are looked up in the xterm palette and
            // the smaller palettes are a prefix of it, so it covers every
            // index.
            [](const palette_color& pc) -> std::optional<rgb_color> {
                const auto* pal = xterm_colors();

                if (pc >= pal->tc_palette.size()) {
                    return std::nullopt;
                }
                return pal->tc_palette[pc].xc_color;
            },
            [](const rgb_color& rgb) -> std::optional<rgb_color> {
                if (rgb.empty()) {
                    return std::nullopt;
                }
                return rgb;
            },
        },
        cu.cu_value);
}

}  // namespace lnav::render
