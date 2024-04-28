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

#include "attr_line.builder.hh"

attr_line_builder&
attr_line_builder::append_as_hexdump(const string_fragment& sf)
{
    auto byte_off = size_t{0};
    for (auto ch : sf) {
        if (byte_off == 8) {
            this->append(" ");
        }
        std::optional<role_t> ro;
        if (ch == '\0') {
            ro = role_t::VCR_NULL;
        } else if (isspace(ch) || iscntrl(ch)) {
            ro = role_t::VCR_ASCII_CTRL;
        } else if (!isprint(ch)) {
            ro = role_t::VCR_NON_ASCII;
        }
        auto ag = ro.has_value() ? this->with_attr(VC_ROLE.value(ro.value()))
                                 : this->with_default();
        this->appendf(FMT_STRING(" {:0>2x}"), ch);
        byte_off += 1;
    }
    for (; byte_off < 16; byte_off++) {
        if (byte_off == 8) {
            this->append(" ");
        }
        this->append("   ");
    }
    this->append("  ");
    byte_off = 0;
    for (auto ch : sf) {
        if (byte_off == 8) {
            this->append(" ");
        }
        if (ch == '\0') {
            auto ag = this->with_attr(VC_ROLE.value(role_t::VCR_NULL));
            this->append("\u22c4");
        } else if (isspace(ch)) {
            auto ag = this->with_attr(VC_ROLE.value(role_t::VCR_ASCII_CTRL));
            this->append("_");
        } else if (iscntrl(ch)) {
            auto ag = this->with_attr(VC_ROLE.value(role_t::VCR_ASCII_CTRL));
            this->append("\u2022");
        } else if (isprint(ch)) {
            this->alb_line.get_string().push_back(ch);
        } else {
            auto ag = this->with_attr(VC_ROLE.value(role_t::VCR_NON_ASCII));
            this->append("\u00d7");
        }
        byte_off += 1;
    }

    return *this;
}
