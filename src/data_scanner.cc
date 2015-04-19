/**
 * Copyright (c) 2007-2012, Timothy Stack
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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "pcrepp.hh"
#include "data_scanner.hh"

using namespace std;

static struct {
    const char *name;
    pcrepp      pcre;
} MATCHERS[DT_TERMINAL_MAX] = {
    { "quot",    pcrepp("\\A(?:(?:u|r)?\"((?:\\\\.|[^\"])+)\"|"
                        "(?:u|r)?'((?:\\\\.|[^'])+)')"), },
    { "url",     pcrepp("\\A([\\w]+://[^\\s'\"\\[\\](){}]+[/a-zA-Z0-9\\-=&])"),
    },
    { "path",    pcrepp("\\A((?:/|\\./|\\.\\./)[\\w\\.\\-_\\~/]*)"),
    },
    { "mac",     pcrepp(
          "\\A([0-9a-fA-F][0-9a-fA-F](?::[0-9a-fA-F][0-9a-fA-F]){5})(?!:)"), },
    { "date",
                 pcrepp("\\A("
                    "\\d{4}/\\d{1,2}/\\d{1,2}|"
                    "\\d{4}-\\d{1,2}-\\d{1,2}|"
                    "\\d{2}/\\w{3}/\\d{4}"
                    ")T?"), },
    { "time",    pcrepp(
          "\\A([\\s\\d]\\d:\\d\\d(?:(?!:\\d)|:\\d\\d(?:[\\.,]\\d{3,6})?Z?))\\b"), },
    /* { "qual", pcrepp("\\A([^\\s:=]+:[^\\s:=,]+(?!,)(?::[^\\s:=,]+)*)"), }, */
    { "ipv6",    pcrepp("\\A(::|[:\\da-fA-F\\.]+[a-fA-F\\d](?:%\\w+)?)"), },
    { "hexd",    pcrepp(
          "\\A([0-9a-fA-F][0-9a-fA-F](?::[0-9a-fA-F][0-9a-fA-F])+)"), },

    { "xmlt",   pcrepp(
            "\\A(<\\??[\\w:]+\\s*(?:[\\w:]+(?:\\s*=\\s*"
                    "(?:\"((?:\\\\.|[^\"])+)\"|'((?:\\\\.|[^'])+)'|[^>]+)"
                    "))*\\s*(?:/|\\?)>)"), },
    { "xmlo",   pcrepp(
            "\\A(<[\\w:]+\\s*(?:[\\w:]+(?:\\s*=\\s*"
                    "(?:\"((?:\\\\.|[^\"])+)\"|'((?:\\\\.|[^'])+)'|[^>]+)"
                    "))*\\s*>)"), },

    { "xmlc",   pcrepp("\\A(</[\\w:]+\\s*>)"), },

    { "coln",    pcrepp("\\A(:)"),
    },
    { "eq",      pcrepp("\\A(=)"),
    },
    { "comm",    pcrepp("\\A(,)"),
    },
    { "semi",    pcrepp("\\A(;)"),
    },

    { "lcurly",  pcrepp("\\A({)"),
    },
    { "rcurly",  pcrepp("\\A(})"),
    },

    { "lsquare", pcrepp("\\A(\\[)"),
    },
    { "rsquare", pcrepp("\\A(\\])"),
    },

    { "lparen",  pcrepp("\\A(\\()"),
    },
    { "rparen",  pcrepp("\\A(\\))"),
    },

    { "langle",  pcrepp("\\A(\\<)"),
    },
    { "rangle",  pcrepp("\\A(\\>)"),
    },

    { "ipv4", pcrepp("\\A("
              "(?:(?:25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])\\.){3}"
              "(?:25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(?![\\d]))"),
    },

    { "uuid",    pcrepp(
          "\\A([0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12})"), },

    { "vers",    pcrepp(
        "\\A("
            "[0-9]+(?:\\.[0-9]+\\w*){2,}(?:-\\w+)?|"
            "[0-9]+(?:\\.[0-9]+\\w*)+(?<!\\d[eE])-\\w+?"
        ")\\b"),
    },
    { "oct",     pcrepp("\\A(-?0[0-7]+\\b)"),
    },
    { "pcnt",    pcrepp("\\A(-?[0-9]+(\\.[0-9]+)?[ ]*%\\b)"),
    },
    { "num",     pcrepp("\\A(-?[0-9]+(\\.[0-9]+)?([eE][\\-+][0-9]+)?)"
                        "\\b(?![\\._\\-][a-zA-Z])"), },
    { "hex",     pcrepp("\\A(-?(?:0x|[0-9])[0-9a-fA-F]+)"
                        "\\b(?![\\._\\-][a-zA-Z])"), },

    { "mail",    pcrepp(
          "\\A([a-zA-Z0-9\\._%+-]+@[a-zA-Z0-9\\.-]+\\.[a-zA-Z]+)\\b"), },
    { "cnst",
      pcrepp("\\A(true|True|TRUE|false|False|FALSE|None|null)\\b") },
    { "word",    pcrepp(
          "\\A([a-zA-Z][a-z']+(?=[\\s\\(\\)!\\*:;'\\\"\\?,]|[\\.\\!,\\?]\\s|$))"),
    },
    { "sym",     pcrepp(
          "\\A([^\";\\s:=,\\(\\)\\{\\}\\[\\]\\+#!@%\\^&\\*'\\?<>\\~`\\|\\\\]+"
          "(?:::[^\";\\s:=,\\(\\)\\{\\}\\[\\]\\+#!@%\\^&\\*'\\?<>\\~`\\|\\\\]+)*)"),
    },
    { "line",    pcrepp("\\A(\r?\n|\r|;)"),
    },
    { "wspc",    pcrepp("\\A([ \\r\\t\\n]+)"),
    },
    { "dot",     pcrepp("\\A(\\.)"),
    },

    { "gbg",     pcrepp("\\A(.)"),
    },
};

