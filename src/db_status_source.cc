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
 */

#include "db_status_source.hh"

#include "base/attr_line.hh"
#include "base/humanize.time.hh"
#include "base/injector.hh"
#include "base/intern_string.hh"
#include "base/string_attr_type.hh"
#include "base/time_util.hh"
#include "command_executor.hh"
#include "db_sub_source.hh"
#include "fmt/format.h"
#include "logfile_sub_source.hh"
#include "readline_highlighters.hh"

using namespace lnav::roles::literals;

db_status_source::db_status_source()
{
    this->dss_fields[DSF_TITLE].set_width(9);
    this->dss_fields[DSF_TITLE].set_role(role_t::VCR_STATUS_TITLE);
    this->dss_fields[DSF_TITLE].set_value(" DB View");
    this->dss_fields[DSF_STITCH_TITLE].set_width(2);
    this->dss_fields[DSF_STITCH_TITLE].set_stitch_value(
        role_t::VCR_STATUS_STITCH_TITLE_TO_NORMAL,
        role_t::VCR_STATUS_STITCH_NORMAL_TO_TITLE);
    this->dss_fields[DSF_RELOAD].set_width(3);
    this->dss_fields[DSF_QUERY].set_share(3);
    this->dss_fields[DSF_TIMING].right_justify(true);
    this->dss_fields[DSF_TIMING].set_width(80);
}

bool
db_status_source::update_from_db_source()
{
    static const auto& dls = injector::get<db_label_source&>();
    static auto& lss = injector::get<logfile_sub_source&>();
    auto changed = false;

    if (dls.dls_user_query.empty()) {
        changed |= this->dss_fields[DSF_RELOAD].clear();
        changed |= this->dss_fields[DSF_QUERY].clear();
        changed |= this->dss_fields[DSF_TIMING].clear();
        return changed;
    }

    auto reload_al = attr_line_t(" ")
                         .append(" ", VC_ICON.value(ui_icon_t::reload))
                         .append(" ");
    changed |= this->dss_fields[DSF_RELOAD].set_value(reload_al);

    attr_line_t query_al;
    query_al.append(" ;").append(dls.dls_user_query);
    readline_sql_highlighter(
        query_al, lnav::sql::dialect::sqlite, std::nullopt);
    changed |= this->dss_fields[DSF_QUERY].set_value(query_al);

    if (dls.dls_query_start.has_value()) {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            dls.dls_query_start.value().time_since_epoch());
        auto ago = humanize::time::point::from_tv(to_timeval(us)).as_time_ago();
        attr_line_t timing_al;
        if (dls.dls_query_end.has_value()) {
            auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(
                dls.dls_query_end.value() - dls.dls_query_start.value());
            auto dur = humanize::time::duration::from_tv(to_timeval(dur_us))
                           .with_compact(false)
                           .to_string();
            timing_al.append("ran ").append(lnav::roles::time_ago(ago));
            if (dls.dls_query_touches_log_data) {
                bool is_stale
                    = lss.lss_index_generation != dls.dls_log_gen_at_query
                    || lss.text_line_count() != dls.dls_log_line_count_at_query;
                timing_al.append(" on ").append(
                    is_stale ? lnav::roles::warning("old log data")
                             : lnav::roles::ok("current log data"));
            }
            timing_al.append(" and ")
                .append(dls.is_error() ? lnav::roles::error("failed")
                                       : lnav::roles::ok("succeeded"))
                .append(" after ")
                .append(lnav::roles::number(dur));
        } else {
            timing_al.append("started ").append(lnav::roles::time_ago(ago));
        }
        timing_al.append(" ");
        changed |= this->dss_fields[DSF_TIMING].set_value(timing_al);
    } else {
        changed |= this->dss_fields[DSF_TIMING].clear();
    }

    return changed;
}
