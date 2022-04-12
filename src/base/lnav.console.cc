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
#include "view_curses.hh"

namespace lnav {
namespace console {

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
    attr_line_t retval;

    if (flags.count(render_flags::prefix)) {
        switch (this->um_level) {
            case level::ok:
                retval.append(lnav::roles::ok("\u2714 "));
                break;
            case level::info:
                retval.append(lnav::roles::status("\u24d8 info")).append(": ");
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
            attr_line_t prefix;

            if (first_line) {
                prefix.append(lnav::roles::error(" reason")).append(": ");
                first_line = false;
            } else {
                prefix.append(lnav::roles::error(" |       "));
            }
            retval.append(prefix).append(line).append("\n");
        }
    }
    if (!this->um_snippets.empty()) {
        for (const auto& snip : this->um_snippets) {
            attr_line_t header;

            header.append(lnav::roles::comment(" --> "))
                .append(lnav::roles::file(snip.s_source));
            if (snip.s_line > 0) {
                header.append(":").append(FMT_STRING("{}"), snip.s_line);
                if (snip.s_column > 0) {
                    header.append(":").append(FMT_STRING("{}"), snip.s_column);
                }
            }
            retval.append(header).append("\n");
            if (!snip.s_content.blank()) {
                for (const auto& line : snip.s_content.split_lines()) {
                    retval.append(lnav::roles::comment(" | "))
                        .append(line)
                        .append("\n");
                }
            }
        }
    }
    if (!this->um_notes.empty()) {
        bool first_line = true;
        for (const auto& note : this->um_notes) {
            for (const auto& line : note.split_lines()) {
                attr_line_t prefix;

                if (first_line) {
                    prefix.append(lnav::roles::comment(" = note")).append(": ");
                    first_line = false;
                } else {
                    prefix.append("         ");
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
                prefix.append(lnav::roles::comment(" = help")).append(": ");
                first_line = false;
            } else {
                prefix.append("         ");
            }

            retval.append(prefix).append(line).append("\n");
        }
    }

    return retval;
}

void
println(FILE* file, const attr_line_t& al)
{
    const auto& str = al.get_string();

    if (!isatty(fileno(file))) {
        fmt::print(file, "{}\n", str);
        return;
    }

    string_attrs_t style_attrs;

    for (const auto& sa : al.get_attrs()) {
        if (sa.sa_type != &VC_ROLE) {
            continue;
        }

        style_attrs.emplace_back(sa);
    }

    std::sort(style_attrs.begin(), style_attrs.end(), [](auto lhs, auto rhs) {
        return lhs.sa_range < rhs.sa_range;
    });

    auto start = size_t{0};
    for (const auto& attr : style_attrs) {
        fmt::print(
            file, "{}", str.substr(start, attr.sa_range.lr_start - start));
        if (attr.sa_type == &VC_ROLE) {
            auto saw = string_attr_wrapper<role_t>(&attr);
            auto role = saw.get();
            auto line_style = fmt::text_style();

            switch (role) {
                case role_t::VCR_ERROR:
                    line_style = fmt::fg(fmt::terminal_color::red);
                    break;
                case role_t::VCR_WARNING:
                    line_style = fmt::fg(fmt::terminal_color::yellow);
                    break;
                case role_t::VCR_COMMENT:
                    line_style = fmt::fg(fmt::terminal_color::cyan);
                    break;
                case role_t::VCR_OK:
                    line_style = fmt::emphasis::bold
                        | fmt::fg(fmt::terminal_color::red);
                    break;
                case role_t::VCR_STATUS:
                    line_style = fmt::emphasis::bold
                        | fmt::fg(fmt::terminal_color::magenta);
                    break;
                case role_t::VCR_VARIABLE:
                    line_style = fmt::emphasis::underline;
                    break;
                case role_t::VCR_SYMBOL:
                case role_t::VCR_NUMBER:
                case role_t::VCR_FILE:
                    line_style = fmt::emphasis::bold;
                    break;
                case role_t::VCR_H1:
                case role_t::VCR_H2:
                case role_t::VCR_H3:
                case role_t::VCR_H4:
                case role_t::VCR_H5:
                case role_t::VCR_H6:
                    line_style = fmt::emphasis::underline;
                    break;
                default:
                    break;
            }
            fmt::print(
                file,
                line_style,
                "{}",
                str.substr(attr.sa_range.lr_start, attr.sa_range.length()));
        }
        start = attr.sa_range.lr_end;
    }
    fmt::print(file, "{}\n", str.substr(start));
}

void
print(FILE* file, const user_message& um)
{
    println(file, um.to_attr_line().rtrim());
}

}  // namespace console
}  // namespace lnav
