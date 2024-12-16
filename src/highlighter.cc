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

#include "highlighter.hh"

#include "config.h"
#include "pcrepp/pcre2pp.hh"
#include "view_curses.hh"

highlighter&
highlighter::operator=(const highlighter& other)
{
    if (this == &other) {
        return *this;
    }

    this->h_name = other.h_name;
    this->h_role = other.h_role;
    this->h_regex = other.h_regex;
    this->h_format_name = other.h_format_name;
    this->h_attrs = other.h_attrs;
    this->h_text_formats = other.h_text_formats;
    this->h_nestable = other.h_nestable;

    return *this;
}

void
highlighter::annotate_capture(attr_line_t& al, const line_range& lr) const
{
    auto& sa = al.get_attrs();

    if (lr.lr_end <= lr.lr_start) {
        return;
    }
    if (!this->h_nestable) {
        for (const auto& attr : sa) {
            if (attr.sa_range.lr_end == -1) {
                continue;
            }
            if (!attr.sa_range.intersects(lr)) {
                continue;
            }
            if (attr.sa_type == &VC_STYLE || attr.sa_type == &VC_ROLE
                || attr.sa_type == &VC_FOREGROUND
                || attr.sa_type == &VC_BACKGROUND)
            {
                return;
            }
        }
    }

    if (this->h_role != role_t::VCR_NONE) {
        sa.emplace_back(lr, VC_ROLE.value(this->h_role));
    }
    if (!this->h_attrs.empty()) {
        sa.emplace_back(lr, VC_STYLE.value(this->h_attrs));
    }
}

void
highlighter::annotate(attr_line_t& al, int start) const
{
    if (!this->h_regex) {
        return;
    }

    auto& vc = view_colors::singleton();
    const auto& str = al.get_string();
    auto& sa = al.get_attrs();
    const auto sf = string_fragment::from_str_range(
        str, start, std::min(size_t{8192}, str.size()));

    if (!sf.is_valid()) {
        return;
    }

    timeval tstart, tend, tdiff;

    gettimeofday(&tstart, nullptr);

    this->h_regex->capture_from(sf).for_each(
        [&](lnav::pcre2pp::match_data& md) {
            if (md.get_count() == 1) {
                this->annotate_capture(al, to_line_range(md[0].value()));
            } else {
                for (size_t lpc = 1; lpc < md.get_count(); lpc++) {
                    if (!md[lpc]) {
                        continue;
                    }

                    const auto* name = this->h_regex->get_name_for_capture(lpc);
                    auto lr = to_line_range(md[lpc].value());

                    if (name != nullptr && name[0]) {
                        auto ident_attrs = vc.attrs_for_ident(name);

                        ident_attrs.ta_attrs |= this->h_attrs.ta_attrs;
                        if (this->h_role != role_t::VCR_NONE) {
                            auto role_attrs = vc.attrs_for_role(this->h_role);

                            ident_attrs.ta_attrs |= role_attrs.ta_attrs;
                        }
                        sa.emplace_back(lr, VC_STYLE.value(ident_attrs));
                    } else {
                        this->annotate_capture(al, lr);
                    }
                }
            }
        });
    gettimeofday(&tend, nullptr);
    timersub(&tend, &tstart, &tdiff);
    if (tdiff.tv_usec > 10000) {
        log_debug("slow highlight %s %s", this->h_name.c_str(),
            this->h_regex->get_pattern().c_str());
    }
}
