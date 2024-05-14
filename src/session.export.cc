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
    std::optional<std::string> lmss_comment;
    std::optional<std::string> lmss_tags;
    std::optional<std::string> lmss_annotations;
    std::optional<std::string> lmss_opid;
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
            from_sqlite<std::optional<std::string>>()(argc, argv, argi + 3),
            from_sqlite<std::optional<std::string>>()(argc, argv, argi + 4),
            from_sqlite<std::optional<std::string>>()(argc, argv, argi + 5),
            from_sqlite<std::optional<std::string>>()(argc, argv, argi + 6),
            from_sqlite<std::string>()(argc, argv, argi + 7),
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

struct log_file_session_state {
    std::string lfss_content_id;
    std::string lfss_format;
    int64_t lfss_time_offset;
};

template<>
struct from_sqlite<log_file_session_state> {
    inline log_file_session_state operator()(int argc,
                                             sqlite3_value** argv,
                                             int argi)
    {
        return {
            from_sqlite<std::string>()(argc, argv, argi + 0),
            from_sqlite<std::string>()(argc, argv, argi + 1),
            from_sqlite<int64_t>()(argc, argv, argi + 2),
        };
    }
};

namespace lnav {
namespace session {

static std::optional<ghc::filesystem::path>
find_container_dir(ghc::filesystem::path file_path)
{
    if (!ghc::filesystem::exists(file_path)) {
        return std::nullopt;
    }

    std::optional<ghc::filesystem::path> dir_with_last_readme;

    while (file_path.has_parent_path()
           && file_path != file_path.root_directory())
    {
        auto parent = file_path.parent_path();
        bool has_readme_entry = false;
        std::error_code ec;

        for (const auto& entry :
             ghc::filesystem::directory_iterator(parent, ec))
        {
            if (!entry.is_regular_file()) {
                continue;
            }

            auto entry_filename = tolower(entry.path().filename().string());
            if (startswith(entry_filename, "readme")) {
                has_readme_entry = true;
                dir_with_last_readme = parent;
            }
        }
        if (!has_readme_entry && dir_with_last_readme) {
            return dir_with_last_readme;
        }

        file_path = parent;
    }

    return std::nullopt;
}

static std::string
replace_home_dir(std::string path)
{
    auto home_dir_opt = getenv_opt("HOME");

    if (!home_dir_opt) {
        return path;
    }

    const auto* home_dir = home_dir_opt.value();

    if (startswith(path, home_dir)) {
        auto retval = path.substr(strlen(home_dir));

        if (retval.front() != '/') {
            retval.insert(0, "/");
        }
        retval.insert(0, "$HOME");
        return retval;
    }

    return path;
}

Result<void, lnav::console::user_message>
export_to(FILE* file)
{
    static auto& lnav_db = injector::get<auto_sqlite3&>();

    static const char* BOOKMARK_QUERY = R"(
SELECT log_time_msecs, log_format, log_mark, log_comment, log_tags, log_annotations, log_user_opid, log_line_hash
   FROM all_logs
   WHERE log_mark = 1 OR
         log_comment IS NOT NULL OR
         log_tags IS NOT NULL OR
         log_annotations IS NOT NULL OR
         (log_user_opid IS NOT NULL AND log_user_opid != '')
)";

    static const char* FILTER_QUERY = R"(
SELECT view_name, enabled, type, language, pattern FROM lnav_view_filters
)";

    static const char* FILE_QUERY = R"(
SELECT content_id, format, time_offset FROM lnav_file
  WHERE format IS NOT NULL AND time_offset != 0
)";

    static constexpr const char HEADER[] = R"(#!lnav -Nf
# This file is an export of an lnav session.  You can type
# '|/path/to/this/file' in lnav to execute this file and
# restore the state of the session.

;SELECT raise_error('This session export was made with a newer version of lnav, please upgrade to ' || {0} || ' or later')
   WHERE lnav_version() < {0} COLLATE naturalcase

# The files loaded into the session were:

)";

    static constexpr const char LOG_DIR_INSERT[] = R"(
# Set this environment variable to override this value or edit this script.
;INSERT OR IGNORE INTO environ (name, value) VALUES ('LOG_DIR_{}', {})
)";

    static constexpr const char MARK_HEADER[] = R"(

# The following SQL statements will restore the bookmarks,
# comments, and tags that were added in the session.

;SELECT total_changes() AS before_mark_changes
)";

    static constexpr const char MARK_FOOTER[] = R"(
