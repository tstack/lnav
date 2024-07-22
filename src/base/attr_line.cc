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

#include <stdarg.h>

#include "ansi_scrubber.hh"
#include "auto_mem.hh"
#include "config.h"
#include "lnav_log.hh"
#include "pcrepp/pcre2pp.hh"

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
        scrub_ansi_string(this->al_string, &this->al_attrs);
    }

    return *this;
}

attr_line_t&
attr_line_t::with_ansi_string(const std::string& str)
{
    this->al_string = str;
    scrub_ansi_string(this->al_string, &this->al_attrs);

    return *this;
}

namespace text_stream {
struct word {
    string_fragment w_word;
    string_fragment w_remaining;
};

struct space {
    string_fragment s_value;
    string_fragment s_remaining;
};

struct corrupt {
    string_fragment c_value;
    string_fragment c_remaining;
};

struct eof {
    string_fragment e_remaining;
};

using chunk = mapbox::util::variant<word, space, corrupt, eof>;

chunk
consume(const string_fragment text)
{
    static const auto WORD_RE
        = lnav::pcre2pp::code::from_const(R"((*UTF)^[^\p{Z}\p{So}\p{C}]+)");
    static const auto SPACE_RE
        = lnav::pcre2pp::code::from_const(R"((*UTF)^\s)");

    if (text.empty()) {
        return eof{text};
    }

    auto word_find_res
        = WORD_RE.find_in(text, PCRE2_NO_UTF_CHECK).ignore_error();
    if (word_find_res) {
        auto split_res = text.split_n(word_find_res->f_all.length()).value();

        return word{split_res.first, split_res.second};
    }

    if (isspace(text.front())) {
        auto split_res = text.split_n(1).value();

        return space{split_res.first, split_res.second};
    }

    auto space_find_res
        = SPACE_RE.find_in(text, PCRE2_NO_UTF_CHECK).ignore_error();
    if (space_find_res) {
        auto split_res = text.split_n(space_find_res->f_all.length()).value();

        return space{split_res.first, split_res.second};
    }

    auto csize_res = ww898::utf::utf8::char_size(
        [&text]() { return std::make_pair(text.front(), text.length()); });

    if (csize_res.isErr()) {
        auto split_res = text.split_n(1);

        return corrupt{split_res->first, split_res->second};
    }

    auto split_res = text.split_n(csize_res.unwrap());

    return word{split_res->first, split_res->second};
}

}  // namespace text_stream