const char *DNT_NAMES[DNT_MAX - DNT_KEY] = {
    "key",
    "pair",
    "val",
    "row",
    "unit",
    "meas",
    "var",
    "rang",
    "dt",
    "grp",
};

const char *data_scanner::token2name(data_token_t token)
{
    if (token < 0) {
        return "inv";
    }
    else if (token < DT_TERMINAL_MAX) {
        return MATCHERS[token].name;
    }
    else if (token == DT_ANY) {
        return "any";
    }
    else{
        return DNT_NAMES[token - DNT_KEY];
    }
}

static
bool find_string_end(const char *str, size_t &start, size_t length, char term)
{
    for (; start < length; start++) {
        if (str[start] == term) {
            start += 1;
            return true;
        }
        if (str[start] == '\\') {
            if (start + 1 >= length) {
                return false;
            }
            start += 1;
        }
    }
    return false;
}

static
void single_char_capture(pcre_context &pc, pcre_input &pi)
{
    pc.all()[0].c_begin = pi.pi_offset;
    pc.all()[0].c_end   = pi.pi_offset + 1;
    pc.all()[1]         = pc.all()[0];
    pc.set_count(2);
    pi.pi_next_offset = pi.pi_offset + 1;
}

bool data_scanner::tokenize(pcre_context &pc, data_token_t &token_out)
{
    const char *str = this->ds_pcre_input.get_string();
    pcre_input &pi  = this->ds_pcre_input;
    int         lpc;

    token_out = data_token_t(-1);

    if (this->ds_pcre_input.pi_next_offset > this->ds_pcre_input.pi_length) {
        return false;
    }
    else if (this->ds_pcre_input.pi_next_offset ==
             this->ds_pcre_input.pi_length) {
        this->ds_pcre_input.pi_next_offset += 1;
        token_out = DT_LINE;
        return false;
    }

    for (lpc = 0; lpc < DT_TERMINAL_MAX; lpc++) {
        switch (lpc) {
        case DT_QUOTED_STRING: {
            pcre_input &pi  = this->ds_pcre_input;
            const char *str = pi.get_string();
            size_t      str_start, str_end;
            bool        found = false;


            pi.pi_offset = pi.pi_next_offset;
            str_end      = str_start = pi.pi_offset + 1;
            switch (str[pi.pi_offset]) {
            case 'u':
            case 'r':
                if (pi.pi_offset + 1 < pi.pi_length &&
                    (str[pi.pi_offset + 1] == '\'' ||
                     str[pi.pi_offset + 1] == '\"')) {
                    str_start += 1;
                    str_end   += 1;
                    found      = find_string_end(str,
                                                 str_end,
                                                 pi.pi_length,
                                                 str[pi.pi_offset + 1]);
                }
                break;

            case '\'':
            case '\"':
                found = find_string_end(str,
                                        str_end,
                                        pi.pi_length,
                                        str[pi.pi_offset]);
                break;
            }
            if (found) {
                token_out           = data_token_t(DT_QUOTED_STRING);
                pi.pi_next_offset   = str_end;
                pc.all()[0].c_begin = pi.pi_offset;
                pc.all()[0].c_end   = str_end;
                pc.all()[1].c_begin = str_start;
                pc.all()[1].c_end   = str_end - 1;
                pc.set_count(2);
                return true;
            }
        }
        break;

        case DT_COLON: {
            pi.pi_offset = pi.pi_next_offset;

            if (str[pi.pi_offset] == ':') {
                token_out = data_token_t(DT_COLON);
                single_char_capture(pc, pi);
                return true;
            }
        }
        break;

        case DT_EQUALS: {
            pi.pi_offset = pi.pi_next_offset;

            if (str[pi.pi_offset] == '=') {
                token_out = data_token_t(DT_EQUALS);
                single_char_capture(pc, pi);
                return true;
            }
        }
        break;

        case DT_COMMA: {
            pi.pi_offset = pi.pi_next_offset;

            if (str[pi.pi_offset] == ',') {
                token_out = data_token_t(DT_COMMA);
                single_char_capture(pc, pi);
                return true;
            }
        }
        break;

        case DT_SEMI: {
            pi.pi_offset = pi.pi_next_offset;

            if (str[pi.pi_offset] == ';') {
                token_out = data_token_t(DT_SEMI);
                single_char_capture(pc, pi);
                return true;
            }
        }
        break;

        default:
            if (MATCHERS[lpc].pcre.match(pc, this->ds_pcre_input,
                                         PCRE_ANCHORED)) {
                switch (lpc) {
                case DT_IPV6_ADDRESS:
                    if (pc.all()->length() <= INET6_ADDRSTRLEN) {
                        char in6str[INET6_ADDRSTRLEN];
                        char buf[sizeof(struct in6_addr)];

                        this->ds_pcre_input.get_substr(pc.all(), in6str);

                        if (inet_pton(AF_INET6, in6str, buf) == 1) {
                            token_out = data_token_t(lpc);
                            return true;
                        }
                    }
                    this->ds_pcre_input.pi_next_offset =
                        this->ds_pcre_input.pi_offset;
                    break;

                default:
                    token_out = data_token_t(lpc);
                    return true;
                }
            }
            break;
        }
    }

    ensure((0 <= token_out && token_out < DT_TERMINAL_MAX));

    return true;
}
