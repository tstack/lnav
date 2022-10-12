/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file session_data.cc
 */

#include <algorithm>
#include <utility>

#include "session_data.hh"

#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <sys/types.h>
#include <yajl/api/yajl_tree.h>

#include "base/fs_util.hh"
#include "base/isc.hh"
#include "base/opt_util.hh"
#include "base/paths.hh"
#include "command_executor.hh"
#include "config.h"
#include "lnav.events.hh"
#include "lnav.hh"
#include "lnav_util.hh"
#include "log_format_ext.hh"
#include "logfile.hh"
#include "service_tags.hh"
#include "sql_util.hh"
#include "sqlitepp.client.hh"
#include "tailer/tailer.looper.hh"
#include "vtab_module.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

struct session_data_t session_data;

static const char* LOG_METADATA_NAME = "log_metadata.db";

static const char* META_TABLE_DEF = R"(
CREATE TABLE IF NOT EXISTS bookmarks (
    log_time datetime,
    log_format varchar(64),
    log_hash varchar(128),
    session_time integer,
    part_name text,
    access_time datetime DEFAULT CURRENT_TIMESTAMP,
    comment text DEFAULT '',
    tags text DEFAULT '',

    PRIMARY KEY (log_time, log_format, log_hash, session_time)
);

CREATE TABLE IF NOT EXISTS time_offset (
    log_time datetime,
    log_format varchar(64),
    log_hash varchar(128),
    session_time integer,
    offset_sec integer,
    offset_usec integer,
    access_time datetime DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (log_time, log_format, log_hash, session_time)
);

CREATE TABLE IF NOT EXISTS recent_netlocs (
    netloc text,

    access_time datetime DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (netloc)
);

CREATE TABLE IF NOT EXISTS regex101_entries (
    format_name text NOT NULL,
    regex_name text NOT NULL,
    permalink text NOT NULL,
    delete_code text NOT NULL,

    PRIMARY KEY (format_name, regex_name),

    CHECK(
       format_name  <> '' AND
       regex_name   <> '' AND
       permalink    <> '')
);
)";

static const char* BOOKMARK_LRU_STMT
    = "DELETE FROM bookmarks WHERE access_time <= "
      "  (SELECT access_time FROM bookmarks "
      "   ORDER BY access_time DESC LIMIT 1 OFFSET 50000)";

static const char* NETLOC_LRU_STMT
    = "DELETE FROM recent_netlocs WHERE access_time <= "
      "  (SELECT access_time FROM bookmarks "
      "   ORDER BY access_time DESC LIMIT 1 OFFSET 10)";

static const char* UPGRADE_STMTS[] = {
    R"(ALTER TABLE bookmarks ADD COLUMN comment text DEFAULT '';)",
    R"(ALTER TABLE bookmarks ADD COLUMN tags text DEFAULT '';)",
};

static const size_t MAX_SESSIONS = 8;
static const size_t MAX_SESSION_FILE_COUNT = 256;

static std::vector<content_line_t> marked_session_lines;
static std::vector<content_line_t> offset_session_lines;

static bool
bind_line(sqlite3* db,
          sqlite3_stmt* stmt,
          content_line_t cl,
          time_t session_time)
{
    logfile_sub_source& lss = lnav_data.ld_log_source;
    auto lf = lss.find(cl);

    if (lf == nullptr) {
        return false;
    }

    sqlite3_clear_bindings(stmt);

    auto line_iter = lf->begin() + cl;
    auto read_result = lf->read_line(line_iter);

    if (read_result.isErr()) {
        return false;
    }

    auto line_hash = read_result
                         .map([cl](auto sbr) {
                             return hasher()
                                 .update(sbr.get_data(), sbr.length())
                                 .update(cl)
                                 .to_string();
                         })
                         .unwrap();

    return bind_values(stmt,
                       lf->original_line_time(line_iter),
                       lf->get_format()->get_name(),
                       line_hash,
                       session_time)
        == SQLITE_OK;
}

struct session_file_info {
    session_file_info(int timestamp, std::string id, std::string path)
        : sfi_timestamp(timestamp), sfi_id(std::move(id)),
          sfi_path(std::move(path)){};

    bool operator<(const session_file_info& other) const
    {
        if (this->sfi_timestamp < other.sfi_timestamp) {
            return true;
        }
        if (this->sfi_path < other.sfi_path) {
            return true;
        }
        return false;
    };

    int sfi_timestamp;
    std::string sfi_id;
    std::string sfi_path;
};

static void
cleanup_session_data()
{
    static_root_mem<glob_t, globfree> session_file_list;
    std::list<struct session_file_info> session_info_list;
    std::map<std::string, int> session_count;
    auto session_file_pattern = lnav::paths::dotlnav() / "*-*.ts*.json";

    if (glob(
            session_file_pattern.c_str(), 0, nullptr, session_file_list.inout())
        == 0)
    {
        for (size_t lpc = 0; lpc < session_file_list->gl_pathc; lpc++) {
            const char* path = session_file_list->gl_pathv[lpc];
            char hash_id[64];
            int timestamp;
            const char* base;

            base = strrchr(path, '/');
            if (base == nullptr) {
                continue;
            }
            base += 1;
            if (sscanf(base, "file-%63[^.].ts%d.json", hash_id, &timestamp)
                == 2)
            {
                session_count[hash_id] += 1;
                session_info_list.emplace_back(timestamp, hash_id, path);
            }
            if (sscanf(base,
                       "view-info-%63[^.].ts%d.ppid%*d.json",
                       hash_id,
                       &timestamp)
                == 2)
            {
                session_count[hash_id] += 1;
                session_info_list.emplace_back(timestamp, hash_id, path);
            }
        }
    }

    session_info_list.sort();

    size_t session_loops = 0;

    while (session_info_list.size() > MAX_SESSION_FILE_COUNT) {
        const session_file_info& front = session_info_list.front();

        session_loops += 1;
        if (session_loops < MAX_SESSION_FILE_COUNT
            && session_count[front.sfi_id] == 1)
        {
            session_info_list.splice(session_info_list.end(),
                                     session_info_list,
                                     session_info_list.begin());
        } else {
            if (remove(front.sfi_path.c_str()) != 0) {
                log_error("Unable to remove session file: %s -- %s",
                          front.sfi_path.c_str(),
                          strerror(errno));
            }
            session_count[front.sfi_id] -= 1;
            session_info_list.pop_front();
        }
    }

    session_info_list.sort();

    while (session_info_list.size() > MAX_SESSION_FILE_COUNT) {
        const session_file_info& front = session_info_list.front();

        if (remove(front.sfi_path.c_str()) != 0) {
            log_error("Unable to remove session file: %s -- %s",
                      front.sfi_path.c_str(),
                      strerror(errno));
        }
        session_count[front.sfi_id] -= 1;
        session_info_list.pop_front();
    }
}

