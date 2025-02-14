/**
 * Copyright (c) 2018, Timothy Stack
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
 * @file input_dispatcher.cc
 */

#include <array>

#include "input_dispatcher.hh"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "base/lnav_log.hh"
#include "base/time_util.hh"
#include "config.h"
#include "fmt/color.h"
#include "ww898/cp_utf8.hpp"

using namespace ww898;

template<typename A>
static void
to_key_seq(A& dst, const char* src)
{
    dst[0] = '\0';
    for (size_t lpc = 0; src[lpc]; lpc++) {
        snprintf(dst.data() + strlen(dst.data()),
                 dst.size() - strlen(dst.data()),
                 "x%02x",
                 src[lpc] & 0xff);
    }
}

void
input_dispatcher::new_input(const struct timeval& current_time,
                            notcurses* nc,
                            ncinput& ch)
{
    std::optional<bool> handled = std::nullopt;
    std::array<char, 32 * 3 + 1> keyseq{0};
    std::string eff_str;

    for (size_t lpc = 0; ch.eff_text[lpc]; lpc++) {
        fmt::format_to(
            std::back_inserter(eff_str), FMT_STRING("{:02x}"), ch.eff_text[lpc]);
    }
    log_debug("new input %x %d/%x(%c)/%s/%s evtype=%d",
              ch.modifiers,
              ch.id,
              ch.id,
              ch.id < 0x7f && isprint(ch.id) ? ch.id : ' ',
              ch.utf8,
              eff_str.c_str(),
              ch.evtype);
    if (ncinput_mouse_p(&ch)) {
        log_debug("  mouse [x=%d;y=%d]", ch.x, ch.y);
    }
    if (!ncinput_mouse_p(&ch) && ch.evtype == NCTYPE_RELEASE) {
        return;
    }

    if (ncinput_lock_p(&ch) || ncinput_modifier_p(&ch)) {
        return;
    }

    if (ncinput_mouse_p(&ch)) {
        this->id_mouse_handler(nc, ch);
        return;
    }

    if (ch.id == NCKEY_PASTE) {
        keyseq[0] = '\0';
        this->id_key_handler(nc, ch, keyseq.data());
        return;
    }

    if (ch.id > 0xff) {
        if (NCKEY_F00 <= ch.id && ch.id <= NCKEY_F60) {
            snprintf(keyseq.data(), keyseq.size(), "f%d", ch.id - NCKEY_F00);
        } else {
            snprintf(keyseq.data(), keyseq.size(), "n%04o", ch.id);
        }
        log_debug("nckey %s", keyseq.data());
        handled = this->id_key_handler(nc, ch, keyseq.data());
    } else {
        auto seq_size = utf::utf8::char_size([&ch]() {
                            return std::make_pair(ch.eff_text[0], 16);
                        }).unwrapOr(size_t{1});
        log_debug("seq_size %d", seq_size);
        if (seq_size == 1) {
            snprintf(
                keyseq.data(), keyseq.size(), "x%02x", ch.eff_text[0] & 0xff);
            log_debug("key %s", keyseq.data());
            handled = this->id_key_handler(nc, ch, keyseq.data());
        }
    }

    if (handled && !handled.value()) {
        this->id_unhandled_handler(keyseq.data());
    }
}
