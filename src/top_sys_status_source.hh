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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _top_sys_status_source_hh
#define _top_sys_status_source_hh

#include <string>

#include "logfile_sub_source.hh"
#include "statusview_curses.hh"

class top_sys_status_source : public status_data_source {
public:
    typedef enum {
        TSF_CPU,
        TSF_MEM,
        TSF_TRAF,

        TSF__MAX
    } field_t;

    top_sys_status_source()
    {
        static std::string names[TSF__MAX] = {
            "#CPU",
            "#Mem",
            "#Traf",
        };

        int lpc;

        for (lpc = 0; lpc < TSF__MAX; lpc++) {
            this->tss_fields[lpc].set_width(5);
            this->tss_fields[lpc].set_value(names[lpc]);
        }
        this->tss_fields[TSF_CPU].set_role(role_t::VCR_WARN_STATUS);
        this->tss_fields[TSF_MEM].set_role(role_t::VCR_ALERT_STATUS);
        this->tss_fields[TSF_TRAF].set_role(role_t::VCR_ACTIVE_STATUS);
    };

    size_t statusview_fields()
    {
        return TSF__MAX;
    };

    status_field& statusview_value_for_field(int field)
    {
        return this->tss_fields[field];
    };

private:
    telltale_field tss_fields[TSF__MAX];
};
#endif
