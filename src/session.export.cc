/**
 * Copyright (c) 2022, Timothy Stack
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

#include "session.export.hh"

#include "base/injector.hh"
#include "bound_tags.hh"
#include "lnav.hh"
#include "sqlitepp.client.hh"
#include "sqlitepp.hh"
#include "textview_curses.hh"

struct log_message_session_state {
    int64_t lmss_time_msecs;
    std::string lmss_format;
    bool lmss_mark;
    nonstd::optional<std::string> lmss_comment;
    nonstd::optional<std::string> lmss_tags;
    std::string lmss_hash;
};

template<>
struct from_sqlite<log_message_session_state> {
    inline log_message_session_state operator()(int argc,
                                                sqlite3_value** argv,
                                                int argi)
    {
        return {
            from_sqlite<int64_t>()(argc, argv, argi + 0),
            from_sqlite<std::string>()(argc, argv, argi + 1),
            from_sqlite<bool>()(argc, argv, argi + 2),
            from_sqlite<nonstd::optional<std::string>>()(argc, argv, argi + 3),
            from_sqlite<nonstd::optional<std::string>>()(argc, argv, argi + 4),
            from_sqlite<std::string>()(argc, argv, argi + 5),
        };
    }
};

struct log_filter_session_state {
    std::string lfss_name;
    bool lfss_enabled;
    std::string lfss_type;
    std::string lfss_language;
    std::string lfss_pattern;
};

template<>
struct from_sqlite<log_filter_session_state> {
    inline log_filter_session_state operator()(int argc,
                                               sqlite3_value** argv,
                                               int argi)
    {
        return {
            from_sqlite<std::string>()(argc, argv, argi + 0),
            from_sqlite<bool>()(argc, argv, argi + 1),
            from_sqlite<std::string>()(argc, argv, argi + 2),
            from_sqlite<std::string>()(argc, argv, argi + 3),
            from_sqlite<std::string>()(argc, argv, argi + 4),
        };
    }
};

namespace lnav {
namespace session {

Result<void, lnav::console::user_message>
export_to(FILE* file)
{
    static auto& lnav_db
        = injector::get<auto_mem<sqlite3, sqlite_close_wrapper>&,
                        sqlite_db_tag>();

    static const char* BOOKMARK_QUERY = R"(
SELECT log_time_msecs, log_format, log_mark, log_comment, log_tags, log_line_hash
   FROM all_logs
   WHERE log_mark = 1 OR log_comment IS NOT NULL OR log_tags IS NOT NULL
)";

    static const char* FILTER_QUERY = R"(
SELECT view_name, enabled, type, language, pattern FROM lnav_view_filters
)";

    static const char* HEADER = R"(
# This file is an export of an lnav session.  You can type
# '|/path/to/this/file' in lnav to execute this file and
# restore the state of the session.
#
# The files loaded into the session were:
)";

    static const char* MARK_HEADER = R"(

# The following SQL statements will restore the bookmarks,
# comments, and tags that were added in the session.

)";

    static const char* FILTER_HEADER = R"(

# The following SQL statements will restore the filters that
# were added in the session.

)";

    console::user_message errmsg;

    auto prep_res = prepare_stmt(lnav_db.in(), BOOKMARK_QUERY);
    if (prep_res.isErr()) {
        return Err(
            console::user_message::error("unable to export log bookmarks")
                .with_reason(prep_res.unwrapErr()));
    }

    fmt::print(file, FMT_STRING("{}"), HEADER);
    for (const auto& lf : lnav_data.ld_active_files.fc_files) {
        fmt::print(file, FMT_STRING("#   {}"), lf->get_filename());
    }

    auto added_mark_header = false;
    auto bookmark_stmt = prep_res.unwrap();
    auto done = false;
    while (!done) {
        done = bookmark_stmt.fetch_row<log_message_session_state>().match(
            [file, &added_mark_header](const log_message_session_state& lmss) {
                if (!added_mark_header) {
                    fmt::print(file, FMT_STRING("{}"), MARK_HEADER);
                    added_mark_header = true;
                }
                fmt::print(file,
                           FMT_STRING(";UPDATE all_logs "
                                      "SET log_mark = {}, "
                                      "log_comment = {}, "
                                      "log_tags = {} "
                                      "WHERE log_time_msecs = {} AND "
                                      "log_format = {} AND "
                                      "log_line_hash = {}\n"),
                           lmss.lmss_mark ? "1" : "0",
                           sqlitepp::quote(lmss.lmss_comment),
                           sqlitepp::quote(lmss.lmss_tags),
                           lmss.lmss_time_msecs,
                           sqlitepp::quote(lmss.lmss_format),
                           sqlitepp::quote(lmss.lmss_hash));
                return false;
            },
            [](prepared_stmt::end_of_rows) { return true; },
            [&errmsg](const prepared_stmt::fetch_error& fe) {
                errmsg
                    = console::user_message::error(
                          "failed to fetch bookmark metadata for log message")
                          .with_reason(fe.fe_msg);
                return true;
            });
    }

    if (!errmsg.um_message.empty()) {
        return Err(errmsg);
    }

    auto prep_filter_res = prepare_stmt(lnav_db.in(), FILTER_QUERY);
    if (prep_filter_res.isErr()) {
        return Err(console::user_message::error("unable to export filter state")
                       .with_reason(prep_filter_res.unwrapErr()));
    }

    auto added_filter_header = false;
    auto filter_stmt = prep_filter_res.unwrap();
    done = false;
    while (!done) {
        done = filter_stmt.fetch_row<log_filter_session_state>().match(
            [file, &added_filter_header](const log_filter_session_state& lfss) {
                if (!added_filter_header) {
                    fmt::print(file, FMT_STRING("{}"), FILTER_HEADER);
                    added_filter_header = true;
                }
                fmt::print(
                    file,
                    FMT_STRING(";REPLACE INTO lnav_view_filters "
                               "(view_name, enabled, type, language, pattern) "
                               "VALUES ({}, {}, {}, {}, {})\n"),
                    sqlitepp::quote(lfss.lfss_name),
                    lfss.lfss_enabled ? 1 : 0,
                    sqlitepp::quote(lfss.lfss_type),
                    sqlitepp::quote(lfss.lfss_language),
                    sqlitepp::quote(lfss.lfss_pattern));
                return false;
            },
            [](prepared_stmt::end_of_rows) { return true; },
            [&errmsg](const prepared_stmt::fetch_error& fe) {
                errmsg = console::user_message::error(
                             "failed to fetch filter state for views")
                             .with_reason(fe.fe_msg);
                return true;
            });
    }

    if (!errmsg.um_message.empty()) {
        return Err(errmsg);
    }

    for (auto view_index : {LNV_LOG, LNV_TEXT}) {
        auto& tc = lnav_data.ld_views[view_index];
        if (tc.get_inner_height() == 0_vl) {
            continue;
        }

        fmt::print(file,
                   FMT_STRING("\n# The following commands will restore the"
                              "\n# state of the {} view.\n\n"),
                   lnav_view_titles[view_index]);
        fmt::print(file,
                   FMT_STRING(":switch-to-view {}\n"),
                   lnav_view_strings[view_index]);

        auto* tss = tc.get_sub_source();
        auto* lss = dynamic_cast<logfile_sub_source*>(tss);
        if (lss != nullptr) {
            auto min_level = lss->get_min_log_level();

            if (min_level != LEVEL_UNKNOWN) {
                fmt::print(file,
                           FMT_STRING(":set-min-log-level {}"),
                           level_names[min_level]);
            }

            struct timeval min_time, max_time;
            char tsbuf[128];
            if (lss->get_min_log_time(min_time)) {
                sql_strftime(tsbuf, sizeof(tsbuf), min_time, 'T');
                fmt::print(file, FMT_STRING(":hide-lines-before {}"), tsbuf);
            }
            if (lss->get_max_log_time(max_time)) {
                sql_strftime(tsbuf, sizeof(tsbuf), max_time, 'T');
                fmt::print(file, FMT_STRING(":hide-lines-after {}"), tsbuf);
            }
        }

        if (!tc.get_current_search().empty()) {
            fmt::print(file, FMT_STRING("/{}\n"), tc.get_current_search());
        }

        fmt::print(file, FMT_STRING(":goto {}\n"), (int) tc.get_top());
    }

    return Ok();
}

}  // namespace session
}  // namespace lnav
