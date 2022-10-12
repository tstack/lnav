/**
 * Copyright (c) 2022, Timothy Stack
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

#include "lnav.console.hh"

#include "config.h"
#include "fmt/color.h"
#include "itertools.hh"
#include "lnav.console.into.hh"
#include "log_level_enum.hh"
#include "pcrepp/pcre2pp.hh"
#include "snippet_highlighters.hh"
#include "view_curses.hh"

using namespace lnav::roles::literals;

namespace lnav {
namespace console {

user_message
user_message::raw(const attr_line_t& al)
{
    user_message retval;

    retval.um_level = level::raw;
    retval.um_message.append(al);
    return retval;
}

user_message
user_message::error(const attr_line_t& al)
{
    user_message retval;

    retval.um_level = level::error;
    retval.um_message.append(al);
    return retval;
}

user_message
user_message::info(const attr_line_t& al)
{
    user_message retval;

    retval.um_level = level::info;
    retval.um_message.append(al);
    return retval;
}

user_message
user_message::ok(const attr_line_t& al)
{
    user_message retval;

    retval.um_level = level::ok;
    retval.um_message.append(al);
    return retval;
}

user_message
user_message::warning(const attr_line_t& al)
{
    user_message retval;

    retval.um_level = level::warning;
    retval.um_message.append(al);
    return retval;
}

attr_line_t
user_message::to_attr_line(std::set<render_flags> flags) const
{
    auto indent = 1;
    attr_line_t retval;

    if (this->um_level == level::warning) {
        indent = 3;
    }

    if (flags.count(render_flags::prefix)) {
        switch (this->um_level) {
            case level::raw:
                break;
            case level::ok:
                retval.append(lnav::roles::ok("\u2714 "));
                break;
            case level::info:
                retval.append("\u24d8 info"_info).append(": ");
                break;
            case level::warning:
                retval.append(lnav::roles::warning("\u26a0 warning"))
                    .append(": ");
                break;
            case level::error:
                retval.append(lnav::roles::error("\u2718 error")).append(": ");
                break;
        }
    }

    retval.append(this->um_message).append("\n");
    if (!this->um_reason.empty()) {
        bool first_line = true;
        for (const auto& line : this->um_reason.split_lines()) {
            auto role = this->um_level == level::error ? role_t::VCR_ERROR
                                                       : role_t::VCR_WARNING;
            attr_line_t prefix;

            if (first_line) {
                prefix.append(indent, ' ')
                    .append("reason", VC_ROLE.value(role))
                    .append(": ");
                first_line = false;
            } else {
                prefix.append(" |      ", VC_ROLE.value(role))
                    .append(indent, ' ');
            }
            retval.append(prefix).append(line).append("\n");
        }
    }
    if (!this->um_snippets.empty()) {
        for (const auto& snip : this->um_snippets) {
            attr_line_t header;

            header.append(" --> "_snippet_border)
                .append(lnav::roles::file(snip.s_location.sl_source.get()));
            if (snip.s_location.sl_line_number > 0) {
                header.append(":").appendf(FMT_STRING("{}"),
                                           snip.s_location.sl_line_number);
            }
            retval.append(header).append("\n");
            if (!snip.s_content.blank()) {
                auto snippet_lines = snip.s_content.split_lines();
                auto longest_line_length = snippet_lines
                    | lnav::itertools::map(&attr_line_t::utf8_length_or_length)
                    | lnav::itertools::max(40);

                for (auto& line : snippet_lines) {
                    line.pad_to(longest_line_length);
                    retval.append(" | "_snippet_border)
                        .append(line)
                        .append("\n");
                }
            }
        }
    }
    if (!this->um_notes.empty()) {
        for (const auto& note : this->um_notes) {
            bool first_line = true;
            for (const auto& line : note.split_lines()) {
                attr_line_t prefix;

                if (first_line) {
                    prefix.append(" ="_snippet_border)
                        .append(indent, ' ')
                        .append("note"_snippet_border)
                        .append(": ");
                    first_line = false;
                } else {
                    prefix.append("        ").append(indent, ' ');
                }

                retval.append(prefix).append(line).append("\n");
            }
        }
    }
    if (!this->um_help.empty()) {
        bool first_line = true;
        for (const auto& line : this->um_help.split_lines()) {
            attr_line_t prefix;

            if (first_line) {
                prefix.append(" ="_snippet_border)
                    .append(indent, ' ')
                    .append("help"_snippet_border)
                    .append(": ");
                first_line = false;
            } else {
                prefix.append("         ");
            }

            retval.append(prefix).append(line).append("\n");
        }
    }

    return retval;
}

static nonstd::optional<fmt::terminal_color>
curses_color_to_terminal_color(int curses_color)
{
    switch (curses_color) {
        case COLOR_BLACK:
            return fmt::terminal_color::black;
        case COLOR_CYAN:
            return fmt::terminal_color::cyan;
        case COLOR_WHITE:
            return fmt::terminal_color::white;
        case COLOR_MAGENTA:
            return fmt::terminal_color::magenta;
        case COLOR_BLUE:
            return fmt::terminal_color::blue;
        case COLOR_YELLOW:
            return fmt::terminal_color::yellow;
        case COLOR_GREEN:
            return fmt::terminal_color::green;
        case COLOR_RED:
            return fmt::terminal_color::red;
        default:
            return nonstd::nullopt;
    }
}

void
println(FILE* file, const attr_line_t& al)
{
    const auto& str = al.get_string();

    if (getenv("NO_COLOR") != nullptr
        || (!isatty(fileno(file)) && getenv("YES_COLOR") == nullptr))
    {
        fmt::print(file, "{}\n", str);
        return;
    }

    std::set<size_t> points = {0, static_cast<size_t>(al.length())};

    for (const auto& attr : al.get_attrs()) {
        if (!attr.sa_range.is_valid()) {
            continue;
        }
        points.insert(attr.sa_range.lr_start);
        if (attr.sa_range.lr_end > 0) {
            points.insert(attr.sa_range.lr_end);
        }
    }

    nonstd::optional<size_t> last_point;
    for (const auto& point : points) {
        if (!last_point) {
            last_point = point;
            continue;
        }
        auto default_fg_style = fmt::text_style{};
        auto default_bg_style = fmt::text_style{};
        auto line_style = fmt::text_style{};
        auto fg_style = fmt::text_style{};
        auto start = last_point.value();

        for (const auto& attr : al.get_attrs()) {
            if (!attr.sa_range.contains(start)
                && !attr.sa_range.contains(point - 1))
            {
                continue;
            }

            try {
                if (attr.sa_type == &VC_BACKGROUND) {
                    auto saw = string_attr_wrapper<int64_t>(&attr);
                    auto color_opt = curses_color_to_terminal_color(saw.get());

                    if (color_opt) {
                        line_style |= fmt::bg(color_opt.value());
                    }
                } else if (attr.sa_type == &VC_FOREGROUND) {
                    auto saw = string_attr_wrapper<int64_t>(&attr);
                    auto color_opt = curses_color_to_terminal_color(saw.get());

                    if (color_opt) {
                        fg_style = fmt::fg(color_opt.value());
                    }
                } else if (attr.sa_type == &VC_STYLE) {
                    auto saw = string_attr_wrapper<text_attrs>(&attr);
                    auto style = saw.get();

                    if (style.ta_attrs & A_REVERSE) {
                        line_style |= fmt::emphasis::reverse;
                    }
                    if (style.ta_attrs & A_BOLD) {
                        line_style |= fmt::emphasis::bold;
                    }
                    if (style.ta_attrs & A_UNDERLINE) {
                        line_style |= fmt::emphasis::underline;
                    }
                    if (style.ta_fg_color) {
                        auto color_opt = curses_color_to_terminal_color(
                            style.ta_fg_color.value());

                        if (color_opt) {
                            fg_style = fmt::fg(color_opt.value());
                        }
                    }
                    if (style.ta_bg_color) {
                        auto color_opt = curses_color_to_terminal_color(
                            style.ta_bg_color.value());

                        if (color_opt) {
                            line_style |= fmt::bg(color_opt.value());
                        }
                    }
                } else if (attr.sa_type == &SA_LEVEL) {
                    auto level = static_cast<log_level_t>(
                        attr.sa_value.get<int64_t>());

                    switch (level) {
                        case LEVEL_FATAL:
                        case LEVEL_CRITICAL:
                        case LEVEL_ERROR:
                            line_style |= fmt::fg(fmt::terminal_color::red);
                            break;
                        case LEVEL_WARNING:
                            line_style |= fmt::fg(fmt::terminal_color::yellow);
                            break;
                        default:
                            break;
                    }
                } else if (attr.sa_type == &VC_ROLE) {
                    auto saw = string_attr_wrapper<role_t>(&attr);
                    auto role = saw.get();

                    switch (role) {
                        case role_t::VCR_TEXT:
                        case role_t::VCR_IDENTIFIER:
                            break;
                        case role_t::VCR_SEARCH:
                            line_style |= fmt::emphasis::reverse;
                            break;
                        case role_t::VCR_ERROR:
                            line_style |= fmt::fg(fmt::terminal_color::red)
                                | fmt::emphasis::bold;
                            break;
                        case role_t::VCR_WARNING:
                        case role_t::VCR_RE_REPEAT:
                            line_style |= fmt::fg(fmt::terminal_color::yellow);
                            break;
                        case role_t::VCR_COMMENT:
                            line_style |= fmt::fg(fmt::terminal_color::green);
                            break;
                        case role_t::VCR_SNIPPET_BORDER:
                            line_style |= fmt::fg(fmt::terminal_color::cyan);
                            break;
                        case role_t::VCR_OK:
                            line_style |= fmt::emphasis::bold
                                | fmt::fg(fmt::terminal_color::green);
                            break;
                        case role_t::VCR_INFO:
                        case role_t::VCR_STATUS:
                            line_style |= fmt::emphasis::bold
                                | fmt::fg(fmt::terminal_color::magenta);
                            break;
                        case role_t::VCR_KEYWORD:
                        case role_t::VCR_RE_SPECIAL:
                            line_style |= fmt::emphasis::bold
                                | fmt::fg(fmt::terminal_color::cyan);
                            break;
                        case role_t::VCR_STRING:
                            line_style |= fmt::fg(fmt::terminal_color::magenta);
                            break;
                        case role_t::VCR_VARIABLE:
                            line_style |= fmt::emphasis::underline;
                            break;
                        case role_t::VCR_SYMBOL:
                        case role_t::VCR_NUMBER:
                        case role_t::VCR_FILE:
                            line_style |= fmt::emphasis::bold;
                            break;
                        case role_t::VCR_H1:
                            line_style |= fmt::emphasis::bold
                                | fmt::fg(fmt::terminal_color::magenta);
                            break;
                        case role_t::VCR_H2:
                            line_style |= fmt::emphasis::bold;
                            break;
                        case role_t::VCR_H3:
                        case role_t::VCR_H4:
                        case role_t::VCR_H5:
                        case role_t::VCR_H6:
                            line_style |= fmt::emphasis::underline;
                            break;
                        case role_t::VCR_LIST_GLYPH:
                            line_style |= fmt::fg(fmt::terminal_color::yellow);
                            break;
                        case role_t::VCR_QUOTED_CODE:
                            default_fg_style
                                = fmt::fg(fmt::terminal_color::white);
                            default_bg_style
                                = fmt::bg(fmt::terminal_color::black);
                            break;
                        case role_t::VCR_LOW_THRESHOLD:
                            line_style |= fmt::bg(fmt::terminal_color::green);
                            break;
                        case role_t::VCR_MED_THRESHOLD:
                            line_style |= fmt::bg(fmt::terminal_color::yellow);
                            break;
                        case role_t::VCR_HIGH_THRESHOLD:
                            line_style |= fmt::bg(fmt::terminal_color::red);
                            break;
                        default:
                            // log_debug("missing role handler %d", (int) role);
                            break;
                    }
                }
            } catch (const fmt::format_error& e) {
                log_error("style error: %s", e.what());
            }
        }

        if (!line_style.has_foreground() && fg_style.has_foreground()) {
            line_style |= fg_style;
        }
        if (!line_style.has_foreground() && default_fg_style.has_foreground()) {
            line_style |= default_fg_style;
        }
        if (!line_style.has_background() && default_bg_style.has_background()) {
            line_style |= default_bg_style;
        }

        if (start < str.size()) {
            auto actual_end = std::min(str.size(), static_cast<size_t>(point));
            fmt::print(file,
                       line_style,
                       FMT_STRING("{}"),
                       str.substr(start, actual_end - start));
        }
        last_point = point;
    }
    fmt::print(file, "\n");
}

void
print(FILE* file, const user_message& um)
{
    auto al = um.to_attr_line();

    if (endswith(al.get_string(), "\n")) {
        al.erase(al.length() - 1);
    }
    println(file, al);
}

user_message
to_user_message(intern_string_t src, const lnav::pcre2pp::compile_error& ce)
{
    attr_line_t pcre_error_content{ce.ce_pattern};

    lnav::snippets::regex_highlighter(pcre_error_content,
                                      pcre_error_content.length(),
                                      line_range{
                                          0,
                                          (int) pcre_error_content.length(),
                                      });
    pcre_error_content.append("\n")
        .append(ce.ce_offset, ' ')
        .append(lnav::roles::error("^ "))
        .append(lnav::roles::error(ce.get_message()))
        .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE));

    return user_message::error(
               attr_line_t()
                   .append_quoted(ce.ce_pattern)
                   .append(" is not a valid regular expression"))
        .with_reason(ce.get_message())
        .with_snippet(lnav::console::snippet::from(src, pcre_error_content));
}

}  // namespace console
}  // namespace lnav