static void
split_attrs(attr_line_t& al, const line_range& lr)
{
    if (lr.empty()) {
        return;
    }

    string_attrs_t new_attrs;

    for (auto& attr : al.al_attrs) {
        if (!lr.intersects(attr.sa_range)) {
            continue;
        }

        new_attrs.emplace_back(line_range{lr.lr_end, attr.sa_range.lr_end},
                               std::make_pair(attr.sa_type, attr.sa_value));
        attr.sa_range.lr_end = lr.lr_start;
    }
    for (auto& new_attr : new_attrs) {
        al.al_attrs.emplace_back(std::move(new_attr));
    }
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

    for (const auto& sa : al.al_attrs) {
        this->al_attrs.emplace_back(sa);

        line_range& lr = this->al_attrs.back().sa_range;

        lr.shift(0, index);
        if (lr.lr_end == -1) {
            lr.lr_end = index + al.al_string.length();
        }
    }

    if (tws == nullptr) {
        return *this;
    }

    auto starting_line_index = this->al_string.rfind('\n', index);
    if (starting_line_index == std::string::npos) {
        starting_line_index = 0;
    } else {
        starting_line_index += 1;
    }

    const ssize_t usable_width = tws->tws_width - tws->tws_indent;

    auto text_to_wrap = string_fragment::from_str_range(
        this->al_string, starting_line_index, this->al_string.length());
    string_fragment last_word;
    ssize_t line_ch_count = 0;
    ssize_t line_indent_count = 0;
    auto needs_indent = false;
    auto last_was_pre = false;

    while (!text_to_wrap.empty()) {
        if (needs_indent) {
            this->insert(text_to_wrap.sf_begin,
                         tws->tws_indent + tws->tws_padding_indent,
                         ' ');
            auto indent_lr = line_range{
                text_to_wrap.sf_begin,
                text_to_wrap.sf_begin + tws->tws_indent,
            };
            split_attrs(*this, indent_lr);
            indent_lr.lr_end += tws->tws_padding_indent;
            line_ch_count += tws->tws_padding_indent;
            line_indent_count += tws->tws_padding_indent;
            if (!indent_lr.empty()) {
                this->al_attrs.emplace_back(indent_lr, SA_PREFORMATTED.value());
            }
            text_to_wrap = text_to_wrap.prepend(
                this->al_string.data(),
                tws->tws_indent + tws->tws_padding_indent);
            needs_indent = false;
        }

        text_stream::chunk next_chunk(mapbox::util::no_init{});
        auto pre_iter = find_string_attr_containing(
            this->al_attrs, &SA_PREFORMATTED, text_to_wrap.sf_begin);
        if (pre_iter != this->al_attrs.end()) {
            auto pre_len = pre_iter->sa_range.lr_end - text_to_wrap.sf_begin;
            auto pre_lf = text_to_wrap.find('\n');
            if (pre_lf && pre_lf.value() < pre_len) {
                pre_len = pre_lf.value() + 1;
            }

            auto pre_pair = text_to_wrap.split_n(pre_len);
            next_chunk = text_stream::word{
                pre_pair->first,
                pre_pair->second,
            };
        }
        if (!next_chunk.valid()) {
            next_chunk = text_stream::consume(text_to_wrap);
        }

        text_to_wrap = next_chunk.match(
            [&](text_stream::word word) {
                auto ch_count
                    = word.w_word.utf8_length().unwrapOr(word.w_word.length());

                if (line_ch_count > line_indent_count && !last_was_pre
                    && (line_ch_count + ch_count) > usable_width)
                {
                    this->insert(word.w_word.sf_begin, 1, '\n');
                    this->insert(word.w_word.sf_begin + 1,
                                 tws->tws_indent + tws->tws_padding_indent,
                                 ' ');
                    auto indent_lr = line_range{
                        word.w_word.sf_begin + 1,
                        word.w_word.sf_begin + 1 + tws->tws_indent,
                    };
                    split_attrs(*this, indent_lr);
                    indent_lr.lr_end += tws->tws_padding_indent;
                    if (!indent_lr.empty()) {
                        this->al_attrs.emplace_back(indent_lr,
                                                    SA_PREFORMATTED.value());
                    }
                    line_ch_count = tws->tws_padding_indent + ch_count;
                    line_indent_count = tws->tws_padding_indent;
                    auto trailing_space_count = 0;
                    if (!last_word.empty()) {
                        trailing_space_count
                            = word.w_word.sf_begin - last_word.sf_begin;
                        this->erase(last_word.sf_begin, trailing_space_count);
                    }
                    return word.w_remaining
                        .erase_before(this->al_string.data(),
                                      trailing_space_count)
                        .prepend(this->al_string.data(),
                                 1 + tws->tws_indent + tws->tws_padding_indent);
                }
                line_ch_count += ch_count;
                if (word.w_word.endswith("\n")) {
                    line_ch_count = 0;
                    line_indent_count = 0;
                    needs_indent = true;
                }

                return word.w_remaining;
            },
            [&](text_stream::space space) {
                if (space.s_value == "\n") {
                    line_ch_count = 0;
                    line_indent_count = 0;
                    needs_indent = true;
                    return space.s_remaining;
                }

                if (line_ch_count > 0) {
                    auto ch_count = space.s_value.utf8_length().unwrapOr(
                        space.s_value.length());

                    if ((line_ch_count + ch_count) > usable_width
                        && find_string_attr_containing(this->al_attrs,
                                                       &SA_PREFORMATTED,
                                                       text_to_wrap.sf_begin)
                            == this->al_attrs.end())
                    {
                        this->erase(space.s_value.sf_begin,
                                    space.s_value.length());
                        this->insert(space.s_value.sf_begin, "\n");
                        line_ch_count = 0;
                        line_indent_count = 0;
                        needs_indent = true;

                        auto trailing_space_count = 0;
                        if (!last_word.empty()) {
                            trailing_space_count
                                = space.s_value.sf_begin - last_word.sf_begin;
                            this->erase(last_word.sf_end, trailing_space_count);
                        }

                        return space.s_remaining
                            .erase_before(
                                this->al_string.data(),
                                space.s_value.length() + trailing_space_count)
                            .prepend(this->al_string.data(), 1);
                    }
                    line_ch_count += ch_count;
                } else if (find_string_attr_containing(this->al_attrs,
                                                       &SA_PREFORMATTED,
                                                       text_to_wrap.sf_begin)
                           == this->al_attrs.end())
                {
                    this->erase(space.s_value.sf_begin, space.s_value.length());
                    return space.s_remaining.erase_before(
                        this->al_string.data(), space.s_value.length());
                }

                return space.s_remaining;
            },
            [](text_stream::corrupt corrupt) { return corrupt.c_remaining; },
            [](text_stream::eof eof) { return eof.e_remaining; });

        if (next_chunk.is<text_stream::word>()) {
            last_word = text_to_wrap;
        }
        last_was_pre = (pre_iter != this->al_attrs.end());

        ensure(this->al_string.data() == text_to_wrap.sf_string);
        ensure(text_to_wrap.sf_begin <= text_to_wrap.sf_end);
    }
    return *this;
}

