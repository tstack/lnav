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

#include <algorithm>
#include <iterator>
#include <regex>
#include <sstream>
#include <string_view>

#include "string_util.hh"

#include "config.h"
#include "is_utf8.hh"
#include "lnav_log.hh"
#include "scn/scan.h"

using namespace std::string_view_literals;

void
scrub_to_utf8(char* buffer, size_t length)
{
    while (true) {
        auto frag = string_fragment::from_bytes(buffer, length);
        auto scan_res = is_utf8(frag);

        if (scan_res.is_valid()) {
            break;
        }
        for (size_t lpc = 0; lpc < scan_res.usr_faulty_bytes; lpc++) {
            buffer[scan_res.usr_valid_frag.sf_end + lpc] = '?';
        }
    }
}

void
quote_content(auto_buffer& buf, const string_fragment& sf, char quote_char)
{
    for (char ch : sf) {
        if (ch == quote_char) {
            buf.push_back('\\').push_back(ch);
            continue;
        }
        switch (ch) {
            case '\\':
                buf.push_back('\\').push_back('\\');
                break;
            case '\n':
                buf.push_back('\\').push_back('n');
                break;
            case '\t':
                buf.push_back('\\').push_back('t');
                break;
            case '\r':
                buf.push_back('\\').push_back('r');
                break;
            case '\a':
                buf.push_back('\\').push_back('a');
                break;
            case '\b':
                buf.push_back('\\').push_back('b');
                break;
            default:
                buf.push_back(ch);
                break;
        }
    }
}

size_t
unquote_content(char* dst, const char* str, size_t len, char quote_char)
{
    size_t index = 0;

    for (size_t lpc = 0; lpc < len; lpc++, index++) {
        dst[index] = str[lpc];
        if (str[lpc] == quote_char) {
            lpc += 1;
        } else if (str[lpc] == '\\' && (lpc + 1) < len) {
            switch (str[lpc + 1]) {
                case 'n':
                    dst[index] = '\n';
                    break;
                case 'r':
                    dst[index] = '\r';
                    break;
                case 't':
                    dst[index] = '\t';
                    break;
                default:
                    dst[index] = str[lpc + 1];
                    break;
            }
            lpc += 1;
        }
    }
    dst[index] = '\0';

    return index;
}

size_t
unquote(char* dst, const char* str, size_t len)
{
    if (str[0] == 'f' || str[0] == 'r' || str[0] == 'u' || str[0] == 'R') {
        str += 1;
        len -= 1;
    }
    char quote_char = str[0];

    require(str[0] == '\'' || str[0] == '"');

    return unquote_content(dst, &str[1], len - 2, quote_char);
}

size_t
unquote_w3c(char* dst, const char* str, size_t len)
{
    size_t index = 0;

    require(str[0] == '\'' || str[0] == '"');

    for (size_t lpc = 1; lpc < (len - 1); lpc++, index++) {
        dst[index] = str[lpc];
        if (str[lpc] == '"') {
            lpc += 1;
        }
    }
    dst[index] = '\0';

    return index;
}

void
truncate_to(std::string& str, size_t max_char_len)
{
    static const std::string ELLIPSIS = "\u22ef";

    if (str.length() < max_char_len) {
        return;
    }

    auto str_char_len_res = utf8_string_length(str);

    if (str_char_len_res.isErr()) {
        // XXX
        return;
    }

    auto str_char_len = str_char_len_res.unwrap();
    if (str_char_len <= max_char_len) {
        return;
    }

    if (max_char_len < 3) {
        str = ELLIPSIS;
        return;
    }

    auto chars_to_remove = (str_char_len - max_char_len) + 1;
    auto midpoint = str_char_len / 2;
    auto chars_to_keep_at_front = midpoint - (chars_to_remove / 2);
    auto bytes_to_keep_at_front
        = utf8_char_to_byte_index(str, chars_to_keep_at_front);
    auto remove_up_to_bytes = utf8_char_to_byte_index(
        str, chars_to_keep_at_front + chars_to_remove);
    auto bytes_to_remove = remove_up_to_bytes - bytes_to_keep_at_front;
    str.erase(bytes_to_keep_at_front, bytes_to_remove);
    str.insert(bytes_to_keep_at_front, ELLIPSIS);
}

ssize_t
utf8_char_to_byte_index(const std::string& str, ssize_t ch_index)
{
    ssize_t retval = 0;

    while (ch_index > 0) {
        auto ch_len
            = ww898::utf::utf8::char_size([&str, retval]() {
                  return std::make_pair(str[retval], str.length() - retval - 1);
              }).unwrapOr(1);

        retval += ch_len;
        ch_index -= 1;
    }

    return retval;
}
bool
is_url(const std::string& fn)
{
    static const auto url_re = std::regex("^(file|https?|ftps?|scp|sftp):.*");

    return std::regex_match(fn, url_re);
}

size_t
last_word_str(char* str, size_t len, size_t max_len)
{
    if (len < max_len) {
        return len;
    }

    size_t last_start = 0;

    for (size_t index = 0; index < len; index++) {
        switch (str[index]) {
            case '.':
            case '-':
            case '/':
            case ':':
                last_start = index + 1;
                break;
        }
    }

    if (last_start == 0) {
        return len;
    }

    memmove(&str[0], &str[last_start], len - last_start);
    return len - last_start;
}

