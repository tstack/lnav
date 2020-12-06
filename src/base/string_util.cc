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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <regex>
#include <sstream>

#include "lnav_log.hh"
#include "is_utf8.hh"
#include "string_util.hh"

void scrub_to_utf8(char *buffer, size_t length)
{
    const char *msg;
    int faulty_bytes;

    while (true) {
        ssize_t utf8_end = is_utf8(
            (unsigned char *) buffer, length, &msg, &faulty_bytes);

        if (msg == nullptr) {
            break;
        }
        for (int lpc = 0; lpc < faulty_bytes; lpc++) {
            buffer[utf8_end + lpc] = '?';
        }
    }
}

size_t unquote(char *dst, const char *str, size_t len)
{
    if (str[0] == 'r' || str[0] == 'u') {
        str += 1;
        len -= 1;
    }
    char quote_char = str[0];
    size_t index = 0;

    require(str[0] == '\'' || str[0] == '"');

    for (size_t lpc = 1; lpc < (len - 1); lpc++, index++) {
        dst[index] = str[lpc];
        if (str[lpc] == quote_char) {
            lpc += 1;
        }
        else if (str[lpc] == '\\' && (lpc + 1) < len) {
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

size_t unquote_w3c(char *dst, const char *str, size_t len)
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

void truncate_to(std::string &str, size_t len)
{
    static const std::string ELLIPSIS = "\xE2\x8B\xAF";

    if (str.length() <= len) {
        return;
    }

    size_t half_width = str.size() / 2 - 1;
    str.erase(half_width, str.length() - (half_width * 2));
    str.insert(half_width, ELLIPSIS);
}

bool is_url(const char *fn)
{
    static auto url_re = std::regex("^(file|https?|ftps?|scp|sftp):.*");

    return std::regex_match(fn, url_re);
}

size_t abbreviate_str(char *str, size_t len, size_t max_len)
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

void split_ws(const std::string &str, std::vector<std::string> &toks_out)
{
    std::stringstream ss(str);
    std::string buf;

    while (ss >> buf) {
        toks_out.push_back(buf);
    }
}
