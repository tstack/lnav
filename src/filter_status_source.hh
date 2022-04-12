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
 */

#ifndef lnav_filter_status_source_hh
#define lnav_filter_status_source_hh

#include <string>

#include "statusview_curses.hh"
#include "textview_curses.hh"

class filter_status_source : public status_data_source {
public:
    typedef enum {
        TSF_FILES_TITLE,
        TSF_FILES_RIGHT_STITCH,
        TSF_TITLE,
        TSF_STITCH_TITLE,
        TSF_COUNT,
        TSF_FILTERED,
        TSF_HELP,

        TSF__MAX
    } field_t;

    filter_status_source();

    size_t statusview_fields() override;

    status_field& statusview_value_for_field(int field) override;

    void update_filtered(text_sub_source* tss);

private:
    status_field tss_fields[TSF__MAX];
    status_field tss_error;
    int bss_last_filtered_count{0};
    sig_atomic_t bss_filter_counter{0};
};

class filter_help_status_source : public status_data_source {
public:
    filter_help_status_source();

    size_t statusview_fields() override;

    status_field& statusview_value_for_field(int field) override;

    status_field fss_prompt{1024, role_t::VCR_STATUS};
    status_field fss_error{1024, role_t::VCR_ALERT_STATUS};

private:
    status_field fss_help;
};

#endif
