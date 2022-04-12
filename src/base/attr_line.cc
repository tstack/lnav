/**
 * Copyright (c) 2020, Timothy Stack
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

#include "attr_line.hh"

#include "ansi_scrubber.hh"
#include "auto_mem.hh"
#include "config.h"
#include "lnav_log.hh"

attr_line_t&
attr_line_t::with_ansi_string(const char* str, ...)
{
    auto_mem<char> formatted_str;
    va_list args;

    va_start(args, str);
    auto ret = vasprintf(formatted_str.out(), str, args);
    va_end(args);

    if (ret >= 0 && formatted_str != nullptr) {
        this->al_string = formatted_str;
        scrub_ansi_string(this->al_string, this->al_attrs);
    }

    return *this;
}

attr_line_t&
attr_line_t::with_ansi_string(const std::string& str)
{
    this->al_string = str;
    scrub_ansi_string(this->al_string, this->al_attrs);

    return *this;
}

attr_line_t&
attr_line_t::insert(size_t index,
                    const attr_line_t& al,
                    text_wrap_settings* tws)
{
    if (index < this->al_string.length()) {
        shift_string_attrs(this->al_attrs, index, al.al_string.length());
    }

    this->al_string.insert(index, al.al_string);

    for (auto& sa : al.al_attrs) {
        this->al_attrs.emplace_back(sa);

        line_range& lr = this->al_attrs.back().sa_range;

        lr.shift(0, index);
        if (lr.lr_end == -1) {
            lr.lr_end = index + al.al_string.length();
        }
    }

    if (tws != nullptr && (int) this->al_string.length() > tws->tws_width) {
        ssize_t start_pos = index;
        ssize_t line_start = this->al_string.rfind('\n', start_pos);

        if (line_start == (ssize_t) std::string::npos) {
            line_start = 0;
        } else {
            line_start += 1;
        }

        ssize_t line_len = index - line_start;
        ssize_t usable_width = tws->tws_width - tws->tws_indent;
        ssize_t avail
            = std::max((ssize_t) 0, (ssize_t) tws->tws_width - line_len);

        if (avail == 0) {
            avail = INT_MAX;
        }

        while (start_pos < (int) this->al_string.length()) {
            ssize_t lpc;

            // Find the end of a word or a breakpoint.
            for (lpc = start_pos; lpc < (int) this->al_string.length()
                 && (isalnum(this->al_string[lpc])
                     || this->al_string[lpc] == ','
                     || this->al_string[lpc] == '_'
                     || this->al_string[lpc] == '.'
                     || this->al_string[lpc] == ';');
                 lpc++)
            {
                if (this->al_string[lpc] == '-' || this->al_string[lpc] == '.')
                {
                    lpc += 1;
                    break;
                }
            }

            if ((avail != usable_width) && (lpc - start_pos > avail)) {
                // Need to wrap the word.  Do the wrap.
                this->insert(start_pos, 1, '\n')
                    .insert(start_pos + 1, tws->tws_indent, ' ');
                start_pos += 1 + tws->tws_indent;
                avail = tws->tws_width - tws->tws_indent;
            } else {
                // There's still room to add stuff.
                avail -= (lpc - start_pos);
                while (lpc < (int) this->al_string.length() && avail) {
                    if (this->al_string[lpc] == '\n') {
                        this->insert(lpc + 1, tws->tws_indent, ' ');
                        avail = usable_width;
                        lpc += 1 + tws->tws_indent;
                        break;
                    }
                    if (isalnum(this->al_string[lpc])
                        || this->al_string[lpc] == '_') {
                        break;
                    }
                    avail -= 1;
                    lpc += 1;
                }
                start_pos = lpc;
                if (!avail) {
                    this->insert(start_pos, 1, '\n')
                        .insert(start_pos + 1, tws->tws_indent, ' ');
                    start_pos += 1 + tws->tws_indent;
                    avail = usable_width;

                    for (lpc = start_pos; lpc < (int) this->al_string.length()
                         && this->al_string[lpc] == ' ';
                         lpc++)
                    {
                    }

                    if (lpc != start_pos) {
                        this->erase(start_pos, (lpc - start_pos));
                    }
                }
            }
        }
    }

    return *this;
}

attr_line_t
attr_line_t::subline(size_t start, size_t len) const
{
    if (len == std::string::npos) {
        len = this->al_string.length() - start;
    }

    line_range lr{(int) start, (int) (start + len)};
    attr_line_t retval;

    retval.al_string = this->al_string.substr(start, len);
    for (const auto& sa : this->al_attrs) {
        if (!lr.intersects(sa.sa_range)) {
            continue;
        }

        retval.al_attrs.emplace_back(
            lr.intersection(sa.sa_range).shift(lr.lr_start, -lr.lr_start),
            std::make_pair(sa.sa_type, sa.sa_value));

        line_range& last_lr = retval.al_attrs.back().sa_range;

        ensure(last_lr.lr_end <= (int) retval.al_string.length());
    }

    return retval;
}

void
attr_line_t::split_lines(std::vector<attr_line_t>& lines) const
{
    size_t pos = 0, next_line;

    while ((next_line = this->al_string.find('\n', pos)) != std::string::npos) {
        lines.emplace_back(this->subline(pos, next_line - pos));
        pos = next_line + 1;
    }
    lines.emplace_back(this->subline(pos));
}

attr_line_t&
attr_line_t::right_justify(unsigned long width)
{
    long padding = width - this->length();
    if (padding > 0) {
        this->al_string.insert(0, padding, ' ');
        for (auto& al_attr : this->al_attrs) {
            if (al_attr.sa_range.lr_start > 0) {
                al_attr.sa_range.lr_start += padding;
            }
            if (al_attr.sa_range.lr_end != -1) {
                al_attr.sa_range.lr_end += padding;
            }
        }
    }

    return *this;
}

size_t
attr_line_t::nearest_text(size_t x) const
{
    if (x > 0 && x >= (size_t) this->length()) {
        if (this->empty()) {
            x = 0;
        } else {
            x = this->length() - 1;
        }
    }

    while (x > 0 && isspace(this->al_string[x])) {
        x -= 1;
    }

    return x;
}

void
attr_line_t::apply_hide()
{
    auto& sa = this->al_attrs;

    for (auto& sattr : sa) {
        if (sattr.sa_type == &SA_HIDDEN && sattr.sa_range.length() > 3) {
            struct line_range& lr = sattr.sa_range;

            std::for_each(sa.begin(), sa.end(), [&](string_attr& attr) {
                if (attr.sa_type == &VC_STYLE && lr.contains(attr.sa_range)) {
                    attr.sa_type = &SA_REMOVED;
                }
            });

            this->al_string.replace(lr.lr_start, lr.length(), "\xE2\x8B\xAE");
            shift_string_attrs(sa, lr.lr_start + 1, -(lr.length() - 3));
            sattr.sa_type = &VC_ROLE;
            sattr.sa_value = role_t::VCR_HIDDEN;
            lr.lr_end = lr.lr_start + 3;
        }
    }
}

attr_line_t&
attr_line_t::rtrim()
{
    auto index = this->al_string.length();

    for (; index > 0; index--) {
        if (!isspace(this->al_string[index - 1])) {
            break;
        }
    }
    if (index > 0 && index < this->al_string.length()) {
        this->erase(index);
    }
    return *this;
}

attr_line_t&
attr_line_t::erase(size_t pos, size_t len)
{
    if (len == std::string::npos) {
        len = this->al_string.size() - pos;
    }
    this->al_string.erase(pos, len);

    shift_string_attrs(this->al_attrs, pos, -((int32_t) len));

    return *this;
}

line_range
line_range::intersection(const line_range& other) const
{
    int actual_end;

    if (this->lr_end == -1) {
        actual_end = other.lr_end;
    } else if (other.lr_end == -1) {
        actual_end = this->lr_end;
    } else {
        actual_end = std::min(this->lr_end, other.lr_end);
    }
    return line_range{std::max(this->lr_start, other.lr_start), actual_end};
}

line_range&
line_range::shift(int32_t start, int32_t amount)
{
    if (start <= this->lr_start) {
        this->lr_start = std::max(0, this->lr_start + amount);
        if (this->lr_end != -1) {
            this->lr_end = std::max(0, this->lr_end + amount);
        }
    } else if (this->lr_end != -1) {
        if (start < this->lr_end) {
            this->lr_end = std::max(this->lr_start, this->lr_end + amount);
        }
    }

    return *this;
}
