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

#ifndef lnav_top_status_source_hh
#define lnav_top_status_source_hh

#include "sqlitepp.client.hh"
#include "sqlitepp.hh"
#include "statusview_curses.hh"
#include "top_status_source.cfg.hh"

class top_status_source : public status_data_source {
public:
    enum field_t {
        TSF_TIME,
        TSF_USER_MSG,

        TSF__MAX
    };

    explicit top_status_source(auto_sqlite3& db,
                               const top_status_source_cfg& cfg);

    using injectable
        = top_status_source(auto_sqlite3& db, const top_status_source_cfg& cfg);

    size_t statusview_fields() override { return TSF__MAX; }

    status_field& statusview_value_for_field(int field) override
    {
        return this->tss_fields[field];
    }

    bool update_time(const struct timeval& current_time);

    void update_time();

    bool update_user_msg();

private:
    const top_status_source_cfg& tss_config;
    status_field tss_fields[TSF__MAX];
    prepared_stmt tss_user_msgs_stmt;
};

#endif