void
init_session()
{
    lnav_data.ld_session_time = time(nullptr);
    lnav_data.ld_session_id.clear();
}

static nonstd::optional<std::string>
compute_session_id()
{
    bool has_files = false;
    hasher h;

    for (auto& ld_file_name : lnav_data.ld_active_files.fc_file_names) {
        if (!ld_file_name.second.loo_include_in_session) {
            continue;
        }
        has_files = true;
        h.update(ld_file_name.first);
    }
    if (!has_files) {
        return nonstd::nullopt;
    }

    return h.to_string();
}

nonstd::optional<session_pair_t>
scan_sessions()
{
    static_root_mem<glob_t, globfree> view_info_list;

    cleanup_session_data();

    const auto session_id = compute_session_id();
    if (!session_id) {
        return nonstd::nullopt;
    }
    std::list<session_pair_t>& session_file_names
        = lnav_data.ld_session_id[session_id.value()];

    session_file_names.clear();

    auto view_info_pattern_base
        = fmt::format(FMT_STRING("view-info-{}.*.json"), session_id.value());
    auto view_info_pattern = lnav::paths::dotlnav() / view_info_pattern_base;
    if (glob(view_info_pattern.c_str(), 0, nullptr, view_info_list.inout())
        == 0)
    {
        for (size_t lpc = 0; lpc < view_info_list->gl_pathc; lpc++) {
            const char* path = view_info_list->gl_pathv[lpc];
            int timestamp, ppid, rc;
            const char* base;

            base = strrchr(path, '/');
            if (base == nullptr) {
                continue;
            }
            base += 1;
            if ((rc = sscanf(base,
                             "view-info-%*[^.].ts%d.ppid%d.json",
                             &timestamp,
                             &ppid))
                == 2)
            {
                ppid_time_pair_t ptp;

                ptp.first = (ppid == getppid()) ? 1 : 0;
                ptp.second = timestamp;
                session_file_names.emplace_back(ptp, path);
            }
        }
    }

    session_file_names.sort();

    while (session_file_names.size() > MAX_SESSIONS) {
        const std::string& name = session_file_names.front().second;

        if (remove(name.c_str()) != 0) {
            log_error("Unable to remove session: %s -- %s",
                      name.c_str(),
                      strerror(errno));
        }
        session_file_names.pop_front();
    }

    if (session_file_names.empty()) {
        return nonstd::nullopt;
    }

    return nonstd::make_optional(session_file_names.back());
}

