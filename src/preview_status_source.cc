/**
 * Copyright (c) 2025, Timothy Stack
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

#include "preview_status_source.hh"

static constexpr char HIDE_TOGGLE_MSG[] = "Press F3 to hide \u25bc ";
static constexpr char SHOW_TOGGLE_MSG[] = "Press F3 to show \u25b2 ";

preview_status_source::preview_status_source()
{
    this->tss_fields[TSF_TITLE].set_width(14);
    this->tss_fields[TSF_TITLE].set_role(role_t::VCR_STATUS_TITLE);
    this->tss_fields[TSF_TITLE].set_value(" Preview Data ");
    this->tss_fields[TSF_STITCH_TITLE].set_width(2);
    this->tss_fields[TSF_STITCH_TITLE].set_stitch_value(
        role_t::VCR_STATUS_STITCH_TITLE_TO_NORMAL,
        role_t::VCR_STATUS_STITCH_NORMAL_TO_TITLE);
    this->tss_fields[TSF_DESCRIPTION].set_share(1);
    this->tss_fields[TSF_TOGGLE].set_width(strlen(HIDE_TOGGLE_MSG) + 1);
    this->tss_fields[TSF_TOGGLE].set_value(HIDE_TOGGLE_MSG);
    this->tss_fields[TSF_TOGGLE].right_justify(true);
}

void
preview_status_source::update_toggle_msg(bool shown)
{
    this->tss_fields[TSF_TOGGLE].set_value(shown ? HIDE_TOGGLE_MSG
                                                 : SHOW_TOGGLE_MSG);
}
