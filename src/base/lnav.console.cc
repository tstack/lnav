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
#include "lnav_log.hh"
#include "log_level_enum.hh"
#include "pcrepp/pcre2pp.hh"
#include "snippet_highlighters.hh"

using namespace lnav::roles::literals;

namespace lnav::console {

snippet
snippet::from_content_with_offset(intern_string_t src,
                                  const attr_line_t& content,
                                  size_t offset,
                                  const std::string& errmsg)
{
    const auto content_sf = string_fragment::from_str(content.get_string());
    const auto line_with_error = content_sf.find_boundaries_around(
        offset, string_fragment::tag1{'\n'});
    const auto line_with_context = content_sf.find_boundaries_around(
        offset, string_fragment::tag1{'\n'}, 3);
    const auto line_number = content_sf.sub_range(0, offset).count('\n');
    const auto erroff_in_line = offset - line_with_error.sf_begin;

    attr_line_t pointer;

    pointer.append(erroff_in_line, ' ')
        .append("^ "_snippet_border)
        .append(lnav::roles::error(errmsg))
        .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE));

    snippet retval;
    retval.s_content
        = content.subline(line_with_context.sf_begin,
                          line_with_error.sf_end - line_with_context.sf_begin);
    if (line_with_error.sf_end >= (int) retval.s_content.get_string().size()) {
        retval.s_content.append("\n");
    }
    retval.s_content.append(pointer).append(
        content.subline(line_with_error.sf_end,
                        line_with_context.sf_end - line_with_error.sf_end));
    retval.s_location = source_location{
        src,
        static_cast<int32_t>(1 + line_number),
    };

    return retval;
}

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