void
load_time_bookmarks()
{
    logfile_sub_source& lss = lnav_data.ld_log_source;
    auto_sqlite3 db;
    auto db_path = lnav::paths::dotlnav() / LOG_METADATA_NAME;
    auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
    logfile_sub_source::iterator file_iter;
    bool reload_needed = false;
    auto_mem<char, sqlite3_free> errmsg;

    log_info("loading bookmark db: %s", db_path.c_str());

    if (sqlite3_open(db_path.c_str(), db.out()) != SQLITE_OK) {
        return;
    }

    for (const char* upgrade_stmt : UPGRADE_STMTS) {
        auto rc = sqlite3_exec(
            db.in(), upgrade_stmt, nullptr, nullptr, errmsg.out());
        if (rc != SQLITE_OK) {
            auto exterr = sqlite3_extended_errcode(db.in());
            log_error("unable to upgrade bookmark table -- (%d/%d) %s",
                      rc,
                      exterr,
                      errmsg.in());
        }
    }

    {
        auto netloc_prep_res
            = prepare_stmt(db.in(), "SELECT netloc FROM recent_netlocs");
        if (netloc_prep_res.isErr()) {
            log_error("unable to get netlocs: %s",
                      netloc_prep_res.unwrapErr().c_str());
            return;
        }

        auto netloc_stmt = netloc_prep_res.unwrap();
        bool done = false;

        while (!done) {
            done = netloc_stmt.fetch_row<std::string>().match(
                [](const std::string& netloc) {
                    session_data.sd_recent_netlocs.insert(netloc);
                    return false;
                },
                [](const prepared_stmt::fetch_error& fe) {
                    log_error("failed to fetch netloc row: %s",
                              fe.fe_msg.c_str());
                    return true;
                },
                [](prepared_stmt::end_of_rows) { return true; });
        }
    }

    if (sqlite3_prepare_v2(
            db.in(),
            "SELECT log_time, log_format, log_hash, session_time, part_name, "
            "access_time, comment,"
            " tags, session_time=? as same_session FROM bookmarks WHERE "
            " log_time between ? and ? and log_format = ? "
            " ORDER BY same_session DESC, session_time DESC",
            -1,
            stmt.out(),
            nullptr)
        != SQLITE_OK)
    {
        log_error("could not prepare bookmark select statement -- %s",
                  sqlite3_errmsg(db));
        return;
    }

    for (file_iter = lnav_data.ld_log_source.begin();
         file_iter != lnav_data.ld_log_source.end();
         ++file_iter)
    {
        auto lf = (*file_iter)->get_file();
        content_line_t base_content_line;

        if (lf == nullptr) {
            continue;
        }

        base_content_line = lss.get_file_base_content_line(file_iter);

        auto low_line_iter = lf->begin();
        auto high_line_iter = lf->end();

        --high_line_iter;

        if (bind_values(stmt.in(),
                        lnav_data.ld_session_load_time,
                        lf->original_line_time(low_line_iter),
                        lf->original_line_time(high_line_iter),
                        lf->get_format()->get_name())
            != SQLITE_OK)
        {
            return;
        }

        date_time_scanner dts;
        bool done = false;
        std::string line;
        int64_t last_mark_time = -1;

        while (!done) {
            int rc = sqlite3_step(stmt.in());

            switch (rc) {
                case SQLITE_OK:
                case SQLITE_DONE:
                    done = true;
                    break;

                case SQLITE_ROW: {
                    const char* log_time
                        = (const char*) sqlite3_column_text(stmt.in(), 0);
                    const char* log_hash
                        = (const char*) sqlite3_column_text(stmt.in(), 2);
                    const char* part_name
                        = (const char*) sqlite3_column_text(stmt.in(), 4);
                    const char* comment
                        = (const char*) sqlite3_column_text(stmt.in(), 6);
                    const char* tags
                        = (const char*) sqlite3_column_text(stmt.in(), 7);
                    int64_t mark_time = sqlite3_column_int64(stmt.in(), 3);
                    struct timeval log_tv;
                    struct exttm log_tm;

                    if (last_mark_time == -1) {
                        last_mark_time = mark_time;
                    } else if (last_mark_time != mark_time) {
                        done = true;
                        continue;
                    }

                    if (part_name == nullptr) {
                        continue;
                    }

                    if (!dts.scan(
                            log_time, strlen(log_time), NULL, &log_tm, log_tv))
                    {
                        continue;
                    }

                    auto line_iter
                        = lower_bound(lf->begin(), lf->end(), log_tv);
                    while (line_iter != lf->end()) {
                        struct timeval line_tv = line_iter->get_timeval();

                        if ((line_tv.tv_sec != log_tv.tv_sec)
                            || (line_tv.tv_usec != log_tv.tv_usec))
                        {
                            break;
                        }

                        auto cl = content_line_t(
                            std::distance(lf->begin(), line_iter));
                        auto read_result = lf->read_line(line_iter);

                        if (read_result.isErr()) {
                            break;
                        }

                        auto sbr = read_result.unwrap();

                        auto line_hash
                            = hasher()
                                  .update(sbr.get_data(), sbr.length())
                                  .update(cl)
                                  .to_string();

                        if (line_hash == log_hash) {
                            auto& bm_meta = lf->get_bookmark_metadata();
                            auto line_number = static_cast<uint32_t>(
                                std::distance(lf->begin(), line_iter));
                            content_line_t line_cl = content_line_t(
                                base_content_line + line_number);
                            bool meta = false;

                            if (part_name != nullptr && part_name[0] != '\0') {
                                lss.set_user_mark(&textview_curses::BM_META,
                                                  line_cl);
                                bm_meta[line_number].bm_name = part_name;
                                meta = true;
                            }
                            if (comment != nullptr && comment[0] != '\0') {
                                lss.set_user_mark(&textview_curses::BM_META,
                                                  line_cl);
                                bm_meta[line_number].bm_comment = comment;
                                meta = true;
                            }
                            if (tags != nullptr && tags[0] != '\0') {
                                auto_mem<yajl_val_s> tag_list(yajl_tree_free);
                                char error_buffer[1024];

                                tag_list = yajl_tree_parse(
                                    tags, error_buffer, sizeof(error_buffer));
                                if (!YAJL_IS_ARRAY(tag_list.in())) {
                                    log_error("invalid tags column: %s", tags);
                                } else {
                                    lss.set_user_mark(&textview_curses::BM_META,
                                                      line_cl);
                                    for (size_t lpc = 0;
                                         lpc < tag_list.in()->u.array.len;
                                         lpc++)
                                    {
                                        yajl_val elem
                                            = tag_list.in()
                                                  ->u.array.values[lpc];

                                        if (!YAJL_IS_STRING(elem)) {
                                            continue;
                                        }
                                        bookmark_metadata::KNOWN_TAGS.insert(
                                            elem->u.string);
                                        bm_meta[line_number].add_tag(
                                            elem->u.string);
                                    }
                                }
                                meta = true;
                            }
                            if (!meta) {
                                marked_session_lines.push_back(line_cl);
                                lss.set_user_mark(&textview_curses::BM_USER,
                                                  line_cl);
                            }
                            reload_needed = true;
                        }

                        ++line_iter;
                    }
                    break;
                }

                default: {
                    const char* errmsg;

                    errmsg = sqlite3_errmsg(lnav_data.ld_db);
                    log_error(
                        "bookmark select error: code %d -- %s", rc, errmsg);
                    done = true;
                } break;
            }
        }

        sqlite3_reset(stmt.in());
    }

    if (sqlite3_prepare_v2(
            db.in(),
            "SELECT *,session_time=? as same_session FROM time_offset WHERE "
            " log_time between ? and ? and log_format = ? "
            " ORDER BY same_session DESC, session_time DESC",
            -1,
            stmt.out(),
            nullptr)
        != SQLITE_OK)
    {
        log_error("could not prepare time_offset select statement -- %s",
                  sqlite3_errmsg(db));
        return;
    }

    for (file_iter = lnav_data.ld_log_source.begin();
         file_iter != lnav_data.ld_log_source.end();
         ++file_iter)
    {
        auto lf = (*file_iter)->get_file();
        content_line_t base_content_line;

        if (lf == nullptr) {
            continue;
        }

        lss.find(lf->get_filename().c_str(), base_content_line);

        auto low_line_iter = lf->begin();
        auto high_line_iter = lf->end();

        --high_line_iter;

        if (bind_values(stmt.in(),
                        lnav_data.ld_session_load_time,
                        lf->original_line_time(low_line_iter),
                        lf->original_line_time(high_line_iter),
                        lf->get_format()->get_name())
            != SQLITE_OK)
        {
            return;
        }

        date_time_scanner dts;
        bool done = false;
        std::string line;
        int64_t last_mark_time = -1;

        while (!done) {
            int rc = sqlite3_step(stmt.in());

            switch (rc) {
                case SQLITE_OK:
                case SQLITE_DONE:
                    done = true;
                    break;

                case SQLITE_ROW: {
                    const char* log_time
                        = (const char*) sqlite3_column_text(stmt.in(), 0);
                    const char* log_hash
                        = (const char*) sqlite3_column_text(stmt.in(), 2);
                    int64_t mark_time = sqlite3_column_int64(stmt.in(), 3);
                    struct timeval log_tv;
                    struct exttm log_tm;

                    if (last_mark_time == -1) {
                        last_mark_time = mark_time;
                    } else if (last_mark_time != mark_time) {
                        done = true;
                        continue;
                    }

                    if (sqlite3_column_type(stmt.in(), 4) == SQLITE_NULL) {
                        continue;
                    }

                    if (!dts.scan(log_time,
                                  strlen(log_time),
                                  nullptr,
                                  &log_tm,
                                  log_tv))
                    {
                        continue;
                    }

                    auto line_iter
                        = lower_bound(lf->begin(), lf->end(), log_tv);
                    while (line_iter != lf->end()) {
                        struct timeval line_tv = line_iter->get_timeval();

                        if ((line_tv.tv_sec != log_tv.tv_sec)
                            || (line_tv.tv_usec != log_tv.tv_usec))
                        {
                            break;
                        }

                        if (lf->get_content_id() == log_hash) {
                            int file_line
                                = std::distance(lf->begin(), line_iter);
                            content_line_t line_cl
                                = content_line_t(base_content_line + file_line);
                            struct timeval offset;

                            offset_session_lines.push_back(line_cl);
                            offset.tv_sec = sqlite3_column_int64(stmt.in(), 4);
                            offset.tv_usec = sqlite3_column_int64(stmt.in(), 5);
                            lf->adjust_content_time(file_line, offset);

                            reload_needed = true;
                        }

                        ++line_iter;
                    }
                    break;
                }

                default: {
                    const char* errmsg;

                    errmsg = sqlite3_errmsg(lnav_data.ld_db);
                    log_error(
                        "bookmark select error: code %d -- %s", rc, errmsg);
                    done = true;
                } break;
            }
        }

        sqlite3_reset(stmt.in());
    }

    if (reload_needed) {
        lnav_data.ld_views[LNV_LOG].reload_data();
    }
}