;SELECT {} - (total_changes() - $before_mark_changes) AS failed_mark_changes
;SELECT echoln(printf('%sERROR%s: failed to restore %d bookmarks',
                      $ansi_red, $ansi_norm, $failed_mark_changes))
    WHERE $failed_mark_changes != 0
)";

    static const char* FILTER_HEADER = R"(

# The following SQL statements will restore the filters that
# were added in the session.

)";

    static const char* FILE_HEADER = R"(

# The following SQL statements will restore the state of the
# files in the session.

;SELECT total_changes() AS before_file_changes
)";

    static constexpr const char FILE_FOOTER[] = R"(
;SELECT {} - (total_changes() - $before_file_changes) AS failed_file_changes
;SELECT echoln(printf('%sERROR%s: failed to restore the state of %d files',
                      $ansi_red, $ansi_norm, $failed_file_changes))
   WHERE $failed_file_changes != 0
)";

    static constexpr const char VIEW_HEADER[] = R"(

# The following commands will restore the state of the {} view.

)";

    auto prep_mark_res = prepare_stmt(lnav_db.in(), BOOKMARK_QUERY);
    if (prep_mark_res.isErr()) {
        return Err(
            console::user_message::error("unable to export log bookmarks")
                .with_reason(prep_mark_res.unwrapErr()));
    }

    fmt::print(file, FMT_STRING(HEADER), sqlitepp::quote(PACKAGE_VERSION).in());

    std::map<std::string, std::vector<std::string>> file_containers;
    std::set<std::string> raw_files;
    for (const auto& name_pair : lnav_data.ld_active_files.fc_file_names) {
        const auto& open_opts = name_pair.second;

        if (!open_opts.loo_is_visible || !open_opts.loo_include_in_session
            || open_opts.loo_source != logfile_name_source::USER)
        {
            continue;
        }

        auto file_path_str = name_pair.first;
        auto file_path = ghc::filesystem::path(file_path_str);
        auto container_path_opt = find_container_dir(file_path);
        if (container_path_opt) {
            auto container_parent = container_path_opt.value().parent_path();
            auto file_container_path
                = ghc::filesystem::relative(file_path, container_parent)
                      .string();
            file_containers[container_parent.string()].push_back(
                file_container_path);
        } else {
            raw_files.insert(file_path_str);
        }
    }
    for (const auto& file_path_str : raw_files) {
        fmt::print(
            file, FMT_STRING(":open {}\n"), replace_home_dir(file_path_str));
    }
    size_t container_index = 0;
    for (const auto& container_pair : file_containers) {
        fmt::print(file,
                   FMT_STRING(LOG_DIR_INSERT),
                   container_index,
                   sqlitepp::quote(container_pair.first).in());
        for (const auto& file_path_str : container_pair.second) {
            fmt::print(file,
                       FMT_STRING(":open $LOG_DIR_{}/{}\n"),
                       container_index,
                       file_path_str);
        }
        container_index += 1;
    }

    fmt::print(file, FMT_STRING("\n:rebuild\n"));

    auto mark_count = 0;
    auto each_mark_res
        = prep_mark_res.unwrap().for_each_row<log_message_session_state>(
            [file, &mark_count](const log_message_session_state& lmss) {
                if (mark_count == 0) {
                    fmt::print(file, FMT_STRING(MARK_HEADER));
                }
                mark_count += 1;
                fmt::print(file,
                           FMT_STRING(";UPDATE all_logs "
                                      "SET log_mark = {}, "
                                      "log_comment = {}, "
                                      "log_tags = {}, "
                                      "log_annotations = {}, "
                                      "log_opid = {} "
                                      "WHERE log_time_msecs = {} AND "
                                      "log_format = {} AND "
                                      "log_line_hash = {}\n"),
                           lmss.lmss_mark ? "1" : "0",
                           sqlitepp::quote(lmss.lmss_comment).in(),
                           sqlitepp::quote(lmss.lmss_tags).in(),
                           sqlitepp::quote(lmss.lmss_annotations).in(),
                           sqlitepp::quote(lmss.lmss_opid).in(),
                           lmss.lmss_time_msecs,
                           sqlitepp::quote(lmss.lmss_format).in(),
                           sqlitepp::quote(lmss.lmss_hash).in());
                return false;
            });

    if (each_mark_res.isErr()) {
        return Err(console::user_message::error(
                       "failed to fetch bookmark metadata for log message")
                       .with_reason(each_mark_res.unwrapErr().fe_msg));
    }

    if (mark_count > 0) {
        fmt::print(file, FMT_STRING(MARK_FOOTER), mark_count);
    }

    auto prep_filter_res = prepare_stmt(lnav_db.in(), FILTER_QUERY);
    if (prep_filter_res.isErr()) {
        return Err(console::user_message::error("unable to export filter state")
                       .with_reason(prep_filter_res.unwrapErr()));
    }

    auto added_filter_header = false;
    auto each_filter_res
        = prep_filter_res.unwrap().for_each_row<log_filter_session_state>(
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
                    sqlitepp::quote(lfss.lfss_name).in(),
                    lfss.lfss_enabled ? 1 : 0,
                    sqlitepp::quote(lfss.lfss_type).in(),
                    sqlitepp::quote(lfss.lfss_language).in(),
                    sqlitepp::quote(lfss.lfss_pattern).in());
                return false;
            });

    if (each_filter_res.isErr()) {
        return Err(console::user_message::error(
                       "failed to fetch filter state for views")
                       .with_reason(each_filter_res.unwrapErr().fe_msg));
    }

    auto prep_file_res = prepare_stmt(lnav_db.in(), FILE_QUERY);
    if (prep_file_res.isErr()) {
        return Err(console::user_message::error("unable to export file state")
                       .with_reason(prep_file_res.unwrapErr()));
    }

    auto file_count = 0;
    auto file_stmt = prep_file_res.unwrap();
    auto each_file_res = file_stmt.for_each_row<log_file_session_state>(
        [file, &file_count](const log_file_session_state& lfss) {
            if (file_count == 0) {
                fmt::print(file, FMT_STRING("{}"), FILE_HEADER);
            }
            file_count += 1;
            fmt::print(file,
                       FMT_STRING(";UPDATE lnav_file "
                                  "SET time_offset = {} "
                                  "WHERE content_id = {} AND format = {}\n"),
                       lfss.lfss_time_offset,
                       sqlitepp::quote(lfss.lfss_content_id).in(),
                       sqlitepp::quote(lfss.lfss_format).in());
            return false;
        });

    if (each_file_res.isErr()) {
        return Err(console::user_message::error("failed to fetch file state")
                       .with_reason(each_file_res.unwrapErr().fe_msg));
    }

    if (file_count > 0) {
        fmt::print(file, FMT_STRING(FILE_FOOTER), file_count);
    }

    for (auto view_index : {LNV_LOG, LNV_TEXT}) {
        auto& tc = lnav_data.ld_views[view_index];
        if (tc.get_inner_height() == 0_vl) {
            continue;
        }

        fmt::print(file, FMT_STRING(VIEW_HEADER), lnav_view_titles[view_index]);
        fmt::print(file,
                   FMT_STRING(":switch-to-view {}\n"),
                   lnav_view_strings[view_index]);

        auto* tss = tc.get_sub_source();
        auto* lss = dynamic_cast<logfile_sub_source*>(tss);
        if (lss != nullptr) {
            auto min_level = lss->get_min_log_level();

            if (min_level != LEVEL_UNKNOWN) {
                fmt::print(file,
                           FMT_STRING(":set-min-log-level {}\n"),
                           level_names[min_level]);
            }

            char tsbuf[128];
            auto min_time_opt = lss->get_min_log_time();
            if (min_time_opt) {
                sql_strftime(tsbuf, sizeof(tsbuf), min_time_opt.value(), 'T');
                fmt::print(file, FMT_STRING(":hide-lines-before {}\n"), tsbuf);
            }
            auto max_time_opt = lss->get_max_log_time();
            if (max_time_opt) {
                sql_strftime(tsbuf, sizeof(tsbuf), max_time_opt.value(), 'T');
                fmt::print(file, FMT_STRING(":hide-lines-after {}\n"), tsbuf);
            }
            for (const auto& ld : *lss) {
                if (ld->is_visible()) {
                    continue;
                }

                if (ld->get_file_ptr()->get_open_options().loo_source
                    == logfile_name_source::ARCHIVE)
                {
                    continue;
                }

                auto container_path_opt
                    = find_container_dir(ld->get_file_ptr()->get_path());
                if (!container_path_opt) {
                    fmt::print(file,
                               FMT_STRING(":hide-file {}\n"),
                               ld->get_file_ptr()->get_path().string());
                    continue;
                }
                auto container_parent
                    = container_path_opt.value().parent_path();
                auto file_container_path = ghc::filesystem::relative(
                    ld->get_file_ptr()->get_path(), container_parent);
                fmt::print(file,
                           FMT_STRING(":hide-file */{}\n"),
                           file_container_path.string());
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