user_message&
user_message::remove_internal_snippets()
{
    auto new_end = std::remove_if(
        this->um_snippets.begin(),
        this->um_snippets.end(),
        [](const snippet& snip) {
            return snip.s_location.sl_source.to_string_fragment().startswith(
                "__");
        });
    this->um_snippets.erase(new_end, this->um_snippets.end());

    return *this;
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
                retval.append("  ");
                retval.al_attrs.emplace_back(line_range{0, 1},
                                             VC_ICON.value(ui_icon_t::ok));
                break;
            case level::info:
                retval.append("  ").append("info"_info).append(": ");
                retval.al_attrs.emplace_back(line_range{0, 1},
                                             VC_ICON.value(ui_icon_t::info));
                break;
            case level::warning:
                retval.append("  ")
                    .append(lnav::roles::warning("warning"))
                    .append(": ");
                retval.al_attrs.emplace_back(line_range{0, 1},
                                             VC_ICON.value(ui_icon_t::warning));
                break;
            case level::error:
                retval.append("  ")
                    .append(lnav::roles::error("error"))
                    .append(": ");
                retval.al_attrs.emplace_back(line_range{0, 1},
                                             VC_ICON.value(ui_icon_t::error));
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

static std::optional<fmt::terminal_color>
color_to_terminal_color(const styling::color_unit& curses_color)
{
    return curses_color.cu_value.match(
        [](const styling::semantic& s) -> std::optional<fmt::terminal_color> {
            return std::nullopt;
        },
        [](const styling::transparent& s)
            -> std::optional<fmt::terminal_color> { return std::nullopt; },
        [](const palette_color& pc) -> std::optional<fmt::terminal_color> {
            switch (pc) {
                case COLOR_BLACK:
                    return fmt::terminal_color::black;
                case COLOR_RED:
                    return fmt::terminal_color::red;
                case COLOR_GREEN:
                    return fmt::terminal_color::green;
                case COLOR_YELLOW:
                    return fmt::terminal_color::yellow;
                case COLOR_BLUE:
                    return fmt::terminal_color::blue;
                case COLOR_MAGENTA:
                    return fmt::terminal_color::magenta;
                case COLOR_CYAN:
                    return fmt::terminal_color::cyan;
                case COLOR_WHITE:
                    return fmt::terminal_color::white;
                default:
                    return std::nullopt;
            }
        },
        [](const rgb_color& rgb) -> std::optional<fmt::terminal_color> {
            switch (to_ansi_color(rgb)) {
                case ansi_color::black:
                    return fmt::terminal_color::black;
                case ansi_color::cyan:
                    return fmt::terminal_color::cyan;
                case ansi_color::white:
                    return fmt::terminal_color::white;
                case ansi_color::magenta:
                    return fmt::terminal_color::magenta;
                case ansi_color::blue:
                    return fmt::terminal_color::blue;
                case ansi_color::yellow:
                    return fmt::terminal_color::yellow;
                case ansi_color::green:
                    return fmt::terminal_color::green;
                case ansi_color::red:
                    return fmt::terminal_color::red;
                default:
                    return std::nullopt;
            }
        });
}

static bool
get_no_color()
{
    return getenv("NO_COLOR") != nullptr;
}

static bool
get_yes_color()
{
    return getenv("YES_COLOR") != nullptr;
}

static bool
get_fd_tty(int fd)
{
    return isatty(fd);
}

static void
set_rev(fmt::text_style& line_style)
{
    if (line_style.has_emphasis()
        && lnav::enums::to_underlying(line_style.get_emphasis())
            & lnav::enums::to_underlying(fmt::emphasis::reverse))
    {
        auto old_style = line_style;
        auto old_emph = fmt::emphasis(
            lnav::enums::to_underlying(old_style.get_emphasis())
            & ~lnav::enums::to_underlying(fmt::emphasis::reverse));
        line_style = fmt::text_style{};
        if (old_style.has_foreground()) {
            line_style |= fmt::fg(old_style.get_foreground());
        }
        if (old_style.has_background()) {
            line_style |= fmt::bg(old_style.get_background());
        }
        line_style |= old_emph;
    } else {
        line_style |= fmt::emphasis::reverse;
    }
}

static void
role_to_style(const role_t role,
              fmt::text_style& default_bg_style,
              fmt::text_style& default_fg_style,
              fmt::text_style& line_style)
{
    switch (role) {
        case role_t::VCR_TEXT:
        case role_t::VCR_IDENTIFIER:
            break;
        case role_t::VCR_ALT_ROW:
            line_style |= fmt::emphasis::bold;
            break;
        case role_t::VCR_SEARCH:
            set_rev(line_style);
            break;
        case role_t::VCR_ERROR:
        case role_t::VCR_DIFF_DELETE:
            line_style
                |= fmt::fg(fmt::terminal_color::red) | fmt::emphasis::bold;
            break;
        case role_t::VCR_HIDDEN:
        case role_t::VCR_WARNING:
        case role_t::VCR_RE_REPEAT:
            line_style |= fmt::fg(fmt::terminal_color::yellow);
            break;
        case role_t::VCR_COMMENT:
        case role_t::VCR_DIFF_ADD:
            line_style |= fmt::fg(fmt::terminal_color::green);
            break;
        case role_t::VCR_SNIPPET_BORDER:
            line_style |= fmt::fg(fmt::terminal_color::cyan);
            break;
        case role_t::VCR_OK:
            line_style
                |= fmt::emphasis::bold | fmt::fg(fmt::terminal_color::green);
            break;
        case role_t::VCR_FOOTNOTE_BORDER:
            line_style |= fmt::fg(fmt::terminal_color::blue);
            break;
        case role_t::VCR_INFO:
        case role_t::VCR_STATUS:
            line_style
                |= fmt::emphasis::bold | fmt::fg(fmt::terminal_color::magenta);
            break;
        case role_t::VCR_KEYWORD:
        case role_t::VCR_RE_SPECIAL:
            line_style
                |= fmt::emphasis::bold | fmt::fg(fmt::terminal_color::cyan);
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
            line_style
                |= fmt::emphasis::bold | fmt::fg(fmt::terminal_color::magenta);
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
        case role_t::VCR_INLINE_CODE:
        case role_t::VCR_QUOTED_CODE:
            default_fg_style = fmt::fg(fmt::terminal_color::white);
            default_bg_style = fmt::bg(fmt::terminal_color::black);
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

static block_elem_t
wchar_for_icon(ui_icon_t ic)
{
    switch (ic) {
        case ui_icon_t::hidden:
            return {L'\u22ee', role_t::VCR_HIDDEN};
        case ui_icon_t::ok:
            return {L'\u2714', role_t::VCR_OK};
        case ui_icon_t::info:
            return {L'\u24d8', role_t::VCR_INFO};
        case ui_icon_t::warning:
            return {L'\u26a0', role_t::VCR_WARNING};
        case ui_icon_t::error:
            return {L'\u2718', role_t::VCR_ERROR};

        case ui_icon_t::log_level_trace:
            return {L'\U0001F143', role_t::VCR_TEXT};
        case ui_icon_t::log_level_debug:
            return {L'\U0001F133', role_t::VCR_TEXT};
        case ui_icon_t::log_level_info:
            return {L'\U0001F138', role_t::VCR_TEXT};
        case ui_icon_t::log_level_stats:
            return {L'\U0001F142', role_t::VCR_TEXT};
        case ui_icon_t::log_level_notice:
            return {L'\U0001F13d', role_t::VCR_TEXT};
        case ui_icon_t::log_level_warning:
            return {L'\U0001F146', role_t::VCR_WARNING};
        case ui_icon_t::log_level_error:
            return {L'\U0001F134', role_t::VCR_ERROR};
        case ui_icon_t::log_level_critical:
            return {L'\U0001F132', role_t::VCR_ERROR};
        case ui_icon_t::log_level_fatal:
            return {L'\U0001F135', role_t::VCR_ERROR};
    }

    ensure(false);
}

void
println(FILE* file, const attr_line_t& al)
{
    static const auto IS_NO_COLOR = get_no_color();
    static const auto IS_YES_COLOR = get_yes_color();
    static const auto IS_STDOUT_TTY = get_fd_tty(STDOUT_FILENO);
    static const auto IS_STDERR_TTY = get_fd_tty(STDERR_FILENO);

    const auto& str = al.get_string();

    if (IS_NO_COLOR || (file != stdout && file != stderr)
        || (((file == stdout && !IS_STDOUT_TTY)
             || (file == stderr && !IS_STDERR_TTY))
            && !IS_YES_COLOR))
    {
        fmt::print(file, "{}\n", str);
        return;
    }

    auto points = std::set<size_t>{0, static_cast<size_t>(al.length())};

    for (const auto& attr : al.get_attrs()) {
        if (!attr.sa_range.is_valid()) {
            continue;
        }
        points.insert(attr.sa_range.lr_start);
        if (attr.sa_range.lr_end > 0) {
            points.insert(attr.sa_range.lr_end);
        }
    }

    std::optional<size_t> last_point;
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
        std::optional<std::string> href;
        auto replaced = false;

        for (const auto& attr : al.get_attrs()) {
            if (!attr.sa_range.contains(start)
                && !attr.sa_range.contains(point - 1))
            {
                continue;
            }

            try {
                if (attr.sa_type == &VC_ICON) {
                    auto ic = attr.sa_value.get<ui_icon_t>();
                    auto be = wchar_for_icon(ic);
                    auto icon_fg_style = default_fg_style;
                    auto icon_bg_style = default_bg_style;
                    auto icon_style = line_style;
                    std::string utf8_out;

                    role_to_style(
                        be.role, icon_bg_style, icon_fg_style, icon_style);
                    ww898::utf::utf8::write(
                        be.value,
                        [&utf8_out](const char ch) { utf8_out.push_back(ch); });
                    fmt::print(file, icon_style, FMT_STRING("{}"), utf8_out);
                    replaced = true;
                } else if (attr.sa_type == &VC_HYPERLINK) {
                    auto saw = string_attr_wrapper<std::string>(&attr);
                    href = saw.get();
                } else if (attr.sa_type == &VC_BACKGROUND) {
                    auto saw = string_attr_wrapper<styling::color_unit>(&attr);
                    auto color_opt = color_to_terminal_color(saw.get());

                    if (color_opt) {
                        line_style |= fmt::bg(color_opt.value());
                    }
                } else if (attr.sa_type == &VC_FOREGROUND) {
                    auto saw = string_attr_wrapper<styling::color_unit>(&attr);
                    auto color_opt = color_to_terminal_color(saw.get());

                    if (color_opt) {
                        fg_style = fmt::fg(color_opt.value());
                    }
                } else if (attr.sa_type == &VC_STYLE) {
                    auto saw = string_attr_wrapper<text_attrs>(&attr);
                    auto style = saw.get();

                    if (style.has_style(text_attrs::style::reverse)) {
                        set_rev(line_style);
                    }
                    if (style.has_style(text_attrs::style::bold)) {
                        line_style |= fmt::emphasis::bold;
                    }
                    if (style.has_style(text_attrs::style::underline)) {
                        line_style |= fmt::emphasis::underline;
                    }
                    if (style.has_style(text_attrs::style::italic)) {
                        line_style |= fmt::emphasis::italic;
                    }
                    if (style.has_style(text_attrs::style::struck)) {
                        line_style |= fmt::emphasis::strikethrough;
                    }
                    if (!style.ta_fg_color.empty()) {
                        auto color_opt
                            = color_to_terminal_color(style.ta_fg_color);

                        if (color_opt) {
                            fg_style = fmt::fg(color_opt.value());
                        }
                    }
                    if (!style.ta_bg_color.empty()) {
                        auto color_opt
                            = color_to_terminal_color(style.ta_bg_color);

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
                } else if (attr.sa_type == &VC_ROLE
                           || attr.sa_type == &VC_ROLE_FG)
                {
                    auto saw = string_attr_wrapper<role_t>(&attr);
                    auto role = saw.get();

                    role_to_style(
                        role, default_bg_style, default_fg_style, line_style);
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

        if (line_style.has_foreground() && line_style.has_background()
            && !line_style.get_foreground().is_rgb
            && !line_style.get_background().is_rgb
            && line_style.get_foreground().value.term_color
                == line_style.get_background().value.term_color)
        {
            auto new_style = fmt::text_style{};

            if (line_style.has_emphasis()) {
                new_style |= line_style.get_emphasis();
            }
            new_style |= fmt::fg(line_style.get_foreground());
            if (line_style.get_background().value.term_color
                == lnav::enums::to_underlying(fmt::terminal_color::black))
            {
                new_style |= fmt::bg(fmt::terminal_color::white);
            } else {
                new_style |= fmt::bg(fmt::terminal_color::black);
            }
            line_style = new_style;
        }

        if (href) {
            fmt::print(file, FMT_STRING("\x1b]8;;{}\x1b\\"), href.value());
        }
        if (!replaced && start < str.size()) {
            auto actual_end = std::min(str.size(), static_cast<size_t>(point));
            auto sub = std::string{};

            for (auto lpc = start; lpc < actual_end;) {
                auto cp_start = lpc;
                auto read_res = ww898::utf::utf8::read(
                    [&str, &lpc]() { return str[lpc++]; });

                if (read_res.isErr()) {
                    sub.append(fmt::format(
                        FMT_STRING("{:?}"),
                        fmt::string_view{&str[cp_start], lpc - cp_start}));
                    continue;
                }

                auto ch = read_res.unwrap();
                switch (ch) {
                    case '\b':
                        sub.append("\u232b");
                        break;
                    case '\x1b':
                        sub.append("\u238b");
                        break;
                    case '\x07':
                        sub.append("\U0001F514");
                        break;
                    case '\t':
                    case '\n':
                        sub.push_back(ch);
                        break;

                    default:
                        if (ch <= 0x1f) {
                            sub.push_back(0xe2);
                            sub.push_back(0x90);
                            sub.push_back(0x80 + ch);
                        } else {
                            sub.append(&str[cp_start], lpc - cp_start);
                        }
                        break;
                }
            }

            fmt::print(file, line_style, FMT_STRING("{}"), sub);
        }
        if (href) {
            fmt::print(file, FMT_STRING("\x1b]8;;\x1b\\"));
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

}  // namespace lnav::console