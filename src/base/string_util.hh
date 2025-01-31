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

#ifndef lnav_string_util_hh
#define lnav_string_util_hh

#include <string>
#include <vector>

#include <string.h>

#include "auto_mem.hh"
#include "intern_string.hh"
#include "ww898/cp_utf8.hpp"

void scrub_to_utf8(char* buffer, size_t length);

inline bool
is_line_ending(char ch)
{
    return ch == '\r' || ch == '\n';
}

void quote_content(auto_buffer& buf,
                   const string_fragment& sf,
                   char quote_char);

size_t unquote_content(char* dst, const char* str, size_t len, char quote_char);

size_t unquote(char* dst, const char* str, size_t len);

size_t unquote_w3c(char* dst, const char* str, size_t len);

inline bool
startswith(const char* str, const char* prefix)
{
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

inline bool
startswith(const std::string& str, const char* prefix)
{
    return startswith(str.c_str(), prefix);
}

inline bool
startswith(const std::string& str, const std::string& prefix)
{
    return startswith(str.c_str(), prefix.c_str());
}

inline bool
endswith(const char* str, const char* suffix)
{
    size_t len = strlen(str), suffix_len = strlen(suffix);

    if (suffix_len > len) {
        return false;
    }

    return strcmp(&str[len - suffix_len], suffix) == 0;
}

template<int N>
bool
endswith(const std::string& str, const char (&suffix)[N])
{
    if (N - 1 > str.length()) {
        return false;
    }

    return strcmp(&str[str.size() - (N - 1)], suffix) == 0;
}

void truncate_to(std::string& str, size_t max_char_len);

std::string scrub_ws(const char* in, ssize_t len = -1);
inline std::string
scrub_ws(const string_fragment& sf)
{
    return scrub_ws(sf.data(), sf.length());
}

inline std::string
trim(const std::string& str)
{
    std::string::size_type start, end;

    for (start = 0; start < str.size() && isspace(str[start]); start++)
        ;
    for (end = str.size(); end > 0 && isspace(str[end - 1]); end--)
        ;

    return str.substr(start, end - start);
}

inline const char*
ltrim(const char* str)
{
    while (isspace(*str)) {
        str += 1;
    }

    return str;
}

inline std::string
rtrim(const std::string& str)
{
    std::string::size_type end;

    for (end = str.size(); end > 0 && isspace(str[end - 1]); end--)
        ;

    return str.substr(0, end);
}

inline std::string
tolower(const char* str)
{
    std::string retval;

    for (int lpc = 0; str[lpc]; lpc++) {
        retval.push_back(::tolower(str[lpc]));
    }

    return retval;
}

inline std::string
tolower(const std::string& str)
{
    return tolower(str.c_str());
}

inline std::string
toupper(const char* str)
{
    std::string retval;

    for (int lpc = 0; str[lpc]; lpc++) {
        retval.push_back(::toupper(str[lpc]));
    }

    return retval;
}

inline std::string
toupper(const std::string& str)
{
    return toupper(str.c_str());
}

ssize_t utf8_char_to_byte_index(const std::string& str, ssize_t ch_index);

inline Result<size_t, const char*>
utf8_string_length(const char* str, ssize_t len = -1)
{
    size_t retval = 0;

    if (len == -1) {
        len = strlen(str);
    }

    for (ssize_t byte_index = 0; byte_index < len;) {
        auto ch_size
            = TRY(ww898::utf::utf8::char_size([str, len, byte_index]() {
                  return std::make_pair(str[byte_index], len - byte_index);
              }));
        byte_index += ch_size;
        retval += 1;
    }

    return Ok(retval);
}

inline Result<size_t, const char*>
utf8_string_length(const std::string& str)
{
    return utf8_string_length(str.c_str(), str.length());
}

bool is_url(const std::string& fn);

bool is_blank(const std::string& str);

size_t abbreviate_str(char* str, size_t len, size_t max_len);

size_t last_word_str(char* str, size_t len, size_t max_len);

void split_ws(const std::string& str, std::vector<std::string>& toks_out);

std::string repeat(const std::string& input, size_t num);

std::string center_str(const std::string& subject, size_t width);

inline std::string
on_blank(const std::string& str, const std::string& def)
{
    if (is_blank(str)) {
        return def;
    }

    return str;
}

std::string to_superscript(const std::string& in);

template<typename T, std::enable_if_t<std::is_integral_v<T>, bool> = true>
std::string
to_superscript(T in)
{
    return to_superscript(fmt::to_string(in));
}

namespace lnav {
class tainted_string {
public:
    explicit tainted_string(std::string s) : ts_str(std::move(s)) {}

    bool operator==(const tainted_string& other) const
    {
        return this->ts_str == other.ts_str;
    }

    bool operator!=(const tainted_string& other) const
    {
        return this->ts_str != other.ts_str;
    }

    bool operator<(const tainted_string& other) const
    {
        return this->ts_str < other.ts_str;
    }

    bool empty() const { return this->ts_str.empty(); }

    size_t length() const { return this->ts_str.length(); }

    size_t size() const { return this->ts_str.size(); }

    friend fmt::formatter<lnav::tainted_string>;

private:
    const std::string ts_str;
};
}  // namespace lnav

namespace fmt {
template<>
struct formatter<lnav::tainted_string> : formatter<string_view> {
    auto format(const lnav::tainted_string& ts, format_context& ctx)
        -> decltype(ctx.out()) const;
};
}  // namespace fmt

namespace lnav::pcre2pp {

std::string quote(string_fragment sf);

}

enum class text_align_t {
    start,
    center,
    end,
};

#endif
