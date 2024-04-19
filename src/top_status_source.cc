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

#include "config.h"
#include "lnav.hh"
#include "md2attr_line.hh"
#include "md4cpp.hh"
#include "shlex.hh"
#include "sqlitepp.client.hh"
#include "top_status_source.cfg.hh"

static const char* MSG_QUERY = R"(
SELECT message FROM lnav_user_notifications
  WHERE message IS NOT NULL AND
        (expiration IS NULL OR expiration > datetime('now')) AND
        (views IS NULL OR
         json_contains(views, (SELECT name FROM lnav_top_view)))
  ORDER BY priority DESC, expiration ASC
  LIMIT 1
)";

top_status_source::top_status_source(auto_sqlite3& db,
                                     const top_status_source_cfg& cfg)
    : tss_config(cfg),
      tss_user_msgs_stmt(prepare_stmt(db.in(), MSG_QUERY).unwrap())
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
    tm current_tm;

    buffer[0] = ' ';
    strftime(&buffer[1],
             sizeof(buffer) - 1,
             this->tss_config.tssc_clock_format.c_str(),
             localtime_r(&current_time.tv_sec, &current_tm));
    sf.set_value(buffer);
}

void
top_status_source::update_time()
{
    struct timeval tv;

    gettimeofday(&tv, nullptr);
    this->update_time(tv);
}

void
top_status_source::update_user_msg()
{
    auto& al = this->tss_fields[TSF_USER_MSG].get_value();
    al.clear();

    this->tss_user_msgs_stmt.reset();
    auto fetch_res = this->tss_user_msgs_stmt.fetch_row<std::string>();
    fetch_res.match(
        [&al](const std::string& value) {
            shlex lexer(value);
            std::string user_note;

            lexer.with_ignore_quotes(true).eval(
                user_note,
                scoped_resolver{&lnav_data.ld_exec_context.ec_global_vars});

            md2attr_line mdal;
            auto parse_res = md4cpp::parse(user_note, mdal);
            if (parse_res.isOk()) {
                al = parse_res.unwrap();
            } else {
                log_error("failed to parse user note as markdown: %s",
                          parse_res.unwrapErr().c_str());
                al = user_note;
            }

            scrub_ansi_string(al.get_string(), &al.get_attrs());
            al.append(" ");
        },
        [](const prepared_stmt::end_of_rows&) {},
        [](const prepared_stmt::fetch_error& fe) {
            log_error("failed to execute user-message expression: %s",
                      fe.fe_msg.c_str());
        });
}