static int
read_files(yajlpp_parse_context* ypc, const unsigned char* str, size_t len)
{
    return 1;
}

static int
read_current_search(yajlpp_parse_context* ypc,
                    const unsigned char* str,
                    size_t len)
{
    const auto regex = std::string((const char*) str, len);
    const char** view_name;
    int view_index;

    view_name = find(lnav_view_strings,
                     lnav_view_strings + LNV__MAX,
                     ypc->get_path_fragment(-2));
    view_index = view_name - lnav_view_strings;

    if (view_index < LNV__MAX && !regex.empty()) {
        lnav_data.ld_views[view_index].execute_search(regex);
        lnav_data.ld_views[view_index].set_follow_search_for(-1, {});
    }

    return 1;
}

static int
read_top_line(yajlpp_parse_context* ypc, long long value)
{
    const char** view_name;
    int view_index;

    view_name = find(lnav_view_strings,
                     lnav_view_strings + LNV__MAX,
                     ypc->get_path_fragment(-2));
    view_index = view_name - lnav_view_strings;
    if (view_index < LNV__MAX) {
        session_data.sd_view_states[view_index].vs_top = value;
    }

    return 1;
}

static int
read_word_wrap(yajlpp_parse_context* ypc, int value)
{
    const char** view_name;
    int view_index;

    view_name = find(lnav_view_strings,
                     lnav_view_strings + LNV__MAX,
                     ypc->get_path_fragment(-2));
    view_index = view_name - lnav_view_strings;
    if (view_index == LNV_HELP) {
    } else if (view_index < LNV__MAX) {
        textview_curses& tc = lnav_data.ld_views[view_index];

        tc.set_word_wrap(value);
    }

    return 1;
}

static int
read_filtering(yajlpp_parse_context* ypc, int value)
{
    const char** view_name;
    int view_index;

    view_name = find(lnav_view_strings,
                     lnav_view_strings + LNV__MAX,
                     ypc->get_path_fragment(-2));
    view_index = view_name - lnav_view_strings;
    if (view_index == LNV_HELP) {
    } else if (view_index < LNV__MAX) {
        textview_curses& tc = lnav_data.ld_views[view_index];

        if (tc.get_sub_source() != nullptr) {
            tc.get_sub_source()->tss_apply_filters = value;
        }
    }

    return 1;
}

static int
read_commands(yajlpp_parse_context* ypc, const unsigned char* str, size_t len)
{
    std::string cmdline = std::string((const char*) str, len);
    const char** view_name;
    int view_index;

    view_name = find(lnav_view_strings,
                     lnav_view_strings + LNV__MAX,
                     ypc->get_path_fragment(-3));
    view_index = view_name - lnav_view_strings;
    bool active = ensure_view(&lnav_data.ld_views[view_index]);
    execute_command(lnav_data.ld_exec_context, cmdline);
    if (!active) {
        lnav_data.ld_view_stack.pop_back();
    }

    return 1;
}

static const struct json_path_container view_def_handlers = {
    json_path_handler("top_line", read_top_line),
    json_path_handler("search", read_current_search),
    json_path_handler("word_wrap", read_word_wrap),
    json_path_handler("filtering", read_filtering),
    json_path_handler("commands#", read_commands),
};

static const struct json_path_container view_handlers = {
    yajlpp::pattern_property_handler("([^/]+)").with_children(
        view_def_handlers),
};

static const struct json_path_container file_state_handlers = {
    yajlpp::property_handler("visible")
        .with_description("Indicates whether the file is visible or not")
        .for_field(&file_state::fs_is_visible),
};

static const struct json_path_container file_states_handlers = {
    yajlpp::pattern_property_handler(R"((?<filename>[^/]+))")
        .with_description("Map of file names to file state objects")
        .with_obj_provider<file_state, void>([](const auto& ypc, auto* root) {
            auto fn = ypc.get_substr("filename");
            return &session_data.sd_file_states[fn];
        })
        .with_children(file_state_handlers),
};

static const struct json_path_container view_info_handlers = {
    yajlpp::property_handler("save-time")
        .for_field(&session_data_t::sd_save_time),
    yajlpp::property_handler("time-offset")
        .for_field(&session_data_t::sd_time_offset),
    json_path_handler("files#", read_files),
    yajlpp::property_handler("file-states").with_children(file_states_handlers),
    yajlpp::property_handler("views").with_children(view_handlers),
};

