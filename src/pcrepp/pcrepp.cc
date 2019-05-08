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
 *
 * @file pcrepp.cc
 */

#include "config.h"

#include <pcrecpp.h>

#include "pcrepp.hh"

using namespace std;

const int JIT_STACK_MIN_SIZE = 32 * 1024;
const int JIT_STACK_MAX_SIZE = 512 * 1024;

pcre_context::capture_t *pcre_context::operator[](const char *name) const
{
    capture_t *retval = NULL;
    int index;

    index = this->pc_pcre->name_index(name);
    if (index != PCRE_ERROR_NOSUBSTRING) {
        retval = &this->pc_captures[index + 1];
    }

    return retval;
}

void pcrepp::find_captures(const char *pattern)
{
    bool in_class = false, in_escape = false, in_literal = false;
    vector<pcre_context::capture> cap_in_progress;

    for (int lpc = 0; pattern[lpc]; lpc++) {
        if (in_class) {
            if (pattern[lpc] == ']') {
                in_class = false;
            }
        }
        else if (in_escape) {
            in_escape = false;
            if (pattern[lpc] == 'Q') {
                in_literal = true;
            }
        }
        else if (in_literal) {
            if (pattern[lpc] == '\\' && pattern[lpc + 1] == 'E') {
                in_literal = false;
                lpc += 1;
            }
        }
        else {
            switch (pattern[lpc]) {
                case '\\':
                    in_escape = true;
                    break;
                case '[':
                    in_class = true;
                    break;
                case '(':
                    cap_in_progress.emplace_back(lpc, lpc);
                    break;
                case ')': {
                    if (!cap_in_progress.empty()) {
                        pcre_context::capture &cap = cap_in_progress.back();
                        char first = '\0', second = '\0', third = '\0';
                        bool is_cap = false;

                        cap.c_end = lpc + 1;
                        if (cap.length() >= 2) {
                            first = pattern[cap.c_begin + 1];
                        }
                        if (cap.length() >= 3) {
                            second = pattern[cap.c_begin + 2];
                        }
                        if (cap.length() >= 4) {
                            third = pattern[cap.c_begin + 3];
                        }
                        if (first == '?') {
                            if (second == '<' || second == '\'') {
                                is_cap = true;
                            }
                            if (second == 'P' && third == '<') {
                                is_cap = true;
                            }
                        }
                        else if (first != '*') {
                            is_cap = true;
                        }
                        if (is_cap) {
                            this->p_captures.push_back(cap);
                        }
                        cap_in_progress.pop_back();
                    }
                    break;
                }
            }
        }
    }
}

bool pcrepp::match(pcre_context &pc, pcre_input &pi, int options) const
{
    int         length, startoffset, filtered_options = options;
    int         count = pc.get_max_count();
    const char *str;
    int         rc;

    pc.set_pcrepp(this);
    pi.pi_offset = pi.pi_next_offset;

    str = pi.get_string();
    if (filtered_options & PCRE_ANCHORED) {
        filtered_options &= ~PCRE_ANCHORED;
        str         = &str[pi.pi_offset];
        startoffset = 0;
        length      = pi.pi_length - pi.pi_offset;
    }
    else {
        startoffset = pi.pi_offset;
        length      = pi.pi_length;
    }
    rc = pcre_exec(this->p_code,
                   this->p_code_extra.in(),
                   str,
                   length,
                   startoffset,
                   filtered_options,
                   (int *)pc.all(),
                   count * 2);

    if (rc < 0) {
        switch (rc) {
            case PCRE_ERROR_NOMATCH:
                break;
            case PCRE_ERROR_PARTIAL:
                pc.set_count(1);
                return true;

            default:
                log_error("pcre err %d", rc);
                break;
        }
    }
    else if (rc == 0) {
        rc = 0;
    }
    else if (pc.all()->c_begin == pc.all()->c_end) {
        rc = 0;
    }
    else {
        if (options & PCRE_ANCHORED) {
            for (int lpc = 0; lpc < rc; lpc++) {
                if (pc.all()[lpc].c_begin == -1) {
                    continue;
                }
                pc.all()[lpc].c_begin += pi.pi_offset;
                pc.all()[lpc].c_end   += pi.pi_offset;
            }
        }
        pi.pi_next_offset = pc.all()->c_end;
    }

    pc.set_count(rc);

    return rc > 0;
}

void pcrepp::study(void)
{
    const char *errptr;

    this->p_code_extra = pcre_study(this->p_code,
#ifdef PCRE_STUDY_JIT_COMPILE
                                    PCRE_STUDY_JIT_COMPILE,
#else
        0,
#endif
                                    &errptr);
    if (!this->p_code_extra && errptr) {
        log_error("pcre_study error: %s", errptr);
    }
    if (this->p_code_extra != NULL) {
        pcre_extra *extra = this->p_code_extra;

        extra->flags |= (PCRE_EXTRA_MATCH_LIMIT |
                         PCRE_EXTRA_MATCH_LIMIT_RECURSION);
        extra->match_limit           = 10000;
        extra->match_limit_recursion = 500;
#ifdef PCRE_STUDY_JIT_COMPILE
        // pcre_assign_jit_stack(extra, NULL, jit_stack());
#endif
    }
    pcre_fullinfo(this->p_code,
                  this->p_code_extra,
                  PCRE_INFO_CAPTURECOUNT,
                  &this->p_capture_count);
    pcre_fullinfo(this->p_code,
                  this->p_code_extra,
                  PCRE_INFO_NAMECOUNT,
                  &this->p_named_count);
    pcre_fullinfo(this->p_code,
                  this->p_code_extra,
                  PCRE_INFO_NAMEENTRYSIZE,
                  &this->p_name_len);
    pcre_fullinfo(this->p_code,
                  this->p_code_extra,
                  PCRE_INFO_NAMETABLE,
                  &this->p_named_entries);
}

#ifdef PCRE_STUDY_JIT_COMPILE
pcre_jit_stack *pcrepp::jit_stack(void)
{
    static pcre_jit_stack *retval = NULL;

    if (retval == NULL) {
        retval = pcre_jit_stack_alloc(JIT_STACK_MIN_SIZE, JIT_STACK_MAX_SIZE);
    }

    return retval;
}

#else
#warning "pcrejit is not available, search performance will be degraded"

void pcrepp::pcre_free_study(pcre_extra *extra)
{
    free(extra);
}
#endif
