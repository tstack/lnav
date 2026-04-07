/**
 * Copyright (c) 2026, Timothy Stack
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
 *
 * @file lnav.tz.cc
 */

#include "lnav.tz.hh"

#include "attr_line.hh"
#include "config.h"
#include "itertools.similar.hh"
#include "lnav_log.hh"

namespace lnav {

static attr_line_t&
symbol_reducer(const std::string& elem, attr_line_t& accum)
{
    return accum.append("\n   ").append(lnav::roles::symbol(elem));
}

Result<const date::time_zone*, lnav::console::user_message>
locate_zone(string_fragment tz_name)
{
    try {
        return Ok(date::locate_zone(tz_name.to_string_view()));
    } catch (const std::runtime_error& e) {
        attr_line_t note;

        try {
            note = (date::get_tzdb().zones
                    | lnav::itertools::map(&date::time_zone::name)
                    | lnav::itertools::similar_to(tz_name.to_string())
                    | lnav::itertools::fold(symbol_reducer, attr_line_t{}))
                       .add_header("did you mean one of the following?");
        } catch (const std::runtime_error& e) {
            log_error("unable to get timezones: %s", e.what());
        }
        auto um = lnav::console::user_message::error(
                      attr_line_t().append_quoted(tz_name).append(
                          " is not a valid timezone"))
                      .with_reason(e.what())
                      .with_note(note)
                      .move();
        return Err(um);
    }
}

}  // namespace lnav