attr_line_t&
attr_line_t::wrap_with(text_wrap_settings* tws)
{
    attr_line_t tmp;

    tmp.al_string = std::move(this->al_string);
    tmp.al_attrs = std::move(this->al_attrs);

    this->append(tmp, tws);

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

        auto ilr = lr.intersection(sa.sa_range).shift(0, -lr.lr_start);
        retval.al_attrs.emplace_back(ilr,
                                     std::make_pair(sa.sa_type, sa.sa_value));
        const auto& last_lr = retval.al_attrs.back().sa_range;

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
            auto& lr = sattr.sa_range;

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
attr_line_t::rtrim(std::optional<const char*> chars)
{
    auto index = this->al_string.length();

    for (; index > 0; index--) {
        if (find_string_attr_containing(
                this->al_attrs, &SA_PREFORMATTED, index - 1)
            != this->al_attrs.end())
        {
            break;
        }
        if (chars
            && strchr(chars.value(), this->al_string[index - 1]) == nullptr)
        {
            break;
        }
        if (!chars && !isspace(this->al_string[index - 1])) {
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
    if (len == 0) {
        return *this;
    }

    this->al_string.erase(pos, len);

    shift_string_attrs(this->al_attrs,
                       line_range{(int) pos, (int) (pos + len)},
                       -((int32_t) len));
    auto new_end = std::remove_if(
        this->al_attrs.begin(), this->al_attrs.end(), [](const auto& attr) {
            return attr.sa_range.empty();
        });
    this->al_attrs.erase(new_end, this->al_attrs.end());

    return *this;
}

attr_line_t&
attr_line_t::pad_to(ssize_t size)
{
    const auto curr_len = this->column_width();

    if (curr_len < size) {
        this->append((size - curr_len), ' ');
        for (auto& attr : this->al_attrs) {
            if (attr.sa_range.lr_start == 0 && attr.sa_range.lr_end == curr_len)
            {
                attr.sa_range.lr_end = this->al_string.length();
            }
        }
    }

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
line_range::shift_range(const line_range& cover, int32_t amount)
{
    if (cover.lr_end <= this->lr_start) {
        this->lr_start = std::max(0, this->lr_start + amount);
        if (this->lr_end != -1) {
            this->lr_end = std::max(0, this->lr_end + amount);
        }
    } else {
        if (amount < 0 && cover.contains(*this)) {
            this->lr_start = cover.lr_start;
        }
        if (this->lr_end != -1) {
            if (cover.lr_start < this->lr_end) {
                if (amount < 0 && amount < (cover.lr_start - this->lr_end)) {
                    this->lr_end = cover.lr_start;
                } else {
                    this->lr_end
                        = std::max(this->lr_start, this->lr_end + amount);
                }
            }
        }
    }

    return *this;
}

line_range&
line_range::shift(int32_t start, int32_t amount)
{
    if (start == this->lr_start) {
        if (amount > 0) {
            this->lr_start += amount;
        }
        if (this->lr_end != -1) {
            this->lr_end += amount;
            if (this->lr_end < this->lr_start) {
                this->lr_end = this->lr_start;
            }
        }
    } else if (start < this->lr_start) {
        this->lr_start = std::max(0, this->lr_start + amount);
        if (this->lr_end != -1) {
            this->lr_end = std::max(0, this->lr_end + amount);
        }
    } else if (this->lr_end != -1) {
        if (start < this->lr_end) {
            if (amount < 0 && amount < (start - this->lr_end)) {
                this->lr_end = start;
            } else {
                this->lr_end = std::max(this->lr_start, this->lr_end + amount);
            }
        }
    }

    return *this;
}

string_attrs_t::const_iterator
find_string_attr(const string_attrs_t& sa, size_t near)
{
    auto nearest = sa.end();
    ssize_t last_diff = INT_MAX;

    for (auto iter = sa.begin(); iter != sa.end(); ++iter) {
        const auto& lr = iter->sa_range;

        if (!lr.is_valid() || !lr.contains(near)) {
            continue;
        }

        ssize_t diff = near - lr.lr_start;
        if (diff < last_diff) {
            last_diff = diff;
            nearest = iter;
        }
    }

    return nearest;
}

void
shift_string_attrs(string_attrs_t& sa, int32_t start, int32_t amount)
{
    for (auto& iter : sa) {
        iter.sa_range.shift(start, amount);
    }
}

void
shift_string_attrs(string_attrs_t& sa, const line_range& cover, int32_t amount)
{
    for (auto& iter : sa) {
        iter.sa_range.shift_range(cover, amount);
    }
}

struct line_range
find_string_attr_range(const string_attrs_t& sa, string_attr_type_base* type)
{
    auto iter = find_string_attr(sa, type);

    if (iter != sa.end()) {
        return iter->sa_range;
    }

    return line_range();
}

void
remove_string_attr(string_attrs_t& sa, const line_range& lr)
{
    string_attrs_t::iterator iter;

    while ((iter = find_string_attr(sa, lr)) != sa.end()) {
        sa.erase(iter);
    }
}

void
remove_string_attr(string_attrs_t& sa, string_attr_type_base* type)
{
    for (auto iter = sa.begin(); iter != sa.end();) {
        if (iter->sa_type == type) {
            iter = sa.erase(iter);
        } else {
            ++iter;
        }
    }
}

string_attrs_t::iterator
find_string_attr(string_attrs_t& sa, const line_range& lr)
{
    string_attrs_t::iterator iter;

    for (iter = sa.begin(); iter != sa.end(); ++iter) {
        if (lr.contains(iter->sa_range)) {
            break;
        }
    }

    return iter;
}

string_attrs_t::const_iterator
find_string_attr(const string_attrs_t& sa,
                 const string_attr_type_base* type,
                 int start)
{
    auto iter = sa.begin();
    for (; iter != sa.end(); ++iter) {
        if (iter->sa_type == type && iter->sa_range.lr_start >= start) {
            break;
        }
    }

    return iter;
}

std::optional<const string_attr*>
get_string_attr(const string_attrs_t& sa,
                const string_attr_type_base* type,
                int start)
{
    auto iter = find_string_attr(sa, type, start);

    if (iter == sa.end()) {
        return std::nullopt;
    }

    return std::make_optional(&(*iter));
}