size_t
abbreviate_str(char* str, size_t len, size_t max_len)
{
    size_t last_start = 1;

    if (len < max_len) {
        return len;
    }

    for (size_t index = 0; index < len; index++) {
        switch (str[index]) {
            case '.':
            case '-':
            case '/':
            case ':':
                memmove(&str[last_start], &str[index], len - index);
                len -= (index - last_start);
                index = last_start + 1;
                last_start = index + 1;

                if (len < max_len) {
                    return len;
                }
                break;
        }
    }

    return len;
}

void
split_ws(const std::string& str, std::vector<std::string>& toks_out)
{
    auto str_sf = string_fragment::from_str(str);

    while (true) {
        auto split_pair = str_sf.split_when(isspace);
        if (split_pair.first.empty()) {
            if (split_pair.second.empty()) {
                break;
            }
            str_sf = split_pair.second;
            continue;
        }

        toks_out.emplace_back(split_pair.first.to_string());
        str_sf = split_pair.second;
    }
}

std::string
repeat(const std::string& input, size_t num)
{
    std::ostringstream os;
    std::fill_n(std::ostream_iterator<std::string>(os), num, input);
    return os.str();
}

std::string
center_str(const std::string& subject, size_t width)
{
    std::string retval = subject;

    truncate_to(retval, width);

    auto retval_length = utf8_string_length(retval).unwrapOr(retval.length());
    auto total_fill = width - retval_length;
    auto before = total_fill / 2;
    auto after = total_fill - before;

    retval.insert(0, before, ' ');
    retval.append(after, ' ');

    return retval;
}

bool
is_blank(const std::string& str)
{
    return std::all_of(
        str.begin(), str.end(), [](const auto ch) { return isspace(ch); });
}

std::string
scrub_ws(const char* in, ssize_t len)
{
    static constexpr auto TAB_SYMBOL = "\u21e5"sv;
    static constexpr auto LF_SYMBOL = "\u240a"sv;
    static constexpr auto CR_SYMBOL = "\u240d"sv;

    std::string retval;

    if (len > 0) {
        retval.reserve(len);
    }

    for (size_t lpc = 0; (len == -1 && in[lpc]) || (len >= 0 && lpc < len);
         lpc++)
    {
        auto ch = in[lpc];

        switch (ch) {
            case '\t':
                retval.append(TAB_SYMBOL);
                break;
            case '\n':
                retval.append(LF_SYMBOL);
                break;
            case '\r':
                retval.append(CR_SYMBOL);
                break;
            default:
                retval.push_back(ch);
                break;
        }
    }

    return retval;
}

static constexpr const char* const SUPERSCRIPT_NUMS[] = {
    "⁰",
    "¹",
    "²",
    "³",
    "⁴",
    "⁵",
    "⁶",
    "⁷",
    "⁸",
    "⁹",
};

std::string
to_superscript(const std::string& in)
{
    std::string retval;
    for (const auto ch : in) {
        if (isdigit(ch)) {
            auto index = ch - '0';

            retval.append(SUPERSCRIPT_NUMS[index]);
        } else {
            retval.push_back(ch);
        }
    }

    return retval;
}

namespace fmt {
auto
formatter<lnav::tainted_string>::format(const lnav::tainted_string& ts,
                                        format_context& ctx)
    -> decltype(ctx.out()) const
{
    auto esc_res = fmt::v10::detail::find_escape(&(*ts.ts_str.begin()),
                                                 &(*ts.ts_str.end()));
    if (esc_res.end == nullptr) {
        return formatter<string_view>::format(ts.ts_str, ctx);
    }

    return format_to(ctx.out(), FMT_STRING("{:?}"), ts.ts_str);
}
}  // namespace fmt

namespace lnav::pcre2pp {

static bool
is_meta(char ch)
{
    switch (ch) {
        case '\\':
        case '^':
        case '$':
        case '.':
        case '[':
        case ']':
        case '(':
        case ')':
        case '*':
        case '+':
        case '?':
        case '{':
        case '}':
            return true;
        default:
            return false;
    }
}

static std::optional<const char*>
char_escape_seq(char ch)
{
    switch (ch) {
        case '\t':
            return "\\t";
        case '\n':
            return "\\n";
    }

    return std::nullopt;
}

std::string
quote(string_fragment str)
{
    std::string retval;

    while (true) {
        auto cp_pair_opt = str.consume_codepoint();
        if (!cp_pair_opt) {
            break;
        }

        auto cp_pair = cp_pair_opt.value();
        if ((cp_pair.first & ~0xff) == 0) {
            if (is_meta(cp_pair.first)) {
                retval.push_back('\\');
            } else {
                auto esc_seq = char_escape_seq(cp_pair.first);
                if (esc_seq) {
                    retval.append(esc_seq.value());
                    str = cp_pair_opt->second;
                    continue;
                }
            }
        }
        ww898::utf::utf8::write(cp_pair.first,
                                [&retval](char ch) { retval.push_back(ch); });
        str = cp_pair_opt->second;
    }

    return retval;
}

}  // namespace lnav::pcre2pp