void
load_session()
{
    load_time_bookmarks();
    scan_sessions() | [](const auto pair) {
        yajl_handle handle;
        auto_fd fd;

        lnav_data.ld_session_load_time = pair.first.second;
        session_data.sd_save_time = pair.first.second;
        const auto& view_info_path = pair.second;

        yajlpp_parse_context ypc(intern_string::lookup(view_info_path.string()),
                                 &view_info_handlers);
        ypc.with_obj(session_data);
        handle = yajl_alloc(&ypc.ypc_callbacks, nullptr, &ypc);

        load_time_bookmarks();

        if ((fd = lnav::filesystem::openp(view_info_path, O_RDONLY)) < 0) {
            perror("cannot open session file");
        } else {
            unsigned char buffer[1024];
            ssize_t rc;

            log_info("loading session file: %s", view_info_path.c_str());
            while ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
                yajl_parse(handle, buffer, rc);
            }
            yajl_complete_parse(handle);
        }
        yajl_free(handle);

        bool log_changes = false, text_changes = false;

        for (auto& lf : lnav_data.ld_active_files.fc_files) {
            auto iter = session_data.sd_file_states.find(lf->get_filename());

            if (iter == session_data.sd_file_states.end()) {
                continue;
            }

            log_debug("found state for file: %s %d",
                      lf->get_content_id().c_str(),
                      iter->second.fs_is_visible);
            lnav_data.ld_log_source.find_data(lf) | [iter](auto ld) {
                ld->set_visibility(iter->second.fs_is_visible);
            };
            if (!iter->second.fs_is_visible) {
                if (lf->get_format() != nullptr) {
                    log_changes = true;
                } else {
                    text_changes = true;
                }
            }
        }

        if (log_changes) {
            lnav_data.ld_log_source.text_filters_changed();
        }
        if (text_changes) {
            lnav_data.ld_text_source.text_filters_changed();
        }
    };

    lnav::events::publish(lnav_data.ld_db.in(),
                          lnav::events::session::loaded{});
}

static void
yajl_writer(void* context, const char* str, size_t len)
{
    FILE* file = (FILE*) context;

    fwrite(str, len, 1, file);
}

static void
save_user_bookmarks(sqlite3* db,
                    sqlite3_stmt* stmt,
                    bookmark_vector<content_line_t>& user_marks)
{
    logfile_sub_source& lss = lnav_data.ld_log_source;
    bookmark_vector<content_line_t>::iterator iter;

    for (iter = user_marks.begin(); iter != user_marks.end(); ++iter) {
        content_line_t cl = *iter;
        auto line_meta_opt = lss.find_bookmark_metadata(cl);
        if (!bind_line(db, stmt, cl, lnav_data.ld_session_time)) {
            continue;
        }

        if (!line_meta_opt) {
            if (sqlite3_bind_text(stmt, 5, "", 0, SQLITE_TRANSIENT)
                != SQLITE_OK)
            {
                log_error("could not bind log hash -- %s", sqlite3_errmsg(db));
                return;
            }
        } else {
            bookmark_metadata& line_meta = *(line_meta_opt.value());
            if (line_meta.empty()) {
                continue;
            }

            if (sqlite3_bind_text(stmt,
                                  5,
                                  line_meta.bm_name.c_str(),
                                  line_meta.bm_name.length(),
                                  SQLITE_TRANSIENT)
                != SQLITE_OK)
            {
                log_error("could not bind part name -- %s", sqlite3_errmsg(db));
                return;
            }

            if (sqlite3_bind_text(stmt,
                                  6,
                                  line_meta.bm_comment.c_str(),
                                  line_meta.bm_comment.length(),
                                  SQLITE_TRANSIENT)
                != SQLITE_OK)
            {
                log_error("could not bind comment -- %s", sqlite3_errmsg(db));
                return;
            }

            std::string tags;

            if (!line_meta.bm_tags.empty()) {
                yajlpp_gen gen;

                yajl_gen_config(gen, yajl_gen_beautify, false);

                {
                    yajlpp_array arr(gen);

                    for (const auto& str : line_meta.bm_tags) {
                        arr.gen(str);
                    }
                }

                tags = gen.to_string_fragment().to_string();
            }

            if (sqlite3_bind_text(
                    stmt, 7, tags.c_str(), tags.length(), SQLITE_TRANSIENT)
                != SQLITE_OK)
            {
                log_error("could not bind tags -- %s", sqlite3_errmsg(db));
                return;
            }
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            log_error("could not execute bookmark insert statement -- %s",
                      sqlite3_errmsg(db));
            return;
        }

        marked_session_lines.push_back(cl);

        sqlite3_reset(stmt);
    }
}

