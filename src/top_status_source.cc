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

#include "top_status_source.hh"

#include <sqlite3.h>

#include "base/injector.hh"
#include "bound_tags.hh"
#include "config.h"
#include "lnav_config.hh"
#include "logfile_sub_source.hh"
#include "sql_util.hh"

top_status_source::top_status_source()
{
    this->tss_fields[TSF_TIME].set_width(28);
    this->tss_fields[TSF_TIME].set_role(role_t::VCR_STATUS_INFO);
    this->tss_fields[TSF_USER_MSG].set_share(1);
    this->tss_fields[TSF_USER_MSG].right_justify(true);
    this->tss_fields[TSF_USER_MSG].set_role(role_t::VCR_STATUS_INFO);
}

void
top_status_source::update_time(const timeval& current_time)
{
    auto& sf = this->tss_fields[TSF_TIME];
    char buffer[32];

    buffer[0] = ' ';
    strftime(&buffer[1],
             sizeof(buffer) - 1,
             lnav_config.lc_ui_clock_format.c_str(),
             localtime(&current_time.tv_sec));
    sf.set_value(buffer);
}

void
top_status_source::update_time()
{
    struct timeval tv;

    gettimeofday(&tv, nullptr);
    this->update_time(tv);
}

struct user_msg_stmt {
    user_msg_stmt()
    {
        static const char* MSG_QUERY = R"(
SELECT message FROM lnav_user_notifications
  WHERE expiration IS NULL OR expiration > datetime('now')
  ORDER BY priority DESC
  LIMIT 1
)";

        auto& lnav_db = injector::get<auto_mem<sqlite3, sqlite_close_wrapper>&,
                                      sqlite_db_tag>();

        auto retcode = sqlite3_prepare_v2(
            lnav_db, MSG_QUERY, -1, this->ums_stmt.out(), nullptr);

        ensure(retcode == SQLITE_OK);
    }

    auto_mem<sqlite3_stmt> ums_stmt{sqlite3_finalize};
};

void
top_status_source::update_user_msg()
{
    static user_msg_stmt um_stmt;
    static auto& lnav_db
        = injector::get<auto_mem<sqlite3, sqlite_close_wrapper>&,
                        sqlite_db_tag>();

    auto& al = this->tss_fields[TSF_USER_MSG].get_value();
    al.clear();

    auto* stmt = um_stmt.ums_stmt.in();
    sqlite3_reset(stmt);

    auto count = sqlite3_bind_parameter_count(stmt);
    for (int lpc = 0; lpc < count; lpc++) {
        const auto* name = sqlite3_bind_parameter_name(stmt, lpc + 1);

        if (name[0] == '$') {
            const char* env_value;

            if ((env_value = getenv(&name[1])) != nullptr) {
                sqlite3_bind_text(stmt, lpc + 1, env_value, -1, SQLITE_STATIC);
            }
            continue;
        }
    }

    auto step_res = sqlite3_step(stmt);

    switch (step_res) {
        case SQLITE_OK:
        case SQLITE_DONE:
            break;
        case SQLITE_ROW: {
            int ncols = sqlite3_column_count(stmt);
            for (int lpc = 0; lpc < ncols; lpc++) {
                const auto* text = (const char*) sqlite3_column_text(stmt, lpc);

                al.with_ansi_string(text);
                al.append(" ");
            }
            break;
        }
        default: {
            log_error("failed to execute user-message expression: %s",
                      sqlite3_errmsg(lnav_db));
            break;
        }
    }
}
