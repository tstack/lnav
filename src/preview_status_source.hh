/**
 * Copyright (c) 2017, Timothy Stack
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

#ifndef lnav_preview_status_source_hh
#define lnav_preview_status_source_hh

#include "statusview_curses.hh"

class preview_status_source : public status_data_source {
public:
    typedef enum {
        TSF_TITLE,
        TSF_STITCH_TITLE,
        TSF_DESCRIPTION,
        TSF_TOGGLE,

        TSF__MAX
    } field_t;

    preview_status_source()
    {
        static constexpr char TOGGLE_MSG[] = "Press CTRL+P to show/hide";

        this->tss_fields[TSF_TITLE].set_width(14);
        this->tss_fields[TSF_TITLE].set_role(role_t::VCR_STATUS_TITLE);
        this->tss_fields[TSF_TITLE].set_value(" Preview Data ");
        this->tss_fields[TSF_STITCH_TITLE].set_width(2);
        this->tss_fields[TSF_STITCH_TITLE].set_stitch_value(
            role_t::VCR_STATUS_STITCH_TITLE_TO_NORMAL,
            role_t::VCR_STATUS_STITCH_NORMAL_TO_TITLE);
        this->tss_fields[TSF_DESCRIPTION].set_share(1);
        this->tss_fields[TSF_TOGGLE].set_width(strlen(TOGGLE_MSG) + 1);
        this->tss_fields[TSF_TOGGLE].set_value(TOGGLE_MSG);
        this->tss_fields[TSF_TOGGLE].right_justify(true);
    }

    size_t statusview_fields() override { return TSF__MAX; }

    status_field& statusview_value_for_field(int field) override
    {
        return this->tss_fields[field];
    }

    status_field& get_description()
    {
        return this->tss_fields[TSF_DESCRIPTION];
    }

private:
    status_field tss_fields[TSF__MAX];
};

#endif