static void
save_time_bookmarks()
{
    auto_sqlite3 db;
    auto db_path = lnav::paths::dotlnav() / LOG_METADATA_NAME;
    auto_mem<char, sqlite3_free> errmsg;
    auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);

    if (sqlite3_open(db_path.c_str(), db.out()) != SQLITE_OK) {
        log_error("unable to open bookmark DB -- %s", db_path.c_str());
        return;
    }

    if (sqlite3_exec(db.in(), META_TABLE_DEF, nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("unable to make bookmark table -- %s", errmsg.in());
        return;
    }

    if (sqlite3_exec(
            db.in(), "BEGIN TRANSACTION", nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("unable to begin transaction -- %s", errmsg.in());
        return;
    }

    {
        static const char* UPDATE_NETLOCS_STMT
            = R"(REPLACE INTO recent_netlocs (netloc) VALUES (?))";

        std::set<std::string> netlocs;

        isc::to<tailer::looper&, services::remote_tailer_t>().send_and_wait(
            [&netlocs](auto& tlooper) { netlocs = tlooper.active_netlocs(); });

        if (sqlite3_prepare_v2(
                db.in(), UPDATE_NETLOCS_STMT, -1, stmt.out(), nullptr)
            != SQLITE_OK)
        {
            log_error("could not prepare recent_netlocs statement -- %s",
                      sqlite3_errmsg(db));
            return;
        }

        for (const auto& netloc : netlocs) {
            bind_to_sqlite(stmt.in(), 1, netloc);

            if (sqlite3_step(stmt.in()) != SQLITE_DONE) {
                log_error("could not execute bookmark insert statement -- %s",
                          sqlite3_errmsg(db));
                return;
            }

            sqlite3_reset(stmt.in());
        }
        session_data.sd_recent_netlocs.insert(netlocs.begin(), netlocs.end());
    }

    logfile_sub_source& lss = lnav_data.ld_log_source;
    bookmarks<content_line_t>::type& bm = lss.get_user_bookmarks();

    if (sqlite3_prepare_v2(db.in(),
                           "DELETE FROM bookmarks WHERE "
                           " log_time = ? and log_format = ? and log_hash = ? "
                           " and session_time = ?",
                           -1,
                           stmt.out(),
                           nullptr)
        != SQLITE_OK)
    {
        log_error("could not prepare bookmark delete statement -- %s",
                  sqlite3_errmsg(db));
        return;
    }

    for (auto& marked_session_line : marked_session_lines) {
        if (!bind_line(db.in(),
                       stmt.in(),
                       marked_session_line,
                       lnav_data.ld_session_time))
        {
            continue;
        }

        if (sqlite3_step(stmt.in()) != SQLITE_DONE) {
            log_error("could not execute bookmark insert statement -- %s",
                      sqlite3_errmsg(db));
            return;
        }

        sqlite3_reset(stmt.in());
    }

    marked_session_lines.clear();

    if (sqlite3_prepare_v2(db.in(),
                           "REPLACE INTO bookmarks"
                           " (log_time, log_format, log_hash, session_time, "
                           "part_name, comment, tags)"
                           " VALUES (?, ?, ?, ?, ?, ?, ?)",
                           -1,
                           stmt.out(),
                           nullptr)
        != SQLITE_OK)
    {
        log_error("could not prepare bookmark replace statement -- %s",
                  sqlite3_errmsg(db));
        return;
    }

    {
        logfile_sub_source::iterator file_iter;

        for (file_iter = lnav_data.ld_log_source.begin();
             file_iter != lnav_data.ld_log_source.end();
             ++file_iter)
        {
            auto lf = (*file_iter)->get_file();

            if (lf == nullptr) {
                continue;
            }

            content_line_t base_content_line;
            base_content_line = lss.get_file_base_content_line(file_iter);
            base_content_line
                = content_line_t(base_content_line + lf->size() - 1);

            if (!bind_line(db.in(),
                           stmt.in(),
                           base_content_line,
                           lnav_data.ld_session_time))
            {
                continue;
            }

            if (sqlite3_bind_null(stmt.in(), 5) != SQLITE_OK) {
                log_error("could not bind log hash -- %s",
                          sqlite3_errmsg(db.in()));
                return;
            }

            if (sqlite3_step(stmt.in()) != SQLITE_DONE) {
                log_error("could not execute bookmark insert statement -- %s",
                          sqlite3_errmsg(db));
                return;
            }

            sqlite3_reset(stmt.in());
        }
    }

    save_user_bookmarks(db.in(), stmt.in(), bm[&textview_curses::BM_USER]);
    save_user_bookmarks(db.in(), stmt.in(), bm[&textview_curses::BM_META]);

    if (sqlite3_prepare_v2(db.in(),
                           "DELETE FROM time_offset WHERE "
                           " log_time = ? and log_format = ? and log_hash = ? "
                           " and session_time = ?",
                           -1,
                           stmt.out(),
                           NULL)
        != SQLITE_OK)
    {
        log_error("could not prepare time_offset delete statement -- %s",
                  sqlite3_errmsg(db));
        return;
    }

    for (auto& offset_session_line : offset_session_lines) {
        if (!bind_line(db.in(),
                       stmt.in(),
                       offset_session_line,
                       lnav_data.ld_session_time))
        {
            continue;
        }

        if (sqlite3_step(stmt.in()) != SQLITE_DONE) {
            log_error("could not execute bookmark insert statement -- %s",
                      sqlite3_errmsg(db));
            return;
        }

        sqlite3_reset(stmt.in());
    }

    offset_session_lines.clear();

    if (sqlite3_prepare_v2(db.in(),
                           "REPLACE INTO time_offset"
                           " (log_time, log_format, log_hash, session_time, "
                           "offset_sec, offset_usec)"
                           " VALUES (?, ?, ?, ?, ?, ?)",
                           -1,
                           stmt.out(),
                           NULL)
        != SQLITE_OK)
    {
        log_error("could not prepare time_offset replace statement -- %s",
                  sqlite3_errmsg(db));
        return;
    }

    {
        logfile_sub_source::iterator file_iter;

        for (file_iter = lnav_data.ld_log_source.begin();
             file_iter != lnav_data.ld_log_source.end();
             ++file_iter)
        {
            auto lf = (*file_iter)->get_file();
            content_line_t base_content_line;

            if (lf == nullptr) {
                continue;
            }

            base_content_line = lss.get_file_base_content_line(file_iter);

            if (!bind_values(stmt,
                             lf->original_line_time(lf->begin()),
                             lf->get_format()->get_name(),
                             lf->get_content_id(),
                             lnav_data.ld_session_time))
            {
                continue;
            }

            if (sqlite3_bind_null(stmt.in(), 5) != SQLITE_OK) {
                log_error("could not bind log hash -- %s",
                          sqlite3_errmsg(db.in()));
                return;
            }

            if (sqlite3_bind_null(stmt.in(), 6) != SQLITE_OK) {
                log_error("could not bind log hash -- %s",
                          sqlite3_errmsg(db.in()));
                return;
            }

            if (sqlite3_step(stmt.in()) != SQLITE_DONE) {
                log_error("could not execute bookmark insert statement -- %s",
                          sqlite3_errmsg(db));
                return;
            }

            sqlite3_reset(stmt.in());
        }
    }

    for (auto& ls : lss) {
        if (ls->get_file() == nullptr) {
            continue;
        }

        auto lf = ls->get_file();

        if (!lf->is_time_adjusted()) {
            continue;
        }

        auto line_iter = lf->begin() + lf->get_time_offset_line();
        struct timeval offset = lf->get_time_offset();

        auto read_result = lf->read_line(line_iter);

        if (read_result.isErr()) {
            return;
        }

        bind_values(stmt.in(),
                    lf->original_line_time(line_iter),
                    lf->get_format()->get_name(),
                    lf->get_content_id(),
                    lnav_data.ld_session_time,
                    offset.tv_sec,
                    offset.tv_usec);

        if (sqlite3_step(stmt.in()) != SQLITE_DONE) {
            log_error("could not execute bookmark insert statement -- %s",
                      sqlite3_errmsg(db));
            return;
        }

        sqlite3_reset(stmt.in());
    }

    if (sqlite3_exec(db.in(), "COMMIT", nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("unable to begin transaction -- %s", errmsg.in());
        return;
    }

    if (sqlite3_exec(db.in(), BOOKMARK_LRU_STMT, nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("unable to delete old bookmarks -- %s", errmsg.in());
        return;
    }

    if (sqlite3_exec(db.in(), NETLOC_LRU_STMT, nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("unable to delete old netlocs -- %s", errmsg.in());
        return;
    }
}

static void
save_session_with_id(const std::string& session_id)
{
    auto_mem<FILE> file(fclose);
    yajl_gen handle = nullptr;

    /* TODO: save the last search query */

    log_info("saving session with id: %s", session_id.c_str());

    auto view_base_name
        = fmt::format(FMT_STRING("view-info-{}.ts{}.ppid{}.json"),
                      session_id,
                      lnav_data.ld_session_time,
                      getppid());
    auto view_file_name = lnav::paths::dotlnav() / view_base_name;
    auto view_file_tmp_name = view_file_name.string() + ".tmp";

    if ((file = fopen(view_file_tmp_name.c_str(), "w")) == nullptr) {
        perror("Unable to open session file");
    } else if (nullptr == (handle = yajl_gen_alloc(nullptr))) {
        perror("Unable to create yajl_gen object");
    } else {
        yajl_gen_config(
            handle, yajl_gen_print_callback, yajl_writer, file.in());

        {
            yajlpp_map root_map(handle);

            root_map.gen("save-time");
            root_map.gen((long long) time(nullptr));

            root_map.gen("time-offset");
            root_map.gen(lnav_data.ld_log_source.is_time_offset_enabled());

            root_map.gen("files");

            {
                yajlpp_array file_list(handle);

                for (auto& ld_file_name :
                     lnav_data.ld_active_files.fc_file_names)
                {
                    file_list.gen(ld_file_name.first);
                }
            }

            root_map.gen("file-states");

            {
                yajlpp_map file_states(handle);

                for (auto& lf : lnav_data.ld_active_files.fc_files) {
                    auto ld_opt = lnav_data.ld_log_source.find_data(lf);

                    file_states.gen(lf->get_filename());

                    {
                        yajlpp_map file_state(handle);

                        file_state.gen("visible");
                        file_state.gen(!ld_opt || ld_opt.value()->ld_visible);
                    }
                }
            }

            root_map.gen("views");

            {
                yajlpp_map top_view_map(handle);

                for (int lpc = 0; lpc < LNV__MAX; lpc++) {
                    textview_curses& tc = lnav_data.ld_views[lpc];
                    unsigned long width;
                    vis_line_t height;

                    top_view_map.gen(lnav_view_strings[lpc]);

                    yajlpp_map view_map(handle);

                    view_map.gen("top_line");

                    tc.get_dimensions(height, width);
                    if (tc.get_top() >= tc.get_top_for_last_row()) {
                        view_map.gen(-1LL);
                    } else {
                        view_map.gen((long long) tc.get_top());
                    }

                    view_map.gen("search");
                    view_map.gen(lnav_data.ld_views[lpc].get_current_search());

                    view_map.gen("word_wrap");
                    view_map.gen(tc.get_word_wrap());

                    auto tss = tc.get_sub_source();
                    if (tss == nullptr) {
                        continue;
                    }

                    view_map.gen("filtering");
                    view_map.gen(tss->tss_apply_filters);

                    filter_stack& fs = tss->get_filters();

                    view_map.gen("commands");
                    yajlpp_array cmd_array(handle);

                    for (const auto& filter : fs) {
                        auto cmd = filter->to_command();

                        if (cmd.empty()) {
                            continue;
                        }

                        cmd_array.gen(cmd);

                        if (!filter->is_enabled()) {
                            cmd_array.gen("disable-filter " + filter->get_id());
                        }
                    }

                    auto& hmap = lnav_data.ld_views[lpc].get_highlights();

                    for (auto& hl : hmap) {
                        if (hl.first.first != highlight_source_t::INTERACTIVE) {
                            continue;
                        }
                        cmd_array.gen("highlight " + hl.first.second);
                    }

                    if (lpc == LNV_LOG) {
                        for (const auto& format :
                             log_format::get_root_formats())
                        {
                            auto* elf = dynamic_cast<external_log_format*>(
                                format.get());

                            if (elf == nullptr) {
                                continue;
                            }

                            for (const auto& vd : elf->elf_value_defs) {
                                if (!vd.second->vd_meta.lvm_user_hidden) {
                                    continue;
                                }

                                cmd_array.gen("hide-fields "
                                              + elf->get_name().to_string()
                                              + "." + vd.first.to_string());
                            }
                        }

                        logfile_sub_source& lss = lnav_data.ld_log_source;

                        struct timeval min_time, max_time;
                        bool have_min_time = lss.get_min_log_time(min_time);
                        bool have_max_time = lss.get_max_log_time(max_time);
                        char min_time_str[32], max_time_str[32];

                        sql_strftime(
                            min_time_str, sizeof(min_time_str), min_time);
                        if (have_min_time) {
                            cmd_array.gen("hide-lines-before "
                                          + std::string(min_time_str));
                        }
                        if (have_max_time) {
                            sql_strftime(
                                max_time_str, sizeof(max_time_str), max_time);
                            cmd_array.gen("hide-lines-after "
                                          + std::string(max_time_str));
                        }

                        auto mark_expr = lss.get_sql_marker_text();
                        if (!mark_expr.empty()) {
                            cmd_array.gen("mark-expr " + mark_expr);
                        }
                    }
                }
            }
        }

        yajl_gen_clear(handle);
        yajl_gen_free(handle);

        fclose(file.release());

        log_perror(rename(view_file_tmp_name.c_str(), view_file_name.c_str()));

        log_info("Saved session: %s", view_file_name.c_str());
    }
}

void
save_session()
{
    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        log_info("secure mode is enabled, not saving session");
        return;
    }

    log_debug("BEGIN save_session");
    save_time_bookmarks();

    const auto opt_session_id = compute_session_id();
    opt_session_id | [](auto& session_id) { save_session_with_id(session_id); };
    for (const auto& pair : lnav_data.ld_session_id) {
        if (opt_session_id && pair.first == opt_session_id.value()) {
            continue;
        }
        save_session_with_id(pair.first);
    }
    log_debug("END save_session");
}

void
reset_session()
{
    log_info("reset session: time=%d", lnav_data.ld_session_time);

    save_session();

    lnav_data.ld_session_time = time(nullptr);
    session_data.sd_file_states.clear();

    for (auto& tc : lnav_data.ld_views) {
        auto& hmap = tc.get_highlights();
        auto hl_iter = hmap.begin();

        while (hl_iter != hmap.end()) {
            if (hl_iter->first.first != highlight_source_t::INTERACTIVE) {
                ++hl_iter;
            } else {
                hmap.erase(hl_iter++);
            }
        }
    }

    for (const auto& lf : lnav_data.ld_active_files.fc_files) {
        lf->reset_state();
    }

    lnav_data.ld_log_source.set_marked_only(false);
    lnav_data.ld_log_source.clear_min_max_log_times();
    lnav_data.ld_log_source.set_min_log_level(LEVEL_UNKNOWN);
    lnav_data.ld_log_source.set_sql_filter("", nullptr);
    lnav_data.ld_log_source.set_sql_marker("", nullptr);

    lnav_data.ld_log_source.clear_bookmark_metadata();

    for (auto& tc : lnav_data.ld_views) {
        text_sub_source* tss = tc.get_sub_source();

        if (tss == nullptr) {
            continue;
        }
        tss->get_filters().clear_filters();
        tss->tss_apply_filters = true;
        tss->text_filters_changed();
        tss->text_clear_marks(&textview_curses::BM_USER);
        tc.get_bookmarks()[&textview_curses::BM_USER].clear();
        tss->text_clear_marks(&textview_curses::BM_META);
        tc.get_bookmarks()[&textview_curses::BM_META].clear();
        tc.reload_data();
    }

    lnav_data.ld_filter_view.reload_data();
    lnav_data.ld_files_view.reload_data();
    for (const auto& format : log_format::get_root_formats()) {
        auto* elf = dynamic_cast<external_log_format*>(format.get());

        if (elf == nullptr) {
            continue;
        }

        for (const auto& vd : elf->elf_value_defs) {
            vd.second->vd_meta.lvm_user_hidden = false;
        }
    }
}

void
lnav::session::regex101::insert_entry(const lnav::session::regex101::entry& ei)
{
    constexpr const char* STMT = R"(
       INSERT INTO regex101_entries
          (format_name, regex_name, permalink, delete_code)
          VALUES (?, ?, ?, ?);
)";

    auto db_path = lnav::paths::dotlnav() / LOG_METADATA_NAME;
    auto_sqlite3 db;

    if (sqlite3_open(db_path.c_str(), db.out()) != SQLITE_OK) {
        return;
    }

    auto_mem<char, sqlite3_free> errmsg;
    if (sqlite3_exec(db.in(), META_TABLE_DEF, nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("unable to make bookmark table -- %s", errmsg.in());
        return;
    }

    auto prep_res = prepare_stmt(db.in(),
                                 STMT,
                                 ei.re_format_name,
                                 ei.re_regex_name,
                                 ei.re_permalink,
                                 ei.re_delete_code);

    if (prep_res.isErr()) {
        return;
    }

    auto ps = prep_res.unwrap();

    ps.execute();
}

template<>
struct from_sqlite<lnav::session::regex101::entry> {
    inline lnav::session::regex101::entry operator()(int argc,
                                                     sqlite3_value** argv,
                                                     int argi)
    {
        return {
            from_sqlite<std::string>()(argc, argv, argi + 0),
            from_sqlite<std::string>()(argc, argv, argi + 1),
            from_sqlite<std::string>()(argc, argv, argi + 2),
            from_sqlite<std::string>()(argc, argv, argi + 3),
        };
    }
};

Result<std::vector<lnav::session::regex101::entry>, std::string>
lnav::session::regex101::get_entries()
{
    constexpr const char* STMT = R"(
       SELECT * FROM regex101_entries;
)";

    auto db_path = lnav::paths::dotlnav() / LOG_METADATA_NAME;
    auto_sqlite3 db;

    if (sqlite3_open(db_path.c_str(), db.out()) != SQLITE_OK) {
        return Err(std::string());
    }

    auto_mem<char, sqlite3_free> errmsg;
    if (sqlite3_exec(db.in(), META_TABLE_DEF, nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("unable to make bookmark table -- %s", errmsg.in());
        return Err(std::string(errmsg));
    }

    auto ps = TRY(prepare_stmt(db.in(), STMT));
    bool done = false;
    std::vector<entry> retval;

    while (!done) {
        auto fetch_res = ps.fetch_row<entry>();

        if (fetch_res.is<prepared_stmt::fetch_error>()) {
            return Err(fetch_res.get<prepared_stmt::fetch_error>().fe_msg);
        }

        fetch_res.match(
            [&done](const prepared_stmt::end_of_rows&) { done = true; },
            [](const prepared_stmt::fetch_error&) {},
            [&retval](entry en) { retval.emplace_back(en); });
    }
    return Ok(retval);
}

void
lnav::session::regex101::delete_entry(const std::string& format_name,
                                      const std::string& regex_name)
{
    constexpr const char* STMT = R"(
       DELETE FROM regex101_entries WHERE
          format_name = ? AND regex_name = ?;
)";

    auto db_path = lnav::paths::dotlnav() / LOG_METADATA_NAME;
    auto_sqlite3 db;

    if (sqlite3_open(db_path.c_str(), db.out()) != SQLITE_OK) {
        return;
    }

    auto prep_res = prepare_stmt(db.in(), STMT, format_name, regex_name);

    if (prep_res.isErr()) {
        return;
    }

    auto ps = prep_res.unwrap();

    ps.execute();
}

lnav::session::regex101::get_result_t
lnav::session::regex101::get_entry(const std::string& format_name,
                                   const std::string& regex_name)
{
    constexpr const char* STMT = R"(
       SELECT * FROM regex101_entries WHERE
          format_name = ? AND regex_name = ?;
    )";

    auto db_path = lnav::paths::dotlnav() / LOG_METADATA_NAME;
    auto_sqlite3 db;

    if (sqlite3_open(db_path.c_str(), db.out()) != SQLITE_OK) {
        return error{std::string()};
    }

    auto_mem<char, sqlite3_free> errmsg;
    if (sqlite3_exec(db.in(), META_TABLE_DEF, nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("unable to make bookmark table -- %s", errmsg.in());
        return error{std::string(errmsg)};
    }

    auto prep_res = prepare_stmt(db.in(), STMT, format_name, regex_name);
    if (prep_res.isErr()) {
        return error{prep_res.unwrapErr()};
    }

    auto ps = prep_res.unwrap();
    return ps.fetch_row<entry>().match(
        [](const prepared_stmt::fetch_error& fe) -> get_result_t {
            return error{fe.fe_msg};
        },
        [](const prepared_stmt::end_of_rows&) -> get_result_t {
            return no_entry{};
        },
        [](const entry& en) -> get_result_t { return en; });
}
