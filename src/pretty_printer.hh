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

    pretty_printer(data_scanner *ds, int leading_indent=0)
            : pp_leading_indent(leading_indent),
              pp_depth(0),
              pp_line_length(0),
              pp_scanner(ds) {
        this->pp_body_lines.push(0);
    };

    std::string print() {
        pcre_context_static<30> pc;
        data_token_t dt;

        while (this->pp_scanner->tokenize2(pc, dt)) {
            element el(dt, pc);

            switch (dt) {
                case DT_XML_EMPTY_TAG:
                    this->start_new_line();
                    this->pp_values.push_back(el);
                    this->start_new_line();
                    continue;
                case DT_XML_OPEN_TAG:
                    this->start_new_line();
                    this->write_element(el);
                    this->descend();
                    continue;
                case DT_XML_CLOSE_TAG:
                    this->flush_values();
                    this->ascend();
                    this->write_element(el);
                    this->start_new_line();
                    continue;
                case DT_LCURLY:
                case DT_LSQUARE:
                case DT_LPAREN:
                    this->flush_values(true);
                    this->pp_values.push_back(el);
                    this->descend();
                    continue;
                case DT_RCURLY:
                case DT_RSQUARE:
                case DT_RPAREN:
                    this->flush_values();
                    if (this->pp_body_lines.top()) {
                        this->start_new_line();
                    }
                    this->ascend();
                    this->write_element(el);
                    continue;
                case DT_COMMA:
                    if (this->pp_depth > 0) {
                        this->flush_values(true);
                        this->write_element(el);
                        this->start_new_line();
                        continue;
                    }
                    break;
                case DT_WHITE:
                    if (this->pp_values.empty() && this->pp_depth == 0) {
                        this->pp_leading_indent = el.e_capture.length();
                        continue;
                    }
                    break;
                default:
                    break;
            }
            this->pp_values.push_back(el);
        }
        while (this->pp_depth > 0) {
            this->ascend();
        }
        this->flush_values();

        return this->pp_stream.str();
    };

private:

    void convert_ip_address(const element &el) {
        union {
            struct sockaddr_in  sin;
            struct sockaddr_in6 sin6;
        } sa;
        pcre_input &pi = this->pp_scanner->get_input();
        std::string ipstr = pi.get_substr(&el.e_capture);
        std::string result = "unknown";
        char buffer[NI_MAXHOST];
        int socklen, rc;

        switch (el.e_token) {
            case DT_IPV4_ADDRESS:
                sa.sin.sin_family = AF_INET;
                rc = inet_pton(AF_INET, ipstr.c_str(), &sa.sin.sin_addr);
                socklen = sizeof(struct sockaddr_in);
                break;
            case DT_IPV6_ADDRESS:
                sa.sin6.sin6_family = AF_INET6;
                rc = inet_pton(AF_INET6, ipstr.c_str(), &sa.sin6.sin6_addr);
                socklen = sizeof(struct sockaddr_in6);
                break;
            default:
                require(0);
                break;
        }
        if (rc == 1 && reverse_lookup_enabled) {
            const struct timeval timeout = {0, 500 * 1000};

            {
                timer::interrupt_timer t(timeout, sigalrm_handler);
                if (t.arm_timer() == 0) {
                    rc = getnameinfo((struct sockaddr *)&sa, socklen,
                            buffer, sizeof(buffer), NULL, 0,
                            NI_NAMEREQD);
                    if (rc == 0) {
                        result = buffer;
                    }
                }
                else {
                    log_error("Unable to set timer, disabling reverse lookup");
                    reverse_lookup_enabled = 0;
                }
            }
            if (!reverse_lookup_enabled) {
                log_info("Reverse lookup in pretty-print view disabled");
            }
        }
        this->pp_stream << " " << ANSI_UNDERLINE_START <<
                "(" << result << ")" <<
                ANSI_NORM;
    }

    void descend() {
        this->pp_depth += 1;
        this->pp_body_lines.push(0);
    }

    void ascend() {
        if (this->pp_depth > 0) {
            int lines = this->pp_body_lines.top();
            this->pp_depth -= 1;
            this->pp_body_lines.pop();
            this->pp_body_lines.top() += lines;
        }
        else {
            this->pp_body_lines.top() = 0;
        }
    }

    void start_new_line() {
        bool has_output;

        if (this->pp_line_length > 0) {
            this->pp_stream << std::endl;
            this->pp_line_length = 0;
        }
        has_output = this->flush_values();
        if (has_output && this->pp_line_length > 0) {
            this->pp_stream << std::endl;
        }
        this->pp_line_length = 0;
        this->pp_body_lines.top() += 1;
    }

    bool flush_values(bool start_on_depth = false) {
        bool retval = false;

        while (!this->pp_values.empty()) {
            {
                element &el = this->pp_values.front();
                this->write_element(this->pp_values.front());
                if (start_on_depth &&
                        (el.e_token == DT_LSQUARE ||
                         el.e_token == DT_LCURLY)) {
                    if (this->pp_line_length > 0) {
                        this->pp_stream << std::endl;
                    }
                    this->pp_line_length = 0;
                }
            }
            this->pp_values.pop_front();
            retval = true;
        }
        return retval;
    }

    void append_indent() {
        this->pp_stream << std::string(this->pp_leading_indent, ' ');
        if (this->pp_stream.tellp() == this->pp_leading_indent) {
            return;
        }
        for (int lpc = 0; lpc < this->pp_depth; lpc++) {
            this->pp_stream << "    ";
        }
    }

    void write_element(const element &el) {
        if (this->pp_line_length == 0 && el.e_token == DT_WHITE) {
            return;
        }
        if (this->pp_line_length == 0 && el.e_token == DT_LINE) {
            return;
        }
        pcre_input &pi = this->pp_scanner->get_input();
        if (this->pp_line_length == 0) {
            this->append_indent();
        }
        if (el.e_token == DT_QUOTED_STRING) {
            pcre_input &pi = this->pp_scanner->get_input();
            auto_mem<char> unquoted_str((char *)malloc(pi.pi_length));
            const char *start = pi.get_substr_start(&el.e_capture);
            unquote(unquoted_str.in(), start, el.e_capture.length());
            data_scanner ds(unquoted_str.in());
            pretty_printer str_pp(&ds, this->pp_leading_indent);
            std::string result = str_pp.print();
            if (result.find('\n') != std::string::npos) {
                this->pp_stream << str_pp.print();
            } else {
                this->pp_stream << pi.get_substr(&el.e_capture);
            }
        } else {
            this->pp_stream << pi.get_substr(&el.e_capture);
            switch (el.e_token) {
                case DT_IPV4_ADDRESS:
                case DT_IPV6_ADDRESS:
                    this->convert_ip_address(el);
                    break;
                default:
                    break;
            }
        }
        this->pp_line_length += el.e_capture.length();
        if (el.e_token == DT_LINE) {
            this->pp_line_length = 0;
            this->pp_body_lines.top() += 1;
        }
    }

    int pp_leading_indent;
    int pp_depth;
    int pp_line_length;
    std::stack<int> pp_body_lines;
    data_scanner *pp_scanner;
    std::ostringstream pp_stream;
    std::deque<element> pp_values;

};

#endif
