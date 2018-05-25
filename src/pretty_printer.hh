/**
 * Copyright (c) 2015, Timothy Stack
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

#ifndef __pretty_printer_hh
#define __pretty_printer_hh

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

#include <stack>
#include <deque>
#include <sstream>
#include <iomanip>
#include <utility>

#include "timer.hh"
#include "ansi_scrubber.hh"
#include "data_scanner.hh"
#include "lnav_util.hh"

extern sig_atomic_t reverse_lookup_enabled;

void sigalrm_handler(int sig);

class pretty_printer {

public:

    struct element {
        element(data_token_t token, pcre_context &pc)
                : e_token(token), e_capture(*pc.all()) {

        };

        data_token_t e_token;
        pcre_context::capture_t e_capture;
    };

    pretty_printer(data_scanner *ds, string_attrs_t sa, int leading_indent=0)
            : pp_leading_indent(leading_indent),
              pp_scanner(ds),
              pp_attrs(std::move(sa)) {
        this->pp_body_lines.push(0);

        pcre_context_static<30> pc;
        data_token_t dt;

        this->pp_scanner->reset();
        while (this->pp_scanner->tokenize2(pc, dt)) {
            if (dt == DT_XML_CLOSE_TAG) {
                pp_is_xml = true;
            }
        }
    };

    void append_to(attr_line_t &al);

private:

    void convert_ip_address(const element &el);

    void descend();

    void ascend();

    void start_new_line();

    bool flush_values(bool start_on_depth = false);

    void append_indent();

    void write_element(const element &el);

    int pp_leading_indent;
    int pp_depth{0};
    int pp_line_length{0};
    int pp_soft_indent{0};
    std::stack<int> pp_body_lines{};
    data_scanner *pp_scanner;
    string_attrs_t pp_attrs;
    std::ostringstream pp_stream;
    std::deque<element> pp_values{};
    int pp_shift_accum{0};
    bool pp_is_xml{false};
};

#endif
