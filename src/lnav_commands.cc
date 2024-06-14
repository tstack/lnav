/**
 * Copyright (c) 2007-2022, Timothy Stack
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

#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "lnav.hh"

#include <fnmatch.h>
#include <glob.h>
#include <sys/stat.h>
#include <termios.h>

#include "base/attr_line.builder.hh"
#include "base/auto_mem.hh"
#include "base/fs_util.hh"
#include "base/humanize.hh"
#include "base/humanize.network.hh"
#include "base/injector.hh"
#include "base/isc.hh"
#include "base/itertools.hh"
#include "base/paths.hh"
#include "base/string_util.hh"
#include "bound_tags.hh"
#include "breadcrumb_curses.hh"
#include "command_executor.hh"
#include "config.h"
#include "curl_looper.hh"
#include "date/tz.h"
#include "db_sub_source.hh"
#include "external_opener.hh"
#include "field_overlay_source.hh"
#include "fmt/printf.h"
#include "hasher.hh"
#include "itertools.similar.hh"
#include "lnav.indexing.hh"
#include "lnav_commands.hh"
#include "lnav_config.hh"
#include "lnav_util.hh"
#include "log.annotate.hh"
#include "log_data_helper.hh"
#include "log_data_table.hh"
#include "log_format_loader.hh"
#include "log_search_table.hh"
#include "log_search_table_fwd.hh"
#include "ptimec.hh"
#include "readline_callbacks.hh"
#include "readline_curses.hh"
#include "readline_highlighters.hh"
#include "readline_possibilities.hh"
#include "relative_time.hh"
#include "scn/scn.h"
#include "service_tags.hh"
#include "session.export.hh"
#include "session_data.hh"
#include "shlex.hh"
#include "spectro_impls.hh"
#include "sqlite-extension-func.hh"
#include "sysclip.hh"
#include "tailer/tailer.looper.hh"
#include "text_anonymizer.hh"
#include "url_handler.cfg.hh"
#include "url_loader.hh"
#include "yajl/api/yajl_parse.h"
#include "yajlpp/json_op.hh"
#include "yajlpp/yajlpp.hh"

#if !CURL_AT_LEAST_VERSION(7, 80, 0)
extern "C"
{
const char* curl_url_strerror(CURLUcode error);
}
#endif

using namespace lnav::roles::literals;

inline attr_line_t&
symbol_reducer(const std::string& elem, attr_line_t& accum)
{
    return accum.append("\n   ").append(lnav::roles::symbol(elem));
}

static std::string
remaining_args(const std::string& cmdline,
               const std::vector<std::string>& args,
               size_t index = 1)
{
    size_t start_pos = 0;

    require(index > 0);

    if (index >= args.size()) {
        return "";
    }
    for (size_t lpc = 0; lpc < index; lpc++) {
        start_pos += args[lpc].length();
    }

    size_t index_in_cmdline = cmdline.find(args[index], start_pos);

    require(index_in_cmdline != std::string::npos);

    return cmdline.substr(index_in_cmdline);
}

static string_fragment
remaining_args_frag(const std::string& cmdline,
                    const std::vector<std::string>& args,
                    size_t index = 1)
{
    size_t start_pos = 0;

    require(index > 0);

    if (index >= args.size()) {
        return string_fragment{};
    }
    for (size_t lpc = 0; lpc < index; lpc++) {
        start_pos += args[lpc].length();
    }

    size_t index_in_cmdline = cmdline.find(args[index], start_pos);

    require(index_in_cmdline != std::string::npos);

    return string_fragment::from_str_range(
        cmdline, index_in_cmdline, cmdline.size());
}

static std::optional<std::string>
find_arg(std::vector<std::string>& args, const std::string& flag)
{
    auto iter = find_if(args.begin(), args.end(), [&flag](const auto elem) {
        return startswith(elem, flag);
    });

    if (iter == args.end()) {
        return std::nullopt;
    }

    auto index = iter->find('=');
    if (index == std::string::npos) {
        return "";
    }

    auto retval = iter->substr(index + 1);

    args.erase(iter);

    return retval;
}

static bookmark_vector<vis_line_t>
combined_user_marks(vis_bookmarks& vb)
{
    const auto& bv = vb[&textview_curses::BM_USER];
    const auto& bv_expr = vb[&textview_curses::BM_USER_EXPR];
    bookmark_vector<vis_line_t> retval;

    for (const auto& row : bv) {
        retval.insert_once(row);
    }
    for (const auto& row : bv_expr) {
        retval.insert_once(row);
    }
    return retval;
}

static Result<std::string, lnav::console::user_message>
com_adjust_log_time(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("line-time");
    } else if (lnav_data.ld_views[LNV_LOG].get_inner_height() == 0) {
        return ec.make_error("no log messages");
    } else if (args.size() >= 2) {
        auto& lss = lnav_data.ld_log_source;
        struct timeval top_time, time_diff;
        struct timeval new_time = {0, 0};
        content_line_t top_content;
        date_time_scanner dts;
        vis_line_t top_line;
        struct exttm tm;
        struct tm base_tm;
        std::shared_ptr<logfile> lf;

        top_line = lnav_data.ld_views[LNV_LOG].get_selection();
        top_content = lss.at(top_line);
        lf = lss.find(top_content);

        auto& ll = (*lf)[top_content];

        top_time = ll.get_timeval();
        localtime_r(&top_time.tv_sec, &base_tm);

        dts.set_base_time(top_time.tv_sec, base_tm);
        args[1] = remaining_args(cmdline, args);

        auto parse_res = relative_time::from_str(args[1]);
        if (parse_res.isOk()) {
            new_time = parse_res.unwrap().adjust(top_time).to_timeval();
        } else if (dts.scan(
                       args[1].c_str(), args[1].size(), nullptr, &tm, new_time)
                   != nullptr)
        {
            // nothing to do
        } else {
            return ec.make_error("could not parse timestamp -- {}", args[1]);
        }

        timersub(&new_time, &top_time, &time_diff);
        if (ec.ec_dry_run) {
            char buffer[1024];

            snprintf(
                buffer,
                sizeof(buffer),
                "info: log timestamps will be adjusted by %ld.%06ld seconds",
                time_diff.tv_sec,
                (long) time_diff.tv_usec);

            retval = buffer;
        } else {
            lf->adjust_content_time(top_content, time_diff, false);

            lss.set_force_rebuild();

            retval = "info: adjusted time";
        }
    } else {
        return ec.make_error("expecting new time value");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_unix_time(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (args.size() >= 2) {
        bool parsed = false;
        struct tm log_time;
        time_t u_time;
        size_t millis;
        char* rest;

        u_time = time(nullptr);
        if (localtime_r(&u_time, &log_time) == nullptr) {
            return ec.make_error(
                "invalid epoch time: {} -- {}", u_time, strerror(errno));
        }

        log_time.tm_isdst = -1;

        args[1] = remaining_args(cmdline, args);
        if ((millis = args[1].find('.')) != std::string::npos
            || (millis = args[1].find(',')) != std::string::npos)
        {
            args[1] = args[1].erase(millis, 4);
        }
        if (((rest = strptime(args[1].c_str(), "%b %d %H:%M:%S %Y", &log_time))
                 != nullptr
             && (rest - args[1].c_str()) >= 20)
            || ((rest
                 = strptime(args[1].c_str(), "%Y-%m-%d %H:%M:%S", &log_time))
                    != nullptr
                && (rest - args[1].c_str()) >= 19))
        {
            u_time = mktime(&log_time);
            parsed = true;
        } else if (sscanf(args[1].c_str(), "%ld", &u_time)) {
            if (localtime_r(&u_time, &log_time) == nullptr) {
                return ec.make_error(
                    "invalid epoch time: {} -- {}", args[1], strerror(errno));
            }

            parsed = true;
        }
        if (parsed) {
            char ftime[128];

            strftime(ftime,
                     sizeof(ftime),
                     "%a %b %d %H:%M:%S %Y  %z %Z",
                     localtime_r(&u_time, &log_time));
            retval = fmt::format(FMT_STRING("{} -- {}"), ftime, u_time);
        } else {
            return ec.make_error("invalid unix time -- {}", args[1]);
        }
    } else {
        return ec.make_error("expecting a unix time value");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_set_file_timezone(exec_context& ec,
                      std::string cmdline,
                      std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("args");
    std::string retval;

    if (args.empty()) {
        args.emplace_back("timezone");
        return Ok(retval);
    }

    if (args.size() == 1) {
        return ec.make_error("expecting a timezone name");
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    auto* lss = dynamic_cast<logfile_sub_source*>(tc->get_sub_source());

    if (lss != nullptr) {
        if (lss->text_line_count() == 0) {
            return ec.make_error("no log messages to examine");
        }

        auto line_pair = lss->find_line_with_file(lss->at(tc->get_selection()));
        if (!line_pair) {
            return ec.make_error(FMT_STRING("cannot find line: {}"),
                                 (int) tc->get_selection());
        }

        shlex lexer(cmdline);
        auto split_res = lexer.split(ec.create_resolver());
        if (split_res.isErr()) {
            auto split_err = split_res.unwrapErr();
            auto um = lnav::console::user_message::error(
                          "unable to parse arguments")
                          .with_reason(split_err.te_msg)
                          .with_snippet(lnav::console::snippet::from(
                              SRC, lexer.to_attr_line(split_err)))
                          .move();

            return Err(um);
        }

        auto split_args
            = split_res.unwrap() | lnav::itertools::map([](const auto& elem) {
                  return elem.se_value;
              });
        try {
            const auto* tz = date::locate_zone(split_args[1]);
            auto pattern = split_args.size() == 2
                ? line_pair->first->get_filename()
                : std::filesystem::path(split_args[2]);

            if (!ec.ec_dry_run) {
                static auto& safe_options_hier
                    = injector::get<lnav::safe_file_options_hier&>();

                safe::WriteAccess<lnav::safe_file_options_hier> options_hier(
                    safe_options_hier);

                options_hier->foh_generation += 1;
                auto& coll = options_hier->foh_path_to_collection["/"];

                log_info("setting timezone for %s to %s",
                         pattern.c_str(),
                         args[1].c_str());
                coll.foc_pattern_to_options[pattern] = lnav::file_options{
                    {intern_string_t{}, source_location{}, tz},
                };

                auto opt_path = lnav::paths::dotlnav() / "file-options.json";
                auto coll_str = coll.to_json();
                lnav::filesystem::write_file(opt_path, coll_str);
            }
        } catch (const std::runtime_error& e) {
            attr_line_t note;

            try {
                note = (date::get_tzdb().zones
                        | lnav::itertools::map(&date::time_zone::name)
                        | lnav::itertools::similar_to(split_args[1])
                        | lnav::itertools::fold(symbol_reducer, attr_line_t{}))
                           .add_header("did you mean one of the following?");
            } catch (const std::runtime_error& e) {
                log_error("unable to get timezones: %s", e.what());
            }
            auto um = lnav::console::user_message::error(
                          attr_line_t()
                              .append_quoted(split_args[1])
                              .append(" is not a valid timezone"))
                          .with_reason(e.what())
                          .with_note(note)
                          .move();
            return Err(um);
        }
    } else {
        return ec.make_error(
            ":set-file-timezone is only supported for the LOG view");
    }

    return Ok(retval);
}

static readline_context::prompt_result_t
com_set_file_timezone_prompt(exec_context& ec, const std::string& cmdline)
{
    auto* tc = *lnav_data.ld_view_stack.top();
    auto* lss = dynamic_cast<logfile_sub_source*>(tc->get_sub_source());

    if (lss == nullptr || lss->text_line_count() == 0) {
        return {};
    }

    shlex lexer(cmdline);
    auto split_res = lexer.split(ec.create_resolver());
    if (split_res.isErr()) {
        return {};
    }

    auto line_pair = lss->find_line_with_file(lss->at(tc->get_selection()));
    if (!line_pair) {
        return {};
    }

    auto elems = split_res.unwrap();
    auto pattern_arg = line_pair->first->get_filename();
    if (elems.size() == 1) {
        try {
            static auto& safe_options_hier
                = injector::get<lnav::safe_file_options_hier&>();

            safe::ReadAccess<lnav::safe_file_options_hier> options_hier(
                safe_options_hier);
            auto file_zone = date::get_tzdb().current_zone()->name();
            auto match_res = options_hier->match(pattern_arg);
            if (match_res) {
                file_zone = match_res->second.fo_default_zone.pp_value->name();
                pattern_arg = lnav::filesystem::escape_path(match_res->first);

                auto new_prompt = fmt::format(FMT_STRING("{} {} {}"),
                                              trim(cmdline),
                                              file_zone,
                                              pattern_arg);

                return {new_prompt};
            }

            return {"", file_zone + " "};
        } catch (const std::runtime_error& e) {
            log_error("cannot get timezones: %s", e.what());
        }
    }
    auto arg_path = std::filesystem::path(pattern_arg);
    auto arg_parent = lnav::filesystem::escape_path(arg_path.parent_path());
    if (!endswith(arg_parent, "/")) {
        arg_parent += "/";
    }
    if (elems.size() == 2 && endswith(cmdline, " ")) {
        return {"", arg_parent};
    }
    if (elems.size() == 3 && elems.back().se_value == arg_parent) {
        return {"", arg_path.filename().string()};
    }

    return {};
}

static readline_context::prompt_result_t
com_clear_file_timezone_prompt(exec_context& ec, const std::string& cmdline)
{
    std::string retval;

    auto* tc = *lnav_data.ld_view_stack.top();
    auto* lss = dynamic_cast<logfile_sub_source*>(tc->get_sub_source());

    if (lss != nullptr && lss->text_line_count() > 0) {
        auto line_pair = lss->find_line_with_file(lss->at(tc->get_selection()));
        if (line_pair) {
            try {
                static auto& safe_options_hier
                    = injector::get<lnav::safe_file_options_hier&>();

                safe::ReadAccess<lnav::safe_file_options_hier> options_hier(
                    safe_options_hier);
                auto file_zone = date::get_tzdb().current_zone()->name();
                auto pattern_arg = line_pair->first->get_filename();
                auto match_res
                    = options_hier->match(line_pair->first->get_filename());
                if (match_res) {
                    file_zone
                        = match_res->second.fo_default_zone.pp_value->name();
                    pattern_arg = match_res->first;
                }

                retval = fmt::format(
                    FMT_STRING("{} {}"), trim(cmdline), pattern_arg);
            } catch (const std::runtime_error& e) {
                log_error("cannot get timezones: %s", e.what());
            }
        }
    }

    return {retval};
}

static Result<std::string, lnav::console::user_message>
com_clear_file_timezone(exec_context& ec,
                        std::string cmdline,
                        std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("file-with-zone");
        return Ok(retval);
    }

    if (args.size() != 2) {
        return ec.make_error("expecting a single file path or pattern");
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    auto* lss = dynamic_cast<logfile_sub_source*>(tc->get_sub_source());

    if (lss != nullptr) {
        if (!ec.ec_dry_run) {
            static auto& safe_options_hier
                = injector::get<lnav::safe_file_options_hier&>();

            safe::WriteAccess<lnav::safe_file_options_hier> options_hier(
                safe_options_hier);

            options_hier->foh_generation += 1;
            auto& coll = options_hier->foh_path_to_collection["/"];
            const auto iter = coll.foc_pattern_to_options.find(args[1]);

            if (iter == coll.foc_pattern_to_options.end()) {
                return ec.make_error(FMT_STRING("no timezone set for: {}"),
                                     args[1]);
            }

            log_info("clearing timezone for %s", args[1].c_str());
            iter->second.fo_default_zone.pp_value = nullptr;
            if (iter->second.empty()) {
                coll.foc_pattern_to_options.erase(iter);
            }

            auto opt_path = lnav::paths::dotlnav() / "file-options.json";
            auto coll_str = coll.to_json();
            lnav::filesystem::write_file(opt_path, coll_str);
        }
    } else {
        return ec.make_error(
            ":clear-file-timezone is only supported for the LOG view");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_set_text_view_mode(exec_context& ec,
                       std::string cmdline,
                       std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("text-view-modes");
        return Ok(retval);
    }

    std::optional<textfile_sub_source::view_mode> vm_opt;

    if (args.size() > 1) {
        if (args[1] == "raw") {
            vm_opt = textfile_sub_source::view_mode::raw;
        } else if (args[1] == "rendered") {
            vm_opt = textfile_sub_source::view_mode::rendered;
        }
    }

    if (!vm_opt) {
        return ec.make_error("expecting a view mode of 'raw' or 'rendered'");
    }

    if (!ec.ec_dry_run) {
        lnav_data.ld_text_source.set_view_mode(vm_opt.value());
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_convert_time_to(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("timezone");
        return Ok(retval);
    }

    if (args.size() == 1) {
        return ec.make_error("expecting a timezone name");
    }

    const auto* tc = *lnav_data.ld_view_stack.top();
    auto* lss = dynamic_cast<logfile_sub_source*>(tc->get_sub_source());

    if (lss != nullptr) {
        if (lss->text_line_count() == 0) {
            return ec.make_error("no log messages to examine");
        }

        const auto* ll = lss->find_line(lss->at(tc->get_selection()));
        try {
            auto* dst_tz = date::locate_zone(args[1]);
            auto utime = date::local_time<std::chrono::seconds>{
                std::chrono::seconds{ll->get_time()}};
            auto cz_time = lnav::to_sys_time(utime);
            auto dz_time = date::make_zoned(dst_tz, cz_time);
            auto etime = std::chrono::duration_cast<std::chrono::seconds>(
                dz_time.get_local_time().time_since_epoch());
            char ftime[128];
            sql_strftime(
                ftime, sizeof(ftime), etime.count(), ll->get_millis(), 'T');
            retval = ftime;

            off_t off = 0;
            exttm tm;
            tm.et_flags |= ETF_ZONE_SET;
            tm.et_gmtoff = dz_time.get_info().offset.count();
            ftime_Z(ftime, off, sizeof(ftime), tm);
            ftime[off] = '\0';
            retval.append(" ");
            retval.append(ftime);
        } catch (const std::runtime_error& e) {
            return ec.make_error(FMT_STRING("Unable to get timezone: {} -- {}"),
                                 args[1],
                                 e.what());
        }
    } else {
        return ec.make_error(
            ":convert-time-to is only supported for the LOG view");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_current_time(exec_context& ec,
                 std::string cmdline,
                 std::vector<std::string>& args)
{
    char ftime[128];
    struct tm localtm;
    std::string retval;
    time_t u_time;

    memset(&localtm, 0, sizeof(localtm));
    u_time = time(nullptr);
    strftime(ftime,
             sizeof(ftime),
             "%a %b %d %H:%M:%S %Y  %z %Z",
             localtime_r(&u_time, &localtm));
    retval = fmt::format(FMT_STRING("{} -- {}"), ftime, u_time);

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_goto(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    static const char* INTERACTIVE_FMTS[] = {
        "%B %e %H:%M:%S",
        "%b %e %H:%M:%S",
        "%B %e %H:%M",
        "%b %e %H:%M",
        "%B %e %I:%M%p",
        "%b %e %I:%M%p",
        "%B %e %I%p",
        "%b %e %I%p",
        "%B %e",
        "%b %e",
        nullptr,
    };

    std::string retval;

    if (args.empty()) {
        args.emplace_back("move-args");
    } else if (args.size() > 1) {
        std::string all_args = remaining_args(cmdline, args);
        auto* tc = *lnav_data.ld_view_stack.top();
        std::optional<vis_line_t> dst_vl;
        auto is_location = false;

        if (startswith(all_args, "#")) {
            auto* ta = dynamic_cast<text_anchors*>(tc->get_sub_source());

            if (ta == nullptr) {
                return ec.make_error("view does not support anchor links");
            }

            dst_vl = ta->row_for_anchor(all_args);
            if (!dst_vl) {
                return ec.make_error("unable to find anchor: {}", all_args);
            }
            is_location = true;
        }

        auto* ttt = dynamic_cast<text_time_translator*>(tc->get_sub_source());
        int line_number, consumed;
        date_time_scanner dts;
        const char* scan_end = nullptr;
        struct timeval tv;
        struct exttm tm;
        float value;
        auto parse_res = relative_time::from_str(all_args);

        if (ttt != nullptr && tc->get_inner_height() > 0_vl) {
            auto top_time_opt = ttt->time_for_row(tc->get_selection());

            if (top_time_opt) {
                auto top_time_tv = top_time_opt.value().ri_time;
                struct tm top_tm;

                localtime_r(&top_time_tv.tv_sec, &top_tm);
                dts.set_base_time(top_time_tv.tv_sec, top_tm);
            }
        }

        if (dst_vl) {
        } else if (parse_res.isOk()) {
            if (ttt == nullptr) {
                return ec.make_error(
                    "relative time values only work in a time-indexed view");
            }
            if (tc->get_inner_height() == 0_vl) {
                return ec.make_error("view is empty");
            }
            auto tv_opt = ttt->time_for_row(tc->get_selection());
            if (!tv_opt) {
                return ec.make_error("cannot get time for the top row");
            }
            tv = tv_opt.value().ri_time;

            vis_line_t vl = tc->get_selection(), new_vl;
            bool done = false;
            auto rt = parse_res.unwrap();

            if (rt.is_relative()) {
                injector::get<relative_time&, last_relative_time_tag>() = rt;
            }

            do {
                auto tm = rt.adjust(tv);

                tv = tm.to_timeval();
                auto new_vl_opt = ttt->row_for_time(tv);
                if (!new_vl_opt) {
                    break;
                }

                new_vl = new_vl_opt.value();
                if (new_vl == 0_vl || new_vl != vl || !rt.is_relative()) {
                    vl = new_vl;
                    done = true;
                }
            } while (!done);

            dst_vl = vl;

            if (!ec.ec_dry_run && !rt.is_absolute()
                && lnav_data.ld_rl_view != nullptr)
            {
                lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                    r, R, "to move forward/backward the same amount of time"));
            }
        } else if ((scan_end = dts.scan(
                        all_args.c_str(), all_args.size(), nullptr, &tm, tv))
                       != nullptr
                   || (scan_end = dts.scan(all_args.c_str(),
                                           all_args.size(),
                                           INTERACTIVE_FMTS,
                                           &tm,
                                           tv))
                       != nullptr)
        {
            if (ttt == nullptr) {
                return ec.make_error(
                    "time values only work in a time-indexed view");
            }

            size_t matched_size = scan_end - all_args.c_str();
            if (matched_size != all_args.size()) {
                auto um
                    = lnav::console::user_message::error(
                          attr_line_t("invalid timestamp: ").append(all_args))
                          .with_reason(
                              attr_line_t("the leading part of the timestamp "
                                          "was matched, however, the trailing "
                                          "text ")
                                  .append_quoted(scan_end)
                                  .append(" was not"))
                          .with_snippets(ec.ec_source)
                          .with_note(
                              attr_line_t("input matched time format ")
                                  .append_quoted(
                                      PTIMEC_FORMATS[dts.dts_fmt_lock].pf_fmt))
                          .with_help(
                              "fix the timestamp or remove the trailing text")
                          .move();

                auto unmatched_size = all_args.size() - matched_size;
                auto& snippet_copy = um.um_snippets.back();
                attr_line_builder alb(snippet_copy.s_content);

                alb.append("\n")
                    .append(1 + cmdline.find(all_args), ' ')
                    .append(matched_size, ' ');
                {
                    auto attr_guard
                        = alb.with_attr(VC_ROLE.value(role_t::VCR_COMMENT));

                    alb.append("^");
                    if (unmatched_size > 1) {
                        if (unmatched_size > 2) {
                            alb.append(unmatched_size - 2, '-');
                        }
                        alb.append("^");
                    }
                    alb.append(" unrecognized input");
                }
                return Err(um);
            }

            if (!(tm.et_flags & ETF_DAY_SET)) {
                tm.et_tm.tm_mday = 1;
            }
            if (!(tm.et_flags & ETF_HOUR_SET)) {
                tm.et_tm.tm_hour = 0;
            }
            if (!(tm.et_flags & ETF_MINUTE_SET)) {
                tm.et_tm.tm_min = 0;
            }
            if (!(tm.et_flags & ETF_SECOND_SET)) {
                tm.et_tm.tm_sec = 0;
            }
            if (!(tm.et_flags & ETF_MICROS_SET)
                && !(tm.et_flags & ETF_MILLIS_SET))
            {
                tm.et_nsec = 0;
            }
            tv = tm.to_timeval();
            dst_vl = ttt->row_for_time(tv);
        } else if (sscanf(args[1].c_str(), "%f%n", &value, &consumed) == 1) {
            if (args[1][consumed] == '%') {
                line_number
                    = (int) ((double) tc->get_inner_height() * (value / 100.0));
            } else {
                line_number = (int) value;
                if (line_number < 0) {
                    line_number = tc->get_inner_height() + line_number;
                }
            }

            dst_vl = vis_line_t(line_number);
        } else {
            auto um = lnav::console::user_message::error(
                          attr_line_t("invalid argument: ").append(args[1]))
                          .with_reason(
                              "expecting line number/percentage, timestamp, or "
                              "relative time")
                          .move();
            ec.add_error_context(um);
            return Err(um);
        }

        dst_vl | [&ec, tc, &retval, is_location](auto new_top) {
            if (ec.ec_dry_run) {
                retval = "info: will move to line "
                    + std::to_string((int) new_top);
            } else {
                tc->get_sub_source()->get_location_history() |
                    [new_top](auto lh) { lh->loc_history_append(new_top); };
                tc->set_selection(new_top);
                if (tc->is_selectable() && is_location) {
                    tc->set_top(new_top - 2_vl, false);
                }

                retval = "";
            }
        };
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_relative_goto(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    std::string retval = "error: ";

    if (args.empty()) {
    } else if (args.size() > 1) {
        textview_curses* tc = *lnav_data.ld_view_stack.top();
        int line_offset, consumed;
        float value;

        if (sscanf(args[1].c_str(), "%f%n", &value, &consumed) == 1) {
            if (args[1][consumed] == '%') {
                line_offset
                    = (int) ((double) tc->get_inner_height() * (value / 100.0));
            } else {
                line_offset = (int) value;
            }

            if (ec.ec_dry_run) {
                retval = "info: shifting top by " + std::to_string(line_offset)
                    + " lines";
            } else {
                tc->set_selection(tc->get_selection()
                                  + vis_line_t(line_offset));

                retval = "";
            }
        } else {
            return ec.make_error("invalid line number -- {}", args[1]);
        }
    } else {
        return ec.make_error("expecting line number/percentage");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_annotate(exec_context& ec,
             std::string cmdline,
             std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        auto* tc = *lnav_data.ld_view_stack.top();
        auto* lss = dynamic_cast<logfile_sub_source*>(tc->get_sub_source());

        if (lss != nullptr) {
            auto sel = tc->get_selection();
            auto applicable_annos = lnav::log::annotate::applicable(sel);

            if (applicable_annos.empty()) {
                return ec.make_error(
                    "no annotations available for this log message");
            }

            auto apply_res = lnav::log::annotate::apply(sel, applicable_annos);
            if (apply_res.isErr()) {
                return Err(apply_res.unwrapErr());
            }
        } else {
            return ec.make_error(
                ":annotate is only supported for the LOG view");
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_mark(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty() || lnav_data.ld_view_stack.empty()) {
    } else if (!ec.ec_dry_run) {
        auto* tc = *lnav_data.ld_view_stack.top();
        lnav_data.ld_last_user_mark[tc] = tc->get_selection();
        tc->toggle_user_mark(&textview_curses::BM_USER,
                             vis_line_t(lnav_data.ld_last_user_mark[tc]));
        tc->reload_data();
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_mark_expr(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty() || lnav_data.ld_view_stack.empty()) {
        args.emplace_back("filter-expr-syms");
    } else if (args.size() < 2) {
        return ec.make_error("expecting an SQL expression");
    } else if (*lnav_data.ld_view_stack.top() != &lnav_data.ld_views[LNV_LOG]) {
        return ec.make_error(":mark-expr is only supported for the LOG view");
    } else {
        auto expr = remaining_args(cmdline, args);
        auto stmt_str = fmt::format(FMT_STRING("SELECT 1 WHERE {}"), expr);

        auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
#ifdef SQLITE_PREPARE_PERSISTENT
        auto retcode = sqlite3_prepare_v3(lnav_data.ld_db.in(),
                                          stmt_str.c_str(),
                                          stmt_str.size(),
                                          SQLITE_PREPARE_PERSISTENT,
                                          stmt.out(),
                                          nullptr);
#else
        auto retcode = sqlite3_prepare_v2(lnav_data.ld_db.in(),
                                          stmt_str.c_str(),
                                          stmt_str.size(),
                                          stmt.out(),
                                          nullptr);
#endif
        if (retcode != SQLITE_OK) {
            const char* errmsg = sqlite3_errmsg(lnav_data.ld_db);
            auto expr_al = attr_line_t(expr).with_attr_for_all(
                VC_ROLE.value(role_t::VCR_QUOTED_CODE));
            readline_sqlite_highlighter(expr_al, std::nullopt);
            auto um
                = lnav::console::user_message::error(
                      attr_line_t("invalid mark expression: ").append(expr_al))
                      .with_reason(errmsg)
                      .with_snippets(ec.ec_source)
                      .move();

            return Err(um);
        }

        auto& lss = lnav_data.ld_log_source;
        if (ec.ec_dry_run) {
            auto set_res = lss.set_preview_sql_filter(stmt.release());

            if (set_res.isErr()) {
                return Err(set_res.unwrapErr());
            }
            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "Matches are highlighted in the text view");
        } else {
            auto set_res = lss.set_sql_marker(expr, stmt.release());

            if (set_res.isErr()) {
                return Err(set_res.unwrapErr());
            }
        }
    }

    return Ok(retval);
}

static readline_context::prompt_result_t
com_mark_expr_prompt(exec_context& ec, const std::string& cmdline)
{
    textview_curses* tc = *lnav_data.ld_view_stack.top();

    if (tc != &lnav_data.ld_views[LNV_LOG]) {
        return {""};
    }

    return {
        fmt::format(FMT_STRING("{} {}"),
                    trim(cmdline),
                    trim(lnav_data.ld_log_source.get_sql_marker_text())),
    };
}

static Result<std::string, lnav::console::user_message>
com_clear_mark_expr(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else {
        if (!ec.ec_dry_run) {
            lnav_data.ld_log_source.set_sql_marker("", nullptr);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_goto_mark(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("mark-type");
    } else {
        static const std::set<const bookmark_type_t*> DEFAULT_TYPES = {
            &textview_curses::BM_USER,
            &textview_curses::BM_USER_EXPR,
            &textview_curses::BM_META,
        };

        textview_curses* tc = get_textview_for_mode(lnav_data.ld_mode);
        std::set<const bookmark_type_t*> mark_types;

        if (args.size() > 1) {
            for (size_t lpc = 1; lpc < args.size(); lpc++) {
                auto bt_opt = bookmark_type_t::find_type(args[lpc]);
                if (!bt_opt) {
                    auto um
                        = lnav::console::user_message::error(
                              attr_line_t("unknown bookmark type: ")
                                  .append(args[lpc]))
                              .with_snippets(ec.ec_source)
                              .with_help(
                                  attr_line_t("available types: ")
                                      .join(bookmark_type_t::get_all_types()
                                                | lnav::itertools::map(
                                                    &bookmark_type_t::get_name)
                                                | lnav::itertools::sorted(),
                                            ", "))
                              .move();
                    return Err(um);
                }
                mark_types.insert(bt_opt.value());
            }
        } else {
            mark_types = DEFAULT_TYPES;
        }

        if (!ec.ec_dry_run) {
            std::optional<vis_line_t> new_top;

            if (args[0] == "next-mark") {
                auto search_from_top = search_forward_from(tc);

                for (const auto& bt : mark_types) {
                    auto bt_top
                        = next_cluster(&bookmark_vector<vis_line_t>::next,
                                       bt,
                                       search_from_top);

                    if (bt_top && (!new_top || bt_top < new_top.value())) {
                        new_top = bt_top;
                    }
                }

                if (!new_top) {
                    auto um = lnav::console::user_message::info(fmt::format(
                        FMT_STRING("no more {} bookmarks after here"),
                        fmt::join(mark_types
                                      | lnav::itertools::map(
                                          &bookmark_type_t::get_name),
                                  ", ")));

                    return Err(um);
                }
            } else {
                for (const auto& bt : mark_types) {
                    auto bt_top
                        = next_cluster(&bookmark_vector<vis_line_t>::prev,
                                       bt,
                                       tc->get_selection());

                    if (bt_top && (!new_top || bt_top > new_top.value())) {
                        new_top = bt_top;
                    }
                }

                if (!new_top) {
                    auto um = lnav::console::user_message::info(fmt::format(
                        FMT_STRING("no more {} bookmarks before here"),
                        fmt::join(mark_types
                                      | lnav::itertools::map(
                                          &bookmark_type_t::get_name),
                                  ", ")));

                    return Err(um);
                }
            }

            if (new_top) {
                tc->get_sub_source()->get_location_history() |
                    [new_top](auto lh) {
                        lh->loc_history_append(new_top.value());
                    };
                tc->set_selection(new_top.value());
            }
            lnav_data.ld_bottom_source.grep_error("");
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_goto_location(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        lnav_data.ld_view_stack.top() | [&args](auto tc) {
            tc->get_sub_source()->get_location_history() |
                [tc, &args](auto lh) {
                    return args[0] == "prev-location"
                        ? lh->loc_history_back(tc->get_selection())
                        : lh->loc_history_forward(tc->get_selection());
                }
                | [tc](auto new_top) {
                      tc->set_selection(new_top);
                      if (tc->is_selectable()) {
                          tc->set_top(new_top - 2_vl, false);
                      }
                  };
        };
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_next_section(exec_context& ec,
                 std::string cmdline,
                 std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        auto* tc = *lnav_data.ld_view_stack.top();
        auto* ta = dynamic_cast<text_anchors*>(tc->get_sub_source());

        if (ta == nullptr) {
            return ec.make_error("view does not support sections");
        }

        auto adj_opt = ta->adjacent_anchor(tc->get_selection(),
                                           text_anchors::direction::next);
        if (!adj_opt) {
            return ec.make_error("no next section found");
        }

        tc->set_selection(adj_opt.value());
        if (tc->is_selectable()) {
            tc->set_top(adj_opt.value() - 2_vl, false);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_prev_section(exec_context& ec,
                 std::string cmdline,
                 std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        auto* tc = *lnav_data.ld_view_stack.top();
        auto* ta = dynamic_cast<text_anchors*>(tc->get_sub_source());

        if (ta == nullptr) {
            return ec.make_error("view does not support sections");
        }

        auto adj_opt = ta->adjacent_anchor(tc->get_selection(),
                                           text_anchors::direction::prev);
        if (!adj_opt) {
            return ec.make_error("no previous section found");
        }

        tc->set_selection(adj_opt.value());
        if (tc->is_selectable()) {
            tc->set_top(adj_opt.value() - 2_vl, false);
        }
    }

    return Ok(retval);
}

static bool
csv_needs_quoting(const std::string& str)
{
    return (str.find_first_of(",\"\r\n") != std::string::npos);
}

static std::string
csv_quote_string(const std::string& str)
{
    static const std::regex csv_column_quoter("\"");

    std::string retval = std::regex_replace(str, csv_column_quoter, "\"\"");

    retval.insert(0, 1, '\"');
    retval.append(1, '\"');

    return retval;
}

static void
csv_write_string(FILE* outfile, const std::string& str)
{
    if (csv_needs_quoting(str)) {
        std::string quoted_str = csv_quote_string(str);

        fmt::fprintf(outfile, "%s", quoted_str);
    } else {
        fmt::fprintf(outfile, "%s", str);
    }
}

static void
yajl_writer(void* context, const char* str, size_t len)
{
    FILE* file = (FILE*) context;

    fwrite(str, len, 1, file);
}

static void
json_write_row(yajl_gen handle,
               int row,
               lnav::text_anonymizer& ta,
               bool anonymize)
{
    auto& dls = lnav_data.ld_db_row_source;
    yajlpp_map obj_map(handle);

    for (size_t col = 0; col < dls.dls_headers.size(); col++) {
        obj_map.gen(dls.dls_headers[col].hm_name);

        if (dls.dls_rows[row][col] == db_label_source::NULL_STR) {
            obj_map.gen();
            continue;
        }

        auto& hm = dls.dls_headers[col];

        switch (hm.hm_column_type) {
            case SQLITE_FLOAT:
            case SQLITE_INTEGER: {
                auto len = strlen(dls.dls_rows[row][col]);

                if (len == 0) {
                    obj_map.gen();
                } else {
                    yajl_gen_number(handle, dls.dls_rows[row][col], len);
                }
                break;
            }
            case SQLITE_TEXT:
                switch (hm.hm_sub_type) {
                    case 74: {
                        unsigned char* err;
                        json_ptr jp("");
                        json_op jo(jp);

                        jo.jo_ptr_callbacks = json_op::gen_callbacks;
                        jo.jo_ptr_data = handle;
                        auto parse_handle = yajlpp::alloc_handle(
                            &json_op::ptr_callbacks, &jo);

                        const unsigned char* json_in
                            = (const unsigned char*) dls.dls_rows[row][col];
                        switch (yajl_parse(parse_handle.in(),
                                           json_in,
                                           strlen((const char*) json_in)))
                        {
                            case yajl_status_error:
                            case yajl_status_client_canceled: {
                                err = yajl_get_error(
                                    parse_handle.in(),
                                    0,
                                    json_in,
                                    strlen((const char*) json_in));
                                log_error("unable to parse JSON cell: %s", err);
                                obj_map.gen(dls.dls_rows[row][col]);
                                yajl_free_error(parse_handle.in(), err);
                                return;
                            }
                            default:
                                break;
                        }

                        switch (yajl_complete_parse(parse_handle.in())) {
                            case yajl_status_error:
                            case yajl_status_client_canceled: {
                                err = yajl_get_error(
                                    parse_handle.in(),
                                    0,
                                    json_in,
                                    strlen((const char*) json_in));
                                log_error("unable to parse JSON cell: %s", err);
                                obj_map.gen(dls.dls_rows[row][col]);
                                yajl_free_error(parse_handle.in(), err);
                                return;
                            }
                            default:
                                break;
                        }
                        break;
                    }
                    default:
                        obj_map.gen(anonymize
                                        ? ta.next(string_fragment::from_c_str(
                                              dls.dls_rows[row][col]))
                                        : dls.dls_rows[row][col]);
                        break;
                }
                break;
            default:
                obj_map.gen(anonymize ? ta.next(string_fragment::from_c_str(
                                            dls.dls_rows[row][col]))
                                      : dls.dls_rows[row][col]);
                break;
        }
    }
}

static Result<std::string, lnav::console::user_message>
com_save_to(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");

    FILE *outfile = nullptr, *toclose = nullptr;
    const char* mode = "";
    std::string fn, retval;
    bool to_term = false;
    bool anonymize = false;
    int (*closer)(FILE*) = fclose;

    if (args.empty()) {
        args.emplace_back("filename");
        return Ok(std::string());
    }

    fn = trim(remaining_args(cmdline, args));

    shlex lexer(fn);

    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse file name")
                  .with_reason(split_err.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err)))
                  .move();

        return Err(um);
    }

    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });
    auto anon_iter
        = std::find(split_args.begin(), split_args.end(), "--anonymize");
    if (anon_iter != split_args.end()) {
        split_args.erase(anon_iter);
        anonymize = true;
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    auto opt_view_name = find_arg(split_args, "--view");
    if (opt_view_name) {
        auto opt_view_index = view_from_string(opt_view_name->c_str());

        if (!opt_view_index) {
            return ec.make_error("invalid view name: {}", *opt_view_name);
        }

        tc = &lnav_data.ld_views[*opt_view_index];
    }

    if (split_args.empty()) {
        return ec.make_error(
            "expecting file name or '-' to write to the terminal");
    }

    if (split_args.size() > 1) {
        return ec.make_error("more than one file name was matched");
    }

    if (args[0] == "append-to") {
        mode = "ae";
    } else {
        mode = "we";
    }

    auto& dls = lnav_data.ld_db_row_source;
    bookmark_vector<vis_line_t> all_user_marks;
    lnav::text_anonymizer ta;

    if (args[0] == "write-csv-to" || args[0] == "write-json-to"
        || args[0] == "write-jsonlines-to" || args[0] == "write-cols-to"
        || args[0] == "write-table-to")
    {
        if (dls.dls_headers.empty()) {
            return ec.make_error(
                "no query result to write, use ';' to execute a query");
        }
    } else if (args[0] == "write-raw-to" && tc == &lnav_data.ld_views[LNV_DB]) {
    } else if (args[0] != "write-screen-to" && args[0] != "write-view-to") {
        all_user_marks = combined_user_marks(tc->get_bookmarks());
        if (all_user_marks.empty()) {
            return ec.make_error(
                "no lines marked to write, use 'm' to mark lines");
        }
    }

    if (ec.ec_dry_run) {
        outfile = tmpfile();
        toclose = outfile;
    } else if (split_args[0] == "-" || split_args[0] == "/dev/stdout") {
        auto ec_out = ec.get_output();

        if (!ec_out) {
            outfile = stdout;
            nodelay(lnav_data.ld_window, 0);
            endwin();
            struct termios curr_termios;
            tcgetattr(1, &curr_termios);
            curr_termios.c_oflag |= ONLCR | OPOST;
            tcsetattr(1, TCSANOW, &curr_termios);
            setvbuf(stdout, nullptr, _IONBF, 0);
            to_term = true;
            fprintf(outfile,
                    "\n---------------- Press any key to exit lo-fi display "
                    "----------------\n\n");
        } else {
            outfile = *ec_out;
        }
        if (outfile == stdout) {
            lnav_data.ld_stdout_used = true;
        }
    } else if (split_args[0] == "/dev/clipboard") {
        auto open_res = sysclip::open(sysclip::type_t::GENERAL);
        if (open_res.isErr()) {
            alerter::singleton().chime("cannot open clipboard");
            return ec.make_error("Unable to copy to clipboard: {}",
                                 open_res.unwrapErr());
        }
        auto holder = open_res.unwrap();
        toclose = outfile = holder.release();
        closer = holder.get_free_func<int (*)(FILE*)>();
    } else if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    } else if ((outfile = fopen(split_args[0].c_str(), mode)) == nullptr) {
        return ec.make_error("unable to open file -- {}", split_args[0]);
    } else {
        toclose = outfile;
    }

    int line_count = 0;

    if (args[0] == "write-csv-to") {
        std::vector<std::vector<const char*>>::iterator row_iter;
        std::vector<const char*>::iterator iter;
        std::vector<db_label_source::header_meta>::iterator hdr_iter;
        bool first = true;

        for (hdr_iter = dls.dls_headers.begin();
             hdr_iter != dls.dls_headers.end();
             ++hdr_iter)
        {
            if (!first) {
                fprintf(outfile, ",");
            }
            csv_write_string(outfile, hdr_iter->hm_name);
            first = false;
        }
        fprintf(outfile, "\n");

        for (row_iter = dls.dls_rows.begin(); row_iter != dls.dls_rows.end();
             ++row_iter)
        {
            if (ec.ec_dry_run && distance(dls.dls_rows.begin(), row_iter) > 10)
            {
                break;
            }

            first = true;
            for (iter = row_iter->begin(); iter != row_iter->end(); ++iter) {
                if (!first) {
                    fprintf(outfile, ",");
                }
                csv_write_string(
                    outfile,
                    anonymize ? ta.next(string_fragment::from_c_str(*iter))
                              : *iter);
                first = false;
            }
            fprintf(outfile, "\n");

            line_count += 1;
        }
    } else if (args[0] == "write-cols-to" || args[0] == "write-table-to") {
        bool first = true;

        fprintf(outfile, "\u250f");
        for (const auto& hdr : dls.dls_headers) {
            auto cell_line = repeat("\u2501", hdr.hm_column_size);

            if (!first) {
                fprintf(outfile, "\u2533");
            }
            fprintf(outfile, "%s", cell_line.c_str());
            first = false;
        }
        fprintf(outfile, "\u2513\n");

        for (const auto& hdr : dls.dls_headers) {
            auto centered_hdr = center_str(hdr.hm_name, hdr.hm_column_size);

            fprintf(outfile, "\u2503");
            fprintf(outfile, "%s", centered_hdr.c_str());
        }
        fprintf(outfile, "\u2503\n");

        first = true;
        fprintf(outfile, "\u2521");
        for (const auto& hdr : dls.dls_headers) {
            auto cell_line = repeat("\u2501", hdr.hm_column_size);

            if (!first) {
                fprintf(outfile, "\u2547");
            }
            fprintf(outfile, "%s", cell_line.c_str());
            first = false;
        }
        fprintf(outfile, "\u2529\n");

        for (size_t row = 0; row < dls.text_line_count(); row++) {
            if (ec.ec_dry_run && row > 10) {
                break;
            }

            for (size_t col = 0; col < dls.dls_headers.size(); col++) {
                const auto& hdr = dls.dls_headers[col];

                fprintf(outfile, "\u2502");

                auto cell = std::string(dls.dls_rows[row][col]);
                if (anonymize) {
                    cell = ta.next(cell);
                }
                auto cell_length
                    = utf8_string_length(cell).unwrapOr(cell.size());
                auto padding = anonymize ? 1 : hdr.hm_column_size - cell_length;

                if (hdr.hm_column_type != SQLITE3_TEXT) {
                    fprintf(outfile, "%s", std::string(padding, ' ').c_str());
                }
                fprintf(outfile, "%s", cell.c_str());
                if (hdr.hm_column_type == SQLITE3_TEXT) {
                    fprintf(outfile, "%s", std::string(padding, ' ').c_str());
                }
            }
            fprintf(outfile, "\u2502\n");

            line_count += 1;
        }

        first = true;
        fprintf(outfile, "\u2514");
        for (const auto& hdr : dls.dls_headers) {
            auto cell_line = repeat("\u2501", hdr.hm_column_size);

            if (!first) {
                fprintf(outfile, "\u2534");
            }
            fprintf(outfile, "%s", cell_line.c_str());
            first = false;
        }
        fprintf(outfile, "\u2518\n");

    } else if (args[0] == "write-json-to") {
        yajlpp_gen gen;

        yajl_gen_config(gen, yajl_gen_beautify, 1);
        yajl_gen_config(gen, yajl_gen_print_callback, yajl_writer, outfile);

        {
            yajlpp_array root_array(gen);

            for (size_t row = 0; row < dls.dls_rows.size(); row++) {
                if (ec.ec_dry_run && row > 10) {
                    break;
                }

                json_write_row(gen, row, ta, anonymize);
                line_count += 1;
            }
        }
    } else if (args[0] == "write-jsonlines-to") {
        yajlpp_gen gen;

        yajl_gen_config(gen, yajl_gen_beautify, 0);
        yajl_gen_config(gen, yajl_gen_print_callback, yajl_writer, outfile);

        for (size_t row = 0; row < dls.dls_rows.size(); row++) {
            if (ec.ec_dry_run && row > 10) {
                break;
            }

            json_write_row(gen, row, ta, anonymize);
            yajl_gen_reset(gen, "\n");
            line_count += 1;
        }
    } else if (args[0] == "write-screen-to") {
        bool wrapped = tc->get_word_wrap();
        vis_line_t orig_top = tc->get_top();
        auto inner_height = tc->get_inner_height();

        tc->set_word_wrap(to_term);

        vis_line_t top = tc->get_top();
        vis_line_t bottom = tc->get_bottom();
        if (lnav_data.ld_flags & LNF_HEADLESS && inner_height > 0_vl) {
            bottom = inner_height - 1_vl;
        }
        auto screen_height = inner_height == 0 ? 0 : bottom - top + 1;
        auto y = 0_vl;
        auto wrapped_count = 0_vl;
        std::vector<attr_line_t> rows(screen_height);
        auto dim = tc->get_dimensions();
        attr_line_t ov_al;

        auto* los = tc->get_overlay_source();
        while (
            los != nullptr
            && los->list_static_overlay(*tc, y, tc->get_inner_height(), ov_al))
        {
            write_line_to(outfile, ov_al);
            ov_al.clear();
            ++y;
        }
        tc->listview_value_for_rows(*tc, top, rows);
        for (auto& al : rows) {
            wrapped_count += vis_line_t((al.length() - 1) / (dim.second - 2));
            if (anonymize) {
                al.al_attrs.clear();
                al.al_string = ta.next(al.al_string);
            }
            write_line_to(outfile, al);

            ++y;
            if (los != nullptr) {
                std::vector<attr_line_t> row_overlay_content;
                los->list_value_for_overlay(*tc, top, row_overlay_content);
                for (const auto& ov_row : row_overlay_content) {
                    write_line_to(outfile, ov_row);
                    line_count += 1;
                    ++y;
                }
            }
            line_count += 1;
            ++top;
        }

        tc->set_word_wrap(wrapped);
        tc->set_top(orig_top);

        if (!(lnav_data.ld_flags & LNF_HEADLESS)) {
            while (y + wrapped_count < dim.first + 2_vl) {
                fmt::print(outfile, FMT_STRING("\n"));
                ++y;
            }
        }
    } else if (args[0] == "write-raw-to") {
        if (tc == &lnav_data.ld_views[LNV_DB]) {
            for (auto row_iter = dls.dls_rows.begin();
                 row_iter != dls.dls_rows.end();
                 ++row_iter)
            {
                if (ec.ec_dry_run
                    && distance(dls.dls_rows.begin(), row_iter) > 10)
                {
                    break;
                }

                for (auto& iter : *row_iter) {
                    if (anonymize) {
                        fputs(
                            ta.next(string_fragment::from_c_str(iter)).c_str(),
                            outfile);
                    } else {
                        fputs(iter, outfile);
                    }
                }
                fprintf(outfile, "\n");

                line_count += 1;
            }
        } else if (tc == &lnav_data.ld_views[LNV_LOG]) {
            std::optional<std::pair<logfile*, content_line_t>> last_line;
            bookmark_vector<vis_line_t> visited;
            auto& lss = lnav_data.ld_log_source;
            std::vector<attr_line_t> rows(1);
            size_t count = 0;
            std::string line;

            for (auto iter = all_user_marks.begin();
                 iter != all_user_marks.end();
                 iter++, count++)
            {
                if (ec.ec_dry_run && count > 10) {
                    break;
                }
                auto cl = lss.at(*iter);
                auto lf = lss.find(cl);
                auto lf_iter = lf->begin() + cl;

                while (lf_iter->get_sub_offset() != 0) {
                    --lf_iter;
                }

                auto line_pair = std::make_pair(
                    lf.get(),
                    content_line_t(std::distance(lf->begin(), lf_iter)));
                if (last_line && last_line.value() == line_pair) {
                    continue;
                }
                last_line = line_pair;
                auto read_res = lf->read_raw_message(lf_iter);
                if (read_res.isErr()) {
                    log_error("unable to read message: %s",
                              read_res.unwrapErr().c_str());
                    continue;
                }
                auto sbr = read_res.unwrap();
                if (anonymize) {
                    auto msg = ta.next(sbr.to_string_fragment().to_string());
                    fprintf(outfile, "%s\n", msg.c_str());
                } else {
                    fprintf(
                        outfile, "%.*s\n", (int) sbr.length(), sbr.get_data());
                }

                line_count += 1;
            }
        }
    } else if (args[0] == "write-view-to") {
        bool wrapped = tc->get_word_wrap();
        auto tss = tc->get_sub_source();

        tc->set_word_wrap(to_term);

        for (size_t lpc = 0; lpc < tss->text_line_count(); lpc++) {
            if (ec.ec_dry_run && lpc >= 10) {
                break;
            }

            std::string line;

            tss->text_value_for_line(*tc, lpc, line, text_sub_source::RF_RAW);
            if (anonymize) {
                line = ta.next(line);
            }
            fprintf(outfile, "%s\n", line.c_str());

            line_count += 1;
        }

        tc->set_word_wrap(wrapped);
    } else {
        auto* los = tc->get_overlay_source();
        auto* fos = dynamic_cast<field_overlay_source*>(los);
        std::vector<attr_line_t> rows(1);
        attr_line_t ov_al;
        size_t count = 0;

        if (fos != nullptr) {
            fos->fos_contexts.push(
                field_overlay_source::context{"", false, false, false});
        }

        auto y = 0_vl;
        while (
            los != nullptr
            && los->list_static_overlay(*tc, y, tc->get_inner_height(), ov_al))
        {
            write_line_to(outfile, ov_al);
            ov_al.clear();
            ++y;
        }
        for (auto iter = all_user_marks.begin(); iter != all_user_marks.end();
             iter++, count++)
        {
            if (ec.ec_dry_run && count > 10) {
                break;
            }
            tc->listview_value_for_rows(*tc, *iter, rows);
            if (anonymize) {
                rows[0].al_attrs.clear();
                rows[0].al_string = ta.next(rows[0].al_string);
            }
            write_line_to(outfile, rows[0]);

            y = 0_vl;
            if (los != nullptr) {
                std::vector<attr_line_t> row_overlay_content;
                los->list_value_for_overlay(*tc, (*iter), row_overlay_content);
                for (const auto& ov_row : row_overlay_content) {
                    write_line_to(outfile, ov_row);
                    line_count += 1;
                    ++y;
                }
            }
            line_count += 1;
        }

        if (fos != nullptr) {
            fos->fos_contexts.pop();
        }
    }

    fflush(outfile);

    if (to_term) {
        cbreak();
        getch();
        refresh();
        nodelay(lnav_data.ld_window, 1);
    }
    if (ec.ec_dry_run) {
        rewind(outfile);

        char buffer[32 * 1024];
        size_t rc = fread(buffer, 1, sizeof(buffer), outfile);

        attr_line_t al(std::string(buffer, rc));

        lnav_data.ld_preview_view[0].set_sub_source(
            &lnav_data.ld_preview_source[0]);
        lnav_data.ld_preview_source[0]
            .replace_with(al)
            .set_text_format(detect_text_format(al.get_string()))
            .truncate_to(10);
        lnav_data.ld_preview_status_source[0].get_description().set_value(
            "First lines of file: %s", split_args[0].c_str());
    } else {
        retval = fmt::format(FMT_STRING("info: Wrote {:L} rows to {}"),
                             line_count,
                             split_args[0]);
    }
    if (toclose != nullptr) {
        closer(toclose);
    }
    outfile = nullptr;

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_pipe_to(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("filename");
        return Ok(std::string());
    }

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    if (args.size() < 2) {
        return ec.make_error("expecting command to execute");
    }

    if (ec.ec_dry_run) {
        return Ok(std::string());
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    auto bv = combined_user_marks(tc->get_bookmarks());
    bool pipe_line_to = (args[0] == "pipe-line-to");
    auto path_v = ec.ec_path_stack;
    std::map<std::string, std::string> extra_env;

    if (pipe_line_to && tc == &lnav_data.ld_views[LNV_LOG]) {
        log_data_helper ldh(lnav_data.ld_log_source);
        char tmp_str[64];

        ldh.parse_line(ec.ec_top_line, true);
        auto format = ldh.ldh_file->get_format();
        auto source_path = format->get_source_path();
        path_v.insert(path_v.end(), source_path.begin(), source_path.end());

        extra_env["log_line"] = fmt::to_string((int) ec.ec_top_line);
        sql_strftime(tmp_str, sizeof(tmp_str), ldh.ldh_line->get_timeval());
        extra_env["log_time"] = tmp_str;
        extra_env["log_path"] = ldh.ldh_file->get_filename();
        extra_env["log_level"] = ldh.ldh_line->get_level_name();
        if (ldh.ldh_line_values.lvv_opid_value) {
            extra_env["log_opid"] = ldh.ldh_line_values.lvv_opid_value.value();
        }
        auto read_res = ldh.ldh_file->read_raw_message(ldh.ldh_line);
        if (read_res.isOk()) {
            auto raw_text = to_string(read_res.unwrap());
            extra_env["log_raw_text"] = raw_text;
        }
        for (auto& ldh_line_value : ldh.ldh_line_values.lvv_values) {
            extra_env[ldh_line_value.lv_meta.lvm_name.to_string()]
                = ldh_line_value.to_string();
        }
        auto iter = ldh.ldh_parser->dp_pairs.begin();
        for (size_t lpc = 0; lpc < ldh.ldh_parser->dp_pairs.size();
             lpc++, ++iter)
        {
            std::string colname = ldh.ldh_parser->get_element_string(
                iter->e_sub_elements->front());
            colname = ldh.ldh_namer->add_column(colname).to_string();
            std::string val = ldh.ldh_parser->get_element_string(
                iter->e_sub_elements->back());
            extra_env[colname] = val;
        }
    }

    std::string cmd = trim(remaining_args(cmdline, args));
    auto for_child_res = auto_pipe::for_child_fds(STDIN_FILENO, STDOUT_FILENO);

    if (for_child_res.isErr()) {
        return ec.make_error(FMT_STRING("unable to open pipe to child: {}"),
                             for_child_res.unwrapErr());
    }

    auto child_fds = for_child_res.unwrap();

    pid_t child_pid = fork();

    for (auto& child_fd : child_fds) {
        child_fd.after_fork(child_pid);
    }

    switch (child_pid) {
        case -1:
            return ec.make_error("unable to fork child process -- {}",
                                 strerror(errno));

        case 0: {
            const char* exec_args[] = {
                "sh",
                "-c",
                cmd.c_str(),
                nullptr,
            };
            std::string path;

            dup2(STDOUT_FILENO, STDERR_FILENO);
            path_v.emplace_back(lnav::paths::dotlnav() / "formats/default");

            setenv("PATH", lnav::filesystem::build_path(path_v).c_str(), 1);
            for (const auto& pair : extra_env) {
                setenv(pair.first.c_str(), pair.second.c_str(), 1);
            }
            execvp(exec_args[0], (char* const*) exec_args);
            _exit(1);
            break;
        }

        default:
            bookmark_vector<vis_line_t>::iterator iter;
            std::string line;

            lnav_data.ld_children.push_back(child_pid);

            std::future<std::string> reader;

            if (child_fds[1].read_end() != -1) {
                reader
                    = ec.ec_pipe_callback(ec, cmdline, child_fds[1].read_end());
            }

            if (pipe_line_to) {
                if (tc->get_inner_height() == 0) {
                    // Nothing to do
                } else if (tc == &lnav_data.ld_views[LNV_LOG]) {
                    logfile_sub_source& lss = lnav_data.ld_log_source;
                    content_line_t cl = lss.at(tc->get_top());
                    std::shared_ptr<logfile> lf = lss.find(cl);
                    shared_buffer_ref sbr;
                    lf->read_full_message(lf->message_start(lf->begin() + cl),
                                          sbr);
                    if (write(child_fds[0].write_end(),
                              sbr.get_data(),
                              sbr.length())
                        == -1)
                    {
                        return ec.make_error("Unable to write to pipe -- {}",
                                             strerror(errno));
                    }
                    log_perror(write(child_fds[0].write_end(), "\n", 1));
                } else {
                    tc->grep_value_for_line(tc->get_top(), line);
                    if (write(
                            child_fds[0].write_end(), line.c_str(), line.size())
                        == -1)
                    {
                        return ec.make_error("Unable to write to pipe -- {}",
                                             strerror(errno));
                    }
                    log_perror(write(child_fds[0].write_end(), "\n", 1));
                }
            } else {
                for (iter = bv.begin(); iter != bv.end(); iter++) {
                    tc->grep_value_for_line(*iter, line);
                    if (write(
                            child_fds[0].write_end(), line.c_str(), line.size())
                        == -1)
                    {
                        return ec.make_error("Unable to write to pipe -- {}",
                                             strerror(errno));
                    }
                    log_perror(write(child_fds[0].write_end(), "\n", 1));
                }
            }

            child_fds[0].write_end().reset();

            if (reader.valid()) {
                retval = reader.get();
            } else {
                retval = "";
            }
            break;
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_redirect_to(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");

    if (args.empty()) {
        args.emplace_back("filename");
        return Ok(std::string());
    }

    if (args.size() == 1) {
        if (ec.ec_dry_run) {
            return Ok(std::string("info: redirect will be cleared"));
        }

        ec.clear_output();
        return Ok(std::string("info: cleared redirect"));
    }

    std::string fn = trim(remaining_args(cmdline, args));
    shlex lexer(fn);

    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse file name")
                  .with_reason(split_err.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err)))
                  .move();

        return Err(um);
    }
    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });
    if (split_args.size() > 1) {
        return ec.make_error("more than one file name was matched");
    }

    if (ec.ec_dry_run) {
        return Ok("info: output will be redirected to -- " + split_args[0]);
    }

    if (split_args[0] == "-") {
        ec.clear_output();
    } else if (split_args[0] == "/dev/clipboard") {
        auto out = sysclip::open(sysclip::type_t::GENERAL);
        if (out.isErr()) {
            alerter::singleton().chime("cannot open clipboard");
            return ec.make_error("Unable to copy to clipboard: {}",
                                 out.unwrapErr());
        }

        auto holder = out.unwrap();
        ec.set_output(split_args[0],
                      holder.release(),
                      holder.get_free_func<int (*)(FILE*)>());
    } else if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    } else {
        FILE* file = fopen(split_args[0].c_str(), "w");
        if (file == nullptr) {
            return ec.make_error("unable to open file -- {}", split_args[0]);
        }

        ec.set_output(split_args[0], file, fclose);
    }

    return Ok("info: redirecting output to file -- " + split_args[0]);
}

static Result<std::string, lnav::console::user_message>
com_highlight(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("filter");
    } else if (args.size() > 1) {
        const static intern_string_t PATTERN_SRC
            = intern_string::lookup("pattern");

        auto* tc = *lnav_data.ld_view_stack.top();
        auto& hm = tc->get_highlights();
        auto re_frag = remaining_args_frag(cmdline, args);
        args[1] = re_frag.to_string();
        if (hm.find({highlight_source_t::INTERACTIVE, args[1]}) != hm.end()) {
            return ec.make_error("highlight already exists -- {}", args[1]);
        }

        auto compile_res = lnav::pcre2pp::code::from(args[1], PCRE2_CASELESS);

        if (compile_res.isErr()) {
            auto ce = compile_res.unwrapErr();
            auto um = lnav::console::to_user_message(PATTERN_SRC, ce);
            return Err(um);
        }
        highlighter hl(compile_res.unwrap().to_shared());
        auto hl_attrs = view_colors::singleton().attrs_for_ident(args[1]);

        if (ec.ec_dry_run) {
            hl_attrs.ta_attrs |= A_BLINK;
        }

        hl.with_attrs(hl_attrs);

        if (ec.ec_dry_run) {
            hm[{highlight_source_t::PREVIEW, "preview"}] = hl;

            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "Matches are highlighted in the view");

            retval = "";
        } else {
            hm[{highlight_source_t::INTERACTIVE, args[1]}] = hl;

            if (lnav_data.ld_rl_view != nullptr) {
                lnav_data.ld_rl_view->add_possibility(
                    ln_mode_t::COMMAND, "highlight", args[1]);
            }

            retval = "info: highlight pattern now active";
        }
        tc->reload_data();
    } else {
        return ec.make_error("expecting a regular expression to highlight");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_clear_highlight(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("highlight");
    } else if (args.size() > 1 && args[1][0] != '$') {
        textview_curses* tc = *lnav_data.ld_view_stack.top();
        auto& hm = tc->get_highlights();

        args[1] = remaining_args(cmdline, args);
        auto hm_iter = hm.find({highlight_source_t::INTERACTIVE, args[1]});
        if (hm_iter == hm.end()) {
            return ec.make_error("highlight does not exist -- {}", args[1]);
        } else if (ec.ec_dry_run) {
            retval = "";
        } else {
            hm.erase(hm_iter);
            retval = "info: highlight pattern cleared";
            tc->reload_data();

            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->rem_possibility(
                    ln_mode_t::COMMAND, "highlight", args[1]);
            }
        }
    } else {
        return ec.make_error("expecting highlight expression to clear");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_help(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        ensure_view(&lnav_data.ld_views[LNV_HELP]);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message> com_enable_filter(
    exec_context& ec, std::string cmdline, std::vector<std::string>& args);

static Result<std::string, lnav::console::user_message>
com_filter(exec_context& ec,
           std::string cmdline,
           std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("filter");

        return Ok(std::string());
    }

    auto tc = *lnav_data.ld_view_stack.top();
    auto tss = tc->get_sub_source();

    if (!tss->tss_supports_filtering) {
        return ec.make_error("{} view does not support filtering",
                             lnav_view_strings[tc - lnav_data.ld_views]);
    } else if (args.size() > 1) {
        const static intern_string_t PATTERN_SRC
            = intern_string::lookup("pattern");

        auto* tss = tc->get_sub_source();
        auto& fs = tss->get_filters();
        auto re_frag = remaining_args_frag(cmdline, args);
        args[1] = re_frag.to_string();
        if (fs.get_filter(args[1]) != nullptr) {
            return com_enable_filter(ec, cmdline, args);
        }

        if (fs.full()) {
            return ec.make_error(
                "filter limit reached, try combining "
                "filters with a pipe symbol (e.g. foo|bar)");
        }

        auto compile_res = lnav::pcre2pp::code::from(args[1], PCRE2_CASELESS);

        if (compile_res.isErr()) {
            auto ce = compile_res.unwrapErr();
            auto um = lnav::console::to_user_message(PATTERN_SRC, ce);
            return Err(um);
        }
        if (ec.ec_dry_run) {
            if (args[0] == "filter-in" && !fs.empty()) {
                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_value(
                        "Match preview for :filter-in only works if there are "
                        "no "
                        "other filters");
                retval = "";
            } else {
                auto& hm = tc->get_highlights();
                highlighter hl(compile_res.unwrap().to_shared());
                auto role = (args[0] == "filter-out") ? role_t::VCR_DIFF_DELETE
                                                      : role_t::VCR_DIFF_ADD;
                hl.with_role(role);
                hl.with_attrs(text_attrs{A_BLINK | A_REVERSE});

                hm[{highlight_source_t::PREVIEW, "preview"}] = hl;
                tc->reload_data();

                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_value(
                        "Matches are highlighted in %s in the text view",
                        role == role_t::VCR_DIFF_DELETE ? "red" : "green");

                retval = "";
            }
        } else {
            text_filter::type_t lt = (args[0] == "filter-out")
                ? text_filter::EXCLUDE
                : text_filter::INCLUDE;
            auto filter_index = fs.next_index();
            if (!filter_index) {
                return ec.make_error("too many filters");
            }
            auto pf = std::make_shared<pcre_filter>(
                lt, args[1], *filter_index, compile_res.unwrap().to_shared());

            log_debug("%s [%d] %s",
                      args[0].c_str(),
                      pf->get_index(),
                      args[1].c_str());
            fs.add_filter(pf);
            tss->text_filters_changed();

            retval = "info: filter now active";
        }
    } else {
        return ec.make_error("expecting a regular expression to filter");
    }

    return Ok(retval);
}

static readline_context::prompt_result_t
com_filter_prompt(exec_context& ec, const std::string& cmdline)
{
    const auto* tc = lnav_data.ld_view_stack.top().value();
    std::vector<std::string> args;

    split_ws(cmdline, args);
    if (args.size() > 1) {
        return {};
    }

    if (tc->tc_selected_text) {
        return {"", tc->tc_selected_text->sti_value};
    }

    return {"", tc->get_current_search()};
}

static Result<std::string, lnav::console::user_message>
com_delete_filter(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("all-filters");
    } else if (args.size() > 1) {
        textview_curses* tc = *lnav_data.ld_view_stack.top();
        text_sub_source* tss = tc->get_sub_source();
        filter_stack& fs = tss->get_filters();

        args[1] = remaining_args(cmdline, args);
        if (ec.ec_dry_run) {
            retval = "";
        } else if (fs.delete_filter(args[1])) {
            retval = "info: deleted filter";
            tss->text_filters_changed();
        } else {
            return ec.make_error("unknown filter -- {}", args[1]);
        }
    } else {
        return ec.make_error("expecting a filter to delete");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_enable_filter(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("disabled-filter");
    } else if (args.size() > 1) {
        textview_curses* tc = *lnav_data.ld_view_stack.top();
        text_sub_source* tss = tc->get_sub_source();
        filter_stack& fs = tss->get_filters();
        std::shared_ptr<text_filter> lf;

        args[1] = remaining_args(cmdline, args);
        lf = fs.get_filter(args[1]);
        if (lf == nullptr) {
            return ec.make_error("no such filter -- {}", args[1]);
        } else if (lf->is_enabled()) {
            retval = "info: filter already enabled";
        } else if (ec.ec_dry_run) {
            retval = "";
        } else {
            fs.set_filter_enabled(lf, true);
            tss->text_filters_changed();
            retval = "info: filter enabled";
        }
    } else {
        return ec.make_error("expecting disabled filter to enable");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_disable_filter(exec_context& ec,
                   std::string cmdline,
                   std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("enabled-filter");
    } else if (args.size() > 1) {
        textview_curses* tc = *lnav_data.ld_view_stack.top();
        text_sub_source* tss = tc->get_sub_source();
        filter_stack& fs = tss->get_filters();
        std::shared_ptr<text_filter> lf;

        args[1] = remaining_args(cmdline, args);
        lf = fs.get_filter(args[1]);
        if (lf == nullptr) {
            return ec.make_error("no such filter -- {}", args[1]);
        } else if (!lf->is_enabled()) {
            retval = "info: filter already disabled";
        } else if (ec.ec_dry_run) {
            retval = "";
        } else {
            fs.set_filter_enabled(lf, false);
            tss->text_filters_changed();
            retval = "info: filter disabled";
        }
    } else {
        return ec.make_error("expecting enabled filter to disable");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_filter_expr(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("filter-expr-syms");
    } else if (args.size() > 1) {
        textview_curses* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return ec.make_error(
                "The :filter-expr command only works in the log view");
        }

        auto expr = remaining_args(cmdline, args);
        args[1] = fmt::format(FMT_STRING("SELECT 1 WHERE {}"), expr);

        auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
#ifdef SQLITE_PREPARE_PERSISTENT
        auto retcode = sqlite3_prepare_v3(lnav_data.ld_db.in(),
                                          args[1].c_str(),
                                          args[1].size(),
                                          SQLITE_PREPARE_PERSISTENT,
                                          stmt.out(),
                                          nullptr);
#else
        auto retcode = sqlite3_prepare_v2(lnav_data.ld_db.in(),
                                          args[1].c_str(),
                                          args[1].size(),
                                          stmt.out(),
                                          nullptr);
#endif
        if (retcode != SQLITE_OK) {
            const char* errmsg = sqlite3_errmsg(lnav_data.ld_db);
            auto expr_al = attr_line_t(expr).with_attr_for_all(
                VC_ROLE.value(role_t::VCR_QUOTED_CODE));
            readline_sqlite_highlighter(expr_al, std::nullopt);
            auto um = lnav::console::user_message::error(
                          attr_line_t("invalid filter expression: ")
                              .append(expr_al))
                          .with_reason(errmsg)
                          .with_snippets(ec.ec_source)
                          .move();

            return Err(um);
        }

        if (ec.ec_dry_run) {
            auto set_res = lnav_data.ld_log_source.set_preview_sql_filter(
                stmt.release());

            if (set_res.isErr()) {
                return Err(set_res.unwrapErr());
            }
            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "Matches are highlighted in the text view");
        } else {
            lnav_data.ld_log_source.set_preview_sql_filter(nullptr);
            auto set_res
                = lnav_data.ld_log_source.set_sql_filter(expr, stmt.release());

            if (set_res.isErr()) {
                return Err(set_res.unwrapErr());
            }
        }
        lnav_data.ld_log_source.text_filters_changed();
        tc->reload_data();
    } else {
        return ec.make_error("expecting an SQL expression");
    }

    return Ok(retval);
}

static readline_context::prompt_result_t
com_filter_expr_prompt(exec_context& ec, const std::string& cmdline)
{
    auto* tc = *lnav_data.ld_view_stack.top();

    if (tc != &lnav_data.ld_views[LNV_LOG]) {
        return {""};
    }

    return {
        fmt::format(FMT_STRING("{} {}"),
                    trim(cmdline),
                    trim(lnav_data.ld_log_source.get_sql_filter_text())),
    };
}

static Result<std::string, lnav::console::user_message>
com_clear_filter_expr(exec_context& ec,
                      std::string cmdline,
                      std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else {
        if (!ec.ec_dry_run) {
            lnav_data.ld_log_source.set_sql_filter("", nullptr);
            lnav_data.ld_log_source.text_filters_changed();
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_enable_word_wrap(exec_context& ec,
                     std::string cmdline,
                     std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        lnav_data.ld_views[LNV_LOG].set_word_wrap(true);
        lnav_data.ld_views[LNV_TEXT].set_word_wrap(true);
        lnav_data.ld_views[LNV_PRETTY].set_word_wrap(true);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_disable_word_wrap(exec_context& ec,
                      std::string cmdline,
                      std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        lnav_data.ld_views[LNV_LOG].set_word_wrap(false);
        lnav_data.ld_views[LNV_TEXT].set_word_wrap(false);
        lnav_data.ld_views[LNV_PRETTY].set_word_wrap(false);
    }

    return Ok(retval);
}

static std::set<std::string> custom_logline_tables;

static Result<std::string, lnav::console::user_message>
com_create_logline_table(exec_context& ec,
                         std::string cmdline,
                         std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (args.size() == 2) {
        textview_curses& log_view = lnav_data.ld_views[LNV_LOG];

        if (log_view.get_inner_height() == 0) {
            return ec.make_error("no log data available");
        } else {
            vis_line_t vl = log_view.get_selection();
            content_line_t cl = lnav_data.ld_log_source.at_base(vl);
            auto ldt = std::make_shared<log_data_table>(
                lnav_data.ld_log_source,
                *lnav_data.ld_vtab_manager,
                cl,
                intern_string::lookup(args[1]));

            if (ec.ec_dry_run) {
                attr_line_t al(ldt->get_table_statement());

                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_value("The following table will be created:");
                lnav_data.ld_preview_view[0].set_sub_source(
                    &lnav_data.ld_preview_source[0]);
                lnav_data.ld_preview_source[0].replace_with(al).set_text_format(
                    text_format_t::TF_SQL);

                return Ok(std::string());
            } else {
                std::string errmsg;

                errmsg = lnav_data.ld_vtab_manager->register_vtab(ldt);
                if (errmsg.empty()) {
                    custom_logline_tables.insert(args[1]);
                    if (lnav_data.ld_rl_view != NULL) {
                        lnav_data.ld_rl_view->add_possibility(
                            ln_mode_t::COMMAND, "custom-table", args[1]);
                    }
                    retval = "info: created new log table -- " + args[1];
                } else {
                    return ec.make_error("unable to create table -- {}",
                                         errmsg);
                }
            }
        }
    } else {
        return ec.make_error("expecting a table name");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_delete_logline_table(exec_context& ec,
                         std::string cmdline,
                         std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("custom-table");
    } else if (args.size() == 2) {
        if (custom_logline_tables.find(args[1]) == custom_logline_tables.end())
        {
            return ec.make_error("unknown logline table -- {}", args[1]);
        }

        if (ec.ec_dry_run) {
            return Ok(std::string());
        }

        std::string rc = lnav_data.ld_vtab_manager->unregister_vtab(
            intern_string::lookup(args[1]));

        if (rc.empty()) {
            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->rem_possibility(
                    ln_mode_t::COMMAND, "custom-table", args[1]);
            }
            retval = "info: deleted logline table";
        } else {
            return ec.make_error("{}", rc);
        }
    } else {
        return ec.make_error("expecting a table name");
    }

    return Ok(retval);
}

static std::set<std::string> custom_search_tables;

static Result<std::string, lnav::console::user_message>
com_create_search_table(exec_context& ec,
                        std::string cmdline,
                        std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (args.size() >= 2) {
        const static intern_string_t PATTERN_SRC
            = intern_string::lookup("pattern");
        string_fragment regex_frag;
        std::string regex;

        if (args.size() >= 3) {
            regex_frag = remaining_args_frag(cmdline, args, 2);
            regex = regex_frag.to_string();
        } else {
            regex = lnav_data.ld_views[LNV_LOG].get_current_search();
        }

        auto compile_res = lnav::pcre2pp::code::from(
            regex, log_search_table_ns::PATTERN_OPTIONS);

        if (compile_res.isErr()) {
            auto re_err = compile_res.unwrapErr();
            auto um = lnav::console::to_user_message(PATTERN_SRC, re_err)
                          .with_snippets(ec.ec_source);
            return Err(um);
        }

        auto re = compile_res.unwrap().to_shared();
        auto tab_name = intern_string::lookup(args[1]);
        auto lst = std::make_shared<log_search_table>(re, tab_name);
        if (ec.ec_dry_run) {
            auto* tc = &lnav_data.ld_views[LNV_LOG];
            auto& hm = tc->get_highlights();
            highlighter hl(re);

            hl.with_role(role_t::VCR_INFO);
            hl.with_attrs(text_attrs{A_BLINK});

            hm[{highlight_source_t::PREVIEW, "preview"}] = hl;
            tc->reload_data();

            attr_line_t al(lst->get_table_statement());

            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "The following table will be created:");

            lnav_data.ld_preview_view[0].set_sub_source(
                &lnav_data.ld_preview_source[0]);
            lnav_data.ld_preview_source[0].replace_with(al).set_text_format(
                text_format_t::TF_SQL);

            return Ok(std::string());
        }

        auto tab_iter = custom_search_tables.find(args[1]);
        if (tab_iter != custom_search_tables.end()) {
            lnav_data.ld_vtab_manager->unregister_vtab(tab_name);
        }

        std::string errmsg;

        errmsg = lnav_data.ld_vtab_manager->register_vtab(lst);
        if (errmsg.empty()) {
            custom_search_tables.insert(args[1]);
            if (lnav_data.ld_rl_view != nullptr) {
                lnav_data.ld_rl_view->add_possibility(
                    ln_mode_t::COMMAND, "search-table", args[1]);
            }
            retval = "info: created new search table -- " + args[1];
        } else {
            return ec.make_error("unable to create table -- {}", errmsg);
        }
    } else {
        return ec.make_error("expecting a table name");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_delete_search_table(exec_context& ec,
                        std::string cmdline,
                        std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("search-table");
    } else if (args.size() == 2) {
        auto tab_iter = custom_search_tables.find(args[1]);
        if (tab_iter == custom_search_tables.end()) {
            return ec.make_error("unknown search table -- {}", args[1]);
        }

        if (ec.ec_dry_run) {
            return Ok(std::string());
        }

        custom_search_tables.erase(tab_iter);
        auto rc = lnav_data.ld_vtab_manager->unregister_vtab(
            intern_string::lookup(args[1]));

        if (rc.empty()) {
            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->rem_possibility(
                    ln_mode_t::COMMAND, "search-table", args[1]);
            }
            retval = "info: deleted search table";
        } else {
            return ec.make_error("{}", rc);
        }
    } else {
        return ec.make_error("expecting a table name");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_session(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (ec.ec_dry_run) {
        retval = "";
    } else if (args.size() >= 2) {
        /* XXX put these in a map */
        if (args[1] != "highlight" && args[1] != "enable-word-wrap"
            && args[1] != "disable-word-wrap" && args[1] != "filter-in"
            && args[1] != "filter-out" && args[1] != "enable-filter"
            && args[1] != "disable-filter")
        {
            return ec.make_error(
                "only the highlight, filter, and word-wrap commands are "
                "supported");
        } else if (getenv("HOME") == NULL) {
            return ec.make_error("the HOME environment variable is not set");
        } else {
            auto saved_cmd = trim(remaining_args(cmdline, args));
            auto old_file_name = lnav::paths::dotlnav() / "session";
            auto new_file_name = lnav::paths::dotlnav() / "session.tmp";

            std::ifstream session_file(old_file_name.string());
            std::ofstream new_session_file(new_file_name.string());

            if (!new_session_file) {
                return ec.make_error("cannot write to session file");
            } else {
                bool added = false;
                std::string line;

                if (session_file.is_open()) {
                    while (getline(session_file, line)) {
                        if (line == saved_cmd) {
                            added = true;
                            break;
                        }
                        new_session_file << line << std::endl;
                    }
                }
                if (!added) {
                    new_session_file << saved_cmd << std::endl;

                    log_perror(
                        rename(new_file_name.c_str(), old_file_name.c_str()));
                } else {
                    log_perror(remove(new_file_name.c_str()));
                }

                retval = "info: session file saved";
            }
        }
    } else {
        return ec.make_error("expecting a command to save to the session file");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_open(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");
    std::string retval;

    if (args.empty()) {
        args.emplace_back("filename");
        return Ok(std::string());
    }

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    if (args.size() < 2) {
        return ec.make_error("expecting file name to open");
    }

    std::vector<std::string> word_exp;
    std::string pat;
    file_collection fc;

    pat = trim(remaining_args(cmdline, args));

    shlex lexer(pat);
    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse file names")
                  .with_reason(split_err.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err)))
                  .move();

        return Err(um);
    }

    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });

    std::vector<std::pair<std::string, file_location_t>> files_to_front;
    std::vector<std::string> closed_files;
    logfile_open_options loo;

    auto prov = ec.get_provenance<exec_context::file_open>();
    if (prov) {
        loo.with_filename(prov->fo_name);
    }

    for (auto fn : split_args) {
        file_location_t file_loc;

        if (access(fn.c_str(), R_OK) != 0) {
            auto colon_index = fn.rfind(':');
            auto hash_index = fn.rfind('#');
            if (colon_index != std::string::npos) {
                auto top_range
                    = scn::string_view{&fn[colon_index + 1], &(*fn.cend())};
                auto scan_res = scn::scan_value<int>(top_range);

                if (scan_res) {
                    fn = fn.substr(0, colon_index);
                    file_loc = vis_line_t(scan_res.value());
                }
            } else if (hash_index != std::string::npos) {
                file_loc = fn.substr(hash_index);
                fn = fn.substr(0, hash_index);
            }
            loo.with_init_location(file_loc);
        }

        auto file_iter = lnav_data.ld_active_files.fc_files.begin();
        for (; file_iter != lnav_data.ld_active_files.fc_files.end();
             ++file_iter)
        {
            auto lf = *file_iter;

            if (lf->get_filename() == fn) {
                if (lf->get_format() != nullptr) {
                    retval = "info: log file already loaded";
                    break;
                }

                files_to_front.emplace_back(fn, file_loc);
                retval = "";
                break;
            }
        }
        if (file_iter == lnav_data.ld_active_files.fc_files.end()) {
            auto_mem<char> abspath;
            struct stat st;
            size_t url_index;

#ifdef HAVE_LIBCURL
            if (startswith(fn, "file:")) {
                auto_mem<CURLU> cu(curl_url_cleanup);
                cu = curl_url();
                auto set_rc = curl_url_set(cu, CURLUPART_URL, fn.c_str(), 0);
                if (set_rc != CURLUE_OK) {
                    return Err(lnav::console::user_message::error(
                                   attr_line_t("invalid URL: ")
                                       .append(lnav::roles::file(fn)))
                                   .with_reason(curl_url_strerror(set_rc)));
                }

                auto_mem<char> path_part;
                auto get_rc
                    = curl_url_get(cu, CURLUPART_PATH, path_part.out(), 0);
                if (get_rc != CURLUE_OK) {
                    return Err(lnav::console::user_message::error(
                                   attr_line_t("cannot get path from URL: ")
                                       .append(lnav::roles::file(fn)))
                                   .with_reason(curl_url_strerror(get_rc)));
                }
                auto_mem<char> frag_part;
                get_rc
                    = curl_url_get(cu, CURLUPART_FRAGMENT, frag_part.out(), 0);
                if (get_rc != CURLUE_OK && get_rc != CURLUE_NO_FRAGMENT) {
                    return Err(lnav::console::user_message::error(
                                   attr_line_t("cannot get fragment from URL: ")
                                       .append(lnav::roles::file(fn)))
                                   .with_reason(curl_url_strerror(get_rc)));
                }

                if (frag_part != nullptr && frag_part[0]) {
                    fn = fmt::format(
                        FMT_STRING("{}#{}"), path_part.in(), frag_part.in());
                } else {
                    fn = path_part;
                }
            }
#endif

            if (is_url(fn.c_str())) {
#ifndef HAVE_LIBCURL
                retval = "error: lnav was not compiled with libcurl";
#else
                if (!ec.ec_dry_run) {
                    auto ul = std::make_shared<url_loader>(fn);

                    lnav_data.ld_active_files.fc_file_names[ul->get_path()]
                        .with_filename(fn)
                        .with_init_location(file_loc);
                    lnav_data.ld_active_files.fc_files_generation += 1;
                    isc::to<curl_looper&, services::curl_streamer_t>().send(
                        [ul](auto& clooper) { clooper.add_request(ul); });
                    lnav_data.ld_files_to_front.emplace_back(fn, file_loc);
                    closed_files.push_back(fn);
                    retval = "info: opened URL";
                } else {
                    retval = "";
                }
#endif
            } else if ((url_index = fn.find("://")) != std::string::npos) {
                const auto& cfg
                    = injector::get<const lnav::url_handler::config&>();
                const auto HOST_REGEX
                    = lnav::pcre2pp::code::from_const("://(?:\\?|$)");

                auto find_res = HOST_REGEX.find_in(fn).ignore_error();
                if (find_res) {
                    fn.insert(url_index + 3, "localhost");
                }

                auto_mem<CURLU> cu(curl_url_cleanup);
                cu = curl_url();
                auto set_rc = curl_url_set(
                    cu, CURLUPART_URL, fn.c_str(), CURLU_NON_SUPPORT_SCHEME);
                if (set_rc != CURLUE_OK) {
                    return Err(lnav::console::user_message::error(
                                   attr_line_t("invalid URL: ")
                                       .append(lnav::roles::file(fn)))
                                   .with_reason(curl_url_strerror(set_rc)));
                }

                auto_mem<char> scheme_part(curl_free);
                auto get_rc
                    = curl_url_get(cu, CURLUPART_SCHEME, scheme_part.out(), 0);
                if (get_rc != CURLUE_OK) {
                    return Err(lnav::console::user_message::error(
                                   attr_line_t("cannot get scheme from URL: ")
                                       .append(lnav::roles::file(fn)))
                                   .with_reason(curl_url_strerror(set_rc)));
                }

                auto proto_iter = cfg.c_schemes.find(scheme_part.in());
                if (proto_iter == cfg.c_schemes.end()) {
                    return Err(
                        lnav::console::user_message::error(
                            attr_line_t("no defined handler for URL scheme: ")
                                .append(lnav::roles::file(scheme_part.in())))
                            .with_reason(curl_url_strerror(set_rc)));
                }

                auto path_and_args
                    = fmt::format(FMT_STRING("{} {}"),
                                  proto_iter->second.p_handler.pp_value,
                                  fn);

                exec_context::provenance_guard pg(&ec,
                                                  exec_context::file_open{fn});

                auto exec_res = execute_file(ec, path_and_args);
                if (exec_res.isErr()) {
                    return exec_res;
                }

                retval = "info: watching -- " + fn;
            } else if (lnav::filesystem::is_glob(fn.c_str())) {
                fc.fc_file_names.emplace(fn, loo);
                files_to_front.emplace_back(
                    loo.loo_filename.empty() ? fn : loo.loo_filename, file_loc);
                retval = "info: watching -- " + fn;
            } else if (stat(fn.c_str(), &st) == -1) {
                if (fn.find(':') != std::string::npos) {
                    fc.fc_file_names.emplace(fn, loo);
                    retval = "info: watching -- " + fn;
                } else {
                    auto um = lnav::console::user_message::error(
                                  attr_line_t("cannot open file: ")
                                      .append(lnav::roles::file(fn)))
                                  .with_errno_reason()
                                  .with_snippets(ec.ec_source)
                                  .with_help(
                                      "make sure the file exists and is "
                                      "accessible")
                                  .move();
                    return Err(um);
                }
            } else if (is_dev_null(st)) {
                return ec.make_error("cannot open /dev/null");
            } else if (S_ISFIFO(st.st_mode)) {
                auto_fd fifo_fd;

                if ((fifo_fd = open(fn.c_str(), O_RDONLY)) == -1) {
                    auto um = lnav::console::user_message::error(
                                  attr_line_t("cannot open FIFO: ")
                                      .append(lnav::roles::file(fn)))
                                  .with_errno_reason()
                                  .with_snippets(ec.ec_source)
                                  .move();
                    return Err(um);
                } else if (ec.ec_dry_run) {
                    retval = "";
                } else {
                    auto desc = fmt::format(FMT_STRING("FIFO [{}]"),
                                            lnav_data.ld_fifo_counter++);
                    if (prov) {
                        desc = prov->fo_name;
                    }
                    auto create_piper_res = lnav::piper::create_looper(
                        desc, std::move(fifo_fd), auto_fd{});
                    if (create_piper_res.isErr()) {
                        auto um = lnav::console::user_message::error(
                                      attr_line_t("cannot create piper: ")
                                          .append(lnav::roles::file(fn)))
                                      .with_reason(create_piper_res.unwrapErr())
                                      .with_snippets(ec.ec_source)
                                      .move();
                        return Err(um);
                    }
                    lnav_data.ld_active_files.fc_file_names[desc].with_piper(
                        create_piper_res.unwrap());
                }
            } else if ((abspath = realpath(fn.c_str(), nullptr)) == nullptr) {
                auto um = lnav::console::user_message::error(
                              attr_line_t("cannot open file: ")
                                  .append(lnav::roles::file(fn)))
                              .with_errno_reason()
                              .with_snippets(ec.ec_source)
                              .with_help(
                                  "make sure the file exists and is "
                                  "accessible")
                              .move();
                return Err(um);
            } else if (S_ISDIR(st.st_mode)) {
                std::string dir_wild(abspath.in());

                if (dir_wild[dir_wild.size() - 1] == '/') {
                    dir_wild.resize(dir_wild.size() - 1);
                }
                fc.fc_file_names.emplace(dir_wild + "/*", loo);
                retval = "info: watching -- " + dir_wild;
            } else if (!S_ISREG(st.st_mode)) {
                auto um = lnav::console::user_message::error(
                              attr_line_t("cannot open file: ")
                                  .append(lnav::roles::file(fn)))
                              .with_reason("not a regular file or directory")
                              .with_snippets(ec.ec_source)
                              .with_help(
                                  "only regular files, directories, and FIFOs "
                                  "can be opened")
                              .move();
                return Err(um);
            } else if (access(fn.c_str(), R_OK) == -1) {
                auto um = lnav::console::user_message::error(
                              attr_line_t("cannot read file: ")
                                  .append(lnav::roles::file(fn)))
                              .with_errno_reason()
                              .with_snippets(ec.ec_source)
                              .with_help(
                                  "make sure the file exists and is "
                                  "accessible")
                              .move();
                return Err(um);
            } else {
                fn = abspath.in();
                fc.fc_file_names.emplace(fn, loo);
                retval = "info: opened -- " + fn;
                files_to_front.emplace_back(fn, file_loc);

                closed_files.push_back(fn);
                if (!loo.loo_filename.empty()) {
                    closed_files.push_back(loo.loo_filename);
                }
                if (lnav_data.ld_rl_view != nullptr) {
                    lnav_data.ld_rl_view->set_alt_value(
                        HELP_MSG_1(X, "to close the file"));
                }
            }
        }
    }

    if (ec.ec_dry_run) {
        lnav_data.ld_preview_view[0].set_sub_source(
            &lnav_data.ld_preview_source[0]);
        lnav_data.ld_preview_source[0].clear();
        if (!fc.fc_file_names.empty()) {
            auto iter = fc.fc_file_names.begin();
            std::string fn_str = iter->first;

            if (fn_str.find(':') != std::string::npos) {
                auto id = lnav_data.ld_preview_generation;
                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_cylon(true)
                    .set_value("Loading %s...", fn_str.c_str());
                lnav_data.ld_preview_view[0].set_sub_source(
                    &lnav_data.ld_preview_source[0]);
                lnav_data.ld_preview_source[0].clear();

                isc::to<tailer::looper&, services::remote_tailer_t>().send(
                    [id, fn_str](auto& tlooper) {
                        auto rp_opt = humanize::network::path::from_str(fn_str);
                        if (rp_opt) {
                            tlooper.load_preview(id, *rp_opt);
                        }
                    });
                lnav_data.ld_preview_view[0].set_needs_update();
            } else if (lnav::filesystem::is_glob(fn_str)) {
                static_root_mem<glob_t, globfree> gl;

                if (glob(fn_str.c_str(), GLOB_NOCHECK, nullptr, gl.inout())
                    == 0)
                {
                    attr_line_t al;

                    for (size_t lpc = 0; lpc < gl->gl_pathc && lpc < 10; lpc++)
                    {
                        al.append(gl->gl_pathv[lpc]).append("\n");
                    }
                    if (gl->gl_pathc > 10) {
                        al.append(" ... ")
                            .append(lnav::roles::number(
                                std::to_string(gl->gl_pathc - 10)))
                            .append(" files not shown ...");
                    }
                    lnav_data.ld_preview_status_source[0]
                        .get_description()
                        .set_value("The following files will be loaded:");
                    lnav_data.ld_preview_view[0].set_sub_source(
                        &lnav_data.ld_preview_source[0]);
                    lnav_data.ld_preview_source[0].replace_with(al);
                } else {
                    return ec.make_error("failed to evaluate glob -- {}",
                                         fn_str);
                }
            } else {
                auto fn = std::filesystem::path(fn_str);
                auto detect_res = detect_file_format(fn);
                attr_line_t al;
                attr_line_builder alb(al);

                switch (detect_res.dffr_file_format) {
                    case file_format_t::ARCHIVE: {
                        auto describe_res = archive_manager::describe(fn);

                        if (describe_res.isOk()) {
                            auto arc_res = describe_res.unwrap();

                            if (arc_res.is<archive_manager::archive_info>()) {
                                auto ai
                                    = arc_res
                                          .get<archive_manager::archive_info>();
                                auto lines_remaining = size_t{9};

                                al.append("Archive: ")
                                    .append(
                                        lnav::roles::symbol(ai.ai_format_name))
                                    .append("\n");
                                for (const auto& entry : ai.ai_entries) {
                                    if (lines_remaining == 0) {
                                        break;
                                    }
                                    lines_remaining -= 1;

                                    char timebuf[64];
                                    sql_strftime(timebuf,
                                                 sizeof(timebuf),
                                                 entry.e_mtime,
                                                 0,
                                                 'T');
                                    al.append("    ")
                                        .append(entry.e_mode)
                                        .append(" ")
                                        .appendf(
                                            FMT_STRING("{:>8}"),
                                            humanize::file_size(
                                                entry.e_size.value(),
                                                humanize::alignment::columnar))
                                        .append(" ")
                                        .append(timebuf)
                                        .append(" ")
                                        .append(lnav::roles::file(entry.e_name))
                                        .append("\n");
                                }
                            }
                        } else {
                            al.append(describe_res.unwrapErr());
                        }
                        break;
                    }
                    case file_format_t::MULTIPLEXED:
                    case file_format_t::UNKNOWN: {
                        auto open_res
                            = lnav::filesystem::open_file(fn, O_RDONLY);

                        if (open_res.isErr()) {
                            return ec.make_error("unable to open -- {}", fn);
                        }
                        auto preview_fd = open_res.unwrap();
                        line_buffer lb;
                        file_range range;

                        lb.set_fd(preview_fd);
                        for (int lpc = 0; lpc < 10; lpc++) {
                            auto load_result = lb.load_next_line(range);

                            if (load_result.isErr()) {
                                break;
                            }

                            auto li = load_result.unwrap();

                            range = li.li_file_range;
                            if (!li.li_utf8_scan_result.is_valid()) {
                                range.fr_size = 16;
                            }
                            auto read_result = lb.read_range(range);
                            if (read_result.isErr()) {
                                break;
                            }

                            auto sbr = read_result.unwrap();
                            auto sf = sbr.to_string_fragment();
                            if (li.li_utf8_scan_result.is_valid()) {
                                alb.append(sf);
                            } else {
                                {
                                    auto ag = alb.with_attr(
                                        VC_ROLE.value(role_t::VCR_FILE_OFFSET));
                                    alb.appendf(FMT_STRING("{: >16x} "),
                                                range.fr_offset);
                                }
                                alb.append_as_hexdump(sf);
                                alb.append("\n");
                            }
                        }
                        break;
                    }
                    case file_format_t::SQLITE_DB: {
                        alb.append(fmt::to_string(detect_res.dffr_file_format));
                        break;
                    }
                    case file_format_t::REMOTE: {
                        break;
                    }
                }

                lnav_data.ld_preview_view[0].set_sub_source(
                    &lnav_data.ld_preview_source[0]);
                lnav_data.ld_preview_source[0].replace_with(al).set_text_format(
                    detect_text_format(al.get_string()));
                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_value("For file: %s", fn.c_str());
            }
        }
    } else {
        lnav_data.ld_files_to_front.insert(lnav_data.ld_files_to_front.end(),
                                           files_to_front.begin(),
                                           files_to_front.end());
        for (const auto& fn : closed_files) {
            lnav_data.ld_active_files.fc_closed_files.erase(fn);
        }

        lnav_data.ld_active_files.merge(fc);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_xopen(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");
    std::string retval;

    if (args.empty()) {
        args.emplace_back("filename");
        return Ok(std::string());
    }

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    if (args.size() < 2) {
        return ec.make_error("expecting file name to open");
    }

    std::vector<std::string> word_exp;
    std::string pat;
    file_collection fc;

    pat = trim(remaining_args(cmdline, args));

    shlex lexer(pat);
    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse file names")
                  .with_reason(split_err.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err)))
                  .move();

        return Err(um);
    }

    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });
    for (auto fn : split_args) {
        auto open_res = lnav::external_opener::for_href(fn);
        if (open_res.isErr()) {
            auto um = lnav::console::user_message::error(
                          attr_line_t("Unable to open file: ")
                              .append(lnav::roles::file(fn)))
                          .with_reason(open_res.unwrapErr())
                          .move();
            return Err(um);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_close(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");
    std::string retval;

    if (args.empty()) {
        args.emplace_back("loaded-files");
        return Ok(retval);
    }

    auto* tc = *lnav_data.ld_view_stack.top();
    std::vector<std::optional<std::filesystem::path>> actual_path_v;
    std::vector<std::string> fn_v;

    if (args.size() > 1) {
        auto lexer = shlex(cmdline);

        auto split_args_res = lexer.split(ec.create_resolver());
        if (split_args_res.isErr()) {
            auto split_err = split_args_res.unwrapErr();
            auto um = lnav::console::user_message::error(
                          "unable to parse file name")
                          .with_reason(split_err.te_msg)
                          .with_snippet(lnav::console::snippet::from(
                              SRC, lexer.to_attr_line(split_err)))
                          .move();

            return Err(um);
        }

        auto args = split_args_res.unwrap()
            | lnav::itertools::map(
                        [](const auto& elem) { return elem.se_value; });
        args.erase(args.begin());

        for (const auto& lf : lnav_data.ld_active_files.fc_files) {
            if (lf.get() == nullptr) {
                continue;
            }

            auto find_iter
                = find_if(args.begin(), args.end(), [&lf](const auto& arg) {
                      return fnmatch(arg.c_str(), lf->get_filename().c_str(), 0)
                          == 0;
                  });

            if (find_iter == args.end()) {
                continue;
            }

            actual_path_v.push_back(lf->get_actual_path());
            fn_v.emplace_back(lf->get_filename());
            if (!ec.ec_dry_run) {
                lnav_data.ld_active_files.request_close(lf);
            }
        }
    } else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
        auto& tss = lnav_data.ld_text_source;

        if (tss.empty()) {
            return ec.make_error("no text files are opened");
        } else if (!ec.ec_dry_run) {
            auto lf = tss.current_file();
            actual_path_v.emplace_back(lf->get_actual_path());
            fn_v.emplace_back(lf->get_filename());
            lnav_data.ld_active_files.request_close(lf);

            if (tss.size() == 1) {
                lnav_data.ld_view_stack.pop_back();
            }
        } else {
            retval = fmt::format(FMT_STRING("closing -- {}"),
                                 tss.current_file()->get_filename());
        }
    } else if (tc == &lnav_data.ld_views[LNV_LOG]) {
        if (tc->get_inner_height() == 0) {
            return ec.make_error("no log files loaded");
        } else {
            auto& lss = lnav_data.ld_log_source;
            auto vl = tc->get_selection();
            auto cl = lss.at(vl);
            auto lf = lss.find(cl);

            actual_path_v.push_back(lf->get_actual_path());
            fn_v.emplace_back(lf->get_filename());
            if (!ec.ec_dry_run) {
                lnav_data.ld_active_files.request_close(lf);
            }
        }
    } else {
        return ec.make_error("close must be run in the log or text file views");
    }
    if (!fn_v.empty()) {
        if (ec.ec_dry_run) {
            retval = "";
        } else {
            for (size_t lpc = 0; lpc < actual_path_v.size(); lpc++) {
                const auto& fn = fn_v[lpc];
                const auto& actual_path = actual_path_v[lpc];

                if (is_url(fn.c_str())) {
                    isc::to<curl_looper&, services::curl_streamer_t>().send(
                        [fn](auto& clooper) { clooper.close_request(fn); });
                }
                if (actual_path) {
                    lnav_data.ld_active_files.fc_file_names.erase(
                        actual_path.value().string());
                }
                lnav_data.ld_active_files.fc_closed_files.insert(fn);
            }
            retval = fmt::format(FMT_STRING("info: closed -- {}"),
                                 fmt::join(fn_v, ", "));
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_file_visibility(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");
    bool only_this_file = false;
    bool make_visible;
    std::string retval;

    if (args[0] == "show-file") {
        make_visible = true;
    } else if (args[0] == "show-only-this-file") {
        make_visible = true;
        only_this_file = true;
    } else {
        make_visible = false;
    }

    if (args.size() == 1 || only_this_file) {
        auto* tc = *lnav_data.ld_view_stack.top();
        std::shared_ptr<logfile> lf;

        if (tc == &lnav_data.ld_views[LNV_TEXT]) {
            const auto& tss = lnav_data.ld_text_source;

            if (tss.empty()) {
                return ec.make_error("no text files are opened");
            }
            lf = tss.current_file();
        } else if (tc == &lnav_data.ld_views[LNV_LOG]) {
            if (tc->get_inner_height() == 0) {
                return ec.make_error("no log files loaded");
            }
            auto& lss = lnav_data.ld_log_source;
            auto vl = tc->get_selection();
            auto cl = lss.at(vl);
            lf = lss.find(cl);
        } else {
            return ec.make_error(
                ":{} must be run in the log or text file views", args[0]);
        }

        if (!ec.ec_dry_run) {
            if (only_this_file) {
                for (const auto& ld : lnav_data.ld_log_source) {
                    ld->set_visibility(false);
                    ld->get_file_ptr()->set_indexing(false);
                }
            }
            lf->set_indexing(make_visible);
            lnav_data.ld_log_source.find_data(lf) |
                [make_visible](auto ld) { ld->set_visibility(make_visible); };
            tc->get_sub_source()->text_filters_changed();
        }
        retval = fmt::format(FMT_STRING("info: {} file -- {}"),
                             make_visible ? "showing" : "hiding",
                             lf->get_filename());
    } else {
        auto* top_tc = *lnav_data.ld_view_stack.top();
        int text_file_count = 0, log_file_count = 0;
        auto lexer = shlex(cmdline);

        auto split_args_res = lexer.split(ec.create_resolver());
        if (split_args_res.isErr()) {
            auto split_err = split_args_res.unwrapErr();
            auto um = lnav::console::user_message::error(
                          "unable to parse file name")
                          .with_reason(split_err.te_msg)
                          .with_snippet(lnav::console::snippet::from(
                              SRC, lexer.to_attr_line(split_err)))
                          .move();

            return Err(um);
        }

        auto args = split_args_res.unwrap()
            | lnav::itertools::map(
                        [](const auto& elem) { return elem.se_value; });
        args.erase(args.begin());

        for (const auto& lf : lnav_data.ld_active_files.fc_files) {
            if (lf.get() == nullptr) {
                continue;
            }

            auto ld_opt = lnav_data.ld_log_source.find_data(lf);

            if (!ld_opt || ld_opt.value()->ld_visible == make_visible) {
                continue;
            }

            auto find_iter
                = find_if(args.begin(), args.end(), [&lf](const auto& arg) {
                      return fnmatch(arg.c_str(), lf->get_filename().c_str(), 0)
                          == 0;
                  });

            if (find_iter == args.end()) {
                continue;
            }

            if (!ec.ec_dry_run) {
                ld_opt | [make_visible](auto ld) {
                    ld->get_file_ptr()->set_indexing(make_visible);
                    ld->set_visibility(make_visible);
                };
            }
            if (lf->get_format() != nullptr) {
                log_file_count += 1;
            } else {
                text_file_count += 1;
            }
        }
        if (!ec.ec_dry_run && log_file_count > 0) {
            lnav_data.ld_views[LNV_LOG]
                .get_sub_source()
                ->text_filters_changed();
            if (top_tc == &lnav_data.ld_views[LNV_TIMELINE]) {
                lnav_data.ld_views[LNV_TIMELINE]
                    .get_sub_source()
                    ->text_filters_changed();
            }
        }
        if (!ec.ec_dry_run && text_file_count > 0) {
            lnav_data.ld_views[LNV_TEXT]
                .get_sub_source()
                ->text_filters_changed();
        }
        retval = fmt::format(
            FMT_STRING("info: {} {:L} log files and {:L} text files"),
            make_visible ? "showing" : "hiding",
            log_file_count,
            text_file_count);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_hide_file(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("visible-files");
    } else {
        return com_file_visibility(ec, cmdline, args);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_show_file(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("hidden-files");
    } else {
        return com_file_visibility(ec, cmdline, args);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_show_only_this_file(exec_context& ec,
                        std::string cmdline,
                        std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else {
        return com_file_visibility(ec, cmdline, args);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_comment(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        return Ok(std::string());
    } else if (args.size() > 1) {
        if (ec.ec_dry_run) {
            return Ok(std::string());
        }
        auto* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return ec.make_error(
                "The :comment command only works in the log view");
        }
        auto& lss = lnav_data.ld_log_source;

        args[1] = trim(remaining_args(cmdline, args));
        auto unquoted = auto_buffer::alloc(args[1].size() + 1);
        auto unquoted_len = unquote_content(
            unquoted.in(), args[1].c_str(), args[1].size(), 0);
        unquoted.resize(unquoted_len + 1);

        tc->set_user_mark(&textview_curses::BM_META, tc->get_selection(), true);

        auto& line_meta = lss.get_bookmark_metadata(tc->get_selection());

        line_meta.bm_comment = unquoted.in();
        lss.set_line_meta_changed();
        lss.text_filters_changed();
        tc->reload_data();

        retval = "info: comment added to line";
    } else {
        return ec.make_error("expecting some comment text");
    }

    return Ok(retval);
}

static readline_context::prompt_result_t
com_comment_prompt(exec_context& ec, const std::string& cmdline)
{
    auto* tc = *lnav_data.ld_view_stack.top();

    if (tc != &lnav_data.ld_views[LNV_LOG]) {
        return {""};
    }
    auto& lss = lnav_data.ld_log_source;

    auto line_meta_opt = lss.find_bookmark_metadata(tc->get_selection());

    if (line_meta_opt && !line_meta_opt.value()->bm_comment.empty()) {
        auto trimmed_comment = trim(line_meta_opt.value()->bm_comment);
        auto buf = auto_buffer::alloc(trimmed_comment.size() + 16);
        quote_content(buf, trimmed_comment, 0);

        return {trim(cmdline) + " " + buf.to_string()};
    }

    return {""};
}

static Result<std::string, lnav::console::user_message>
com_clear_comment(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        return Ok(std::string());
    } else if (ec.ec_dry_run) {
        return Ok(std::string());
    } else {
        textview_curses* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return ec.make_error(
                "The :clear-comment command only works in the log "
                "view");
        }
        auto& lss = lnav_data.ld_log_source;

        auto line_meta_opt = lss.find_bookmark_metadata(tc->get_selection());
        if (line_meta_opt) {
            bookmark_metadata& line_meta = *(line_meta_opt.value());

            line_meta.bm_comment.clear();
            if (line_meta.empty(bookmark_metadata::categories::notes)) {
                tc->set_user_mark(
                    &textview_curses::BM_META, tc->get_selection(), false);
                if (line_meta.empty(bookmark_metadata::categories::any)) {
                    lss.erase_bookmark_metadata(tc->get_selection());
                }
            }

            lss.set_line_meta_changed();
            lss.text_filters_changed();
            tc->reload_data();

            retval = "info: cleared comment";
        }
        tc->search_new_data();
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_tag(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("tag");
        return Ok(std::string());
    } else if (args.size() > 1) {
        if (ec.ec_dry_run) {
            return Ok(std::string());
        }
        textview_curses* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return ec.make_error("The :tag command only works in the log view");
        }
        auto& lss = lnav_data.ld_log_source;

        tc->set_user_mark(&textview_curses::BM_META, tc->get_selection(), true);
        auto& line_meta = lss.get_bookmark_metadata(tc->get_selection());
        for (size_t lpc = 1; lpc < args.size(); lpc++) {
            std::string tag = args[lpc];

            if (!startswith(tag, "#")) {
                tag = "#" + tag;
            }
            bookmark_metadata::KNOWN_TAGS.insert(tag);
            line_meta.add_tag(tag);
        }
        tc->search_new_data();
        lss.set_line_meta_changed();
        lss.text_filters_changed();
        tc->reload_data();

        retval = "info: tag(s) added to line";
    } else {
        return ec.make_error("expecting one or more tags");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_untag(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("line-tags");
        return Ok(std::string());
    } else if (args.size() > 1) {
        if (ec.ec_dry_run) {
            return Ok(std::string());
        }
        textview_curses* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return ec.make_error(
                "The :untag command only works in the log view");
        }
        auto& lss = lnav_data.ld_log_source;

        auto line_meta_opt = lss.find_bookmark_metadata(tc->get_selection());
        if (line_meta_opt) {
            auto& line_meta = *(line_meta_opt.value());

            for (size_t lpc = 1; lpc < args.size(); lpc++) {
                std::string tag = args[lpc];

                if (!startswith(tag, "#")) {
                    tag = "#" + tag;
                }
                line_meta.remove_tag(tag);
            }
            if (line_meta.empty(bookmark_metadata::categories::notes)) {
                tc->set_user_mark(
                    &textview_curses::BM_META, tc->get_selection(), false);
            }
        }
        tc->search_new_data();
        lss.set_line_meta_changed();
        lss.text_filters_changed();
        tc->reload_data();

        retval = "info: tag(s) removed from line";
    } else {
        return ec.make_error("expecting one or more tags");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_delete_tags(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("tag");
        return Ok(std::string());
    } else if (args.size() > 1) {
        if (ec.ec_dry_run) {
            return Ok(std::string());
        }
        textview_curses* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return ec.make_error(
                "The :delete-tag command only works in the log "
                "view");
        }

        auto& known_tags = bookmark_metadata::KNOWN_TAGS;
        std::vector<std::string> tags;

        for (size_t lpc = 1; lpc < args.size(); lpc++) {
            std::string tag = args[lpc];

            if (!startswith(tag, "#")) {
                tag = "#" + tag;
            }
            if (known_tags.find(tag) == known_tags.end()) {
                return ec.make_error("Unknown tag -- {}", tag);
            }

            tags.emplace_back(tag);
            known_tags.erase(tag);
        }

        auto& lss = lnav_data.ld_log_source;
        auto& vbm = tc->get_bookmarks()[&textview_curses::BM_META];

        for (auto iter = vbm.begin(); iter != vbm.end();) {
            auto line_meta_opt = lss.find_bookmark_metadata(*iter);

            if (!line_meta_opt) {
                ++iter;
                continue;
            }

            auto& line_meta = line_meta_opt.value();
            for (const auto& tag : tags) {
                line_meta->remove_tag(tag);
            }

            if (line_meta->empty(bookmark_metadata::categories::notes)) {
                size_t off = std::distance(vbm.begin(), iter);
                auto vl = *iter;
                tc->set_user_mark(&textview_curses::BM_META, vl, false);
                if (line_meta->empty(bookmark_metadata::categories::any)) {
                    lss.erase_bookmark_metadata(vl);
                }

                iter = std::next(vbm.begin(), off);
            } else {
                ++iter;
            }
        }

        retval = "info: deleted tag(s)";
    } else {
        return ec.make_error("expecting one or more tags");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_partition_name(exec_context& ec,
                   std::string cmdline,
                   std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        return Ok(std::string());
    } else if (args.size() > 1) {
        if (ec.ec_dry_run) {
            retval = "";
        } else {
            textview_curses& tc = lnav_data.ld_views[LNV_LOG];
            logfile_sub_source& lss = lnav_data.ld_log_source;

            args[1] = trim(remaining_args(cmdline, args));

            tc.set_user_mark(
                &textview_curses::BM_PARTITION, tc.get_selection(), true);

            auto& line_meta = lss.get_bookmark_metadata(tc.get_selection());

            line_meta.bm_name = args[1];
            retval = "info: name set for partition";
        }
    } else {
        return ec.make_error("expecting partition name");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_clear_partition(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        return Ok(std::string());
    } else if (args.size() == 1) {
        textview_curses& tc = lnav_data.ld_views[LNV_LOG];
        logfile_sub_source& lss = lnav_data.ld_log_source;
        auto& bv = tc.get_bookmarks()[&textview_curses::BM_PARTITION];
        std::optional<vis_line_t> part_start;

        if (binary_search(bv.begin(), bv.end(), tc.get_selection())) {
            part_start = tc.get_selection();
        } else {
            part_start = bv.prev(tc.get_selection());
        }
        if (!part_start) {
            return ec.make_error("top line is not in a partition");
        }

        if (!ec.ec_dry_run) {
            auto& line_meta = lss.get_bookmark_metadata(part_start.value());

            line_meta.bm_name.clear();
            if (line_meta.empty(bookmark_metadata::categories::partition)) {
                tc.set_user_mark(
                    &textview_curses::BM_PARTITION, part_start.value(), false);
                if (line_meta.empty(bookmark_metadata::categories::any)) {
                    lss.erase_bookmark_metadata(part_start.value());
                }
            }

            retval = "info: cleared partition name";
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_summarize(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("colname");
        return Ok(retval);
    } else if (!setup_logline_table(ec)) {
        return ec.make_error("no log data available");
    } else if (args.size() == 1) {
        return ec.make_error("no columns specified");
    } else {
        auto_mem<char, sqlite3_free> query_frag;
        std::vector<std::string> other_columns;
        std::vector<std::string> num_columns;
        const auto& top_source = ec.ec_source.back();
        sql_progress_guard progress_guard(sql_progress,
                                          sql_progress_finished,
                                          top_source.s_location,
                                          top_source.s_content);
        auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
        int retcode;
        std::string query;

        query = "SELECT ";
        for (size_t lpc = 1; lpc < args.size(); lpc++) {
            if (lpc > 1)
                query += ", ";
            query += args[lpc];
        }
        query += " FROM logline ";

        retcode = sqlite3_prepare_v2(
            lnav_data.ld_db.in(), query.c_str(), -1, stmt.out(), nullptr);
        if (retcode != SQLITE_OK) {
            const char* errmsg = sqlite3_errmsg(lnav_data.ld_db);

            return ec.make_error("{}", errmsg);
        }

        switch (sqlite3_step(stmt.in())) {
            case SQLITE_OK:
            case SQLITE_DONE: {
                return ec.make_error("no data");
            } break;
            case SQLITE_ROW:
                break;
            default: {
                const char* errmsg;

                errmsg = sqlite3_errmsg(lnav_data.ld_db);
                return ec.make_error("{}", errmsg);
            } break;
        }

        if (ec.ec_dry_run) {
            return Ok(std::string());
        }

        for (int lpc = 0; lpc < sqlite3_column_count(stmt.in()); lpc++) {
            switch (sqlite3_column_type(stmt.in(), lpc)) {
                case SQLITE_INTEGER:
                case SQLITE_FLOAT:
                    num_columns.push_back(args[lpc + 1]);
                    break;
                default:
                    other_columns.push_back(args[lpc + 1]);
                    break;
            }
        }

        query = "SELECT";
        for (auto iter = other_columns.begin(); iter != other_columns.end();
             ++iter)
        {
            if (iter != other_columns.begin()) {
                query += ",";
            }
            query_frag
                = sqlite3_mprintf(" %s as \"c_%s\", count(*) as \"count_%s\"",
                                  iter->c_str(),
                                  iter->c_str(),
                                  iter->c_str());
            query += query_frag;
        }

        if (!other_columns.empty() && !num_columns.empty()) {
            query += ", ";
        }

        for (auto iter = num_columns.begin(); iter != num_columns.end(); ++iter)
        {
            if (iter != num_columns.begin()) {
                query += ",";
            }
            query_frag = sqlite3_mprintf(
                " sum(\"%s\"), "
                " min(\"%s\"), "
                " avg(\"%s\"), "
                " median(\"%s\"), "
                " stddev(\"%s\"), "
                " max(\"%s\") ",
                iter->c_str(),
                iter->c_str(),
                iter->c_str(),
                iter->c_str(),
                iter->c_str(),
                iter->c_str());
            query += query_frag;
        }

        query
            += (" FROM logline "
                "WHERE (logline.log_part is null or "
                "startswith(logline.log_part, '.') = 0) ");

        for (auto iter = other_columns.begin(); iter != other_columns.end();
             ++iter)
        {
            if (iter == other_columns.begin()) {
                query += " GROUP BY ";
            } else {
                query += ",";
            }
            query_frag = sqlite3_mprintf(" \"c_%s\"", iter->c_str());
            query += query_frag;
        }

        for (auto iter = other_columns.begin(); iter != other_columns.end();
             ++iter)
        {
            if (iter == other_columns.begin()) {
                query += " ORDER BY ";
            } else {
                query += ",";
            }
            query_frag = sqlite3_mprintf(
                " \"count_%s\" desc, \"c_%s\" collate "
                "naturalnocase asc",
                iter->c_str(),
                iter->c_str());
            query += query_frag;
        }
        log_debug("query %s", query.c_str());

        db_label_source& dls = lnav_data.ld_db_row_source;

        dls.clear();
        retcode = sqlite3_prepare_v2(
            lnav_data.ld_db.in(), query.c_str(), -1, stmt.out(), nullptr);

        if (retcode != SQLITE_OK) {
            const char* errmsg = sqlite3_errmsg(lnav_data.ld_db);

            return ec.make_error("{}", errmsg);
        } else if (stmt == nullptr) {
            retval = "";
        } else {
            bool done = false;

            ec.ec_sql_callback(ec, stmt.in());
            while (!done) {
                retcode = sqlite3_step(stmt.in());

                switch (retcode) {
                    case SQLITE_OK:
                    case SQLITE_DONE:
                        done = true;
                        break;

                    case SQLITE_ROW:
                        ec.ec_sql_callback(ec, stmt.in());
                        break;

                    default: {
                        const char* errmsg;

                        errmsg = sqlite3_errmsg(lnav_data.ld_db);
                        return ec.make_error("{}", errmsg);
                    }
                }
            }

            if (retcode == SQLITE_DONE) {
                lnav_data.ld_views[LNV_LOG].reload_data();
                lnav_data.ld_views[LNV_DB].reload_data();
                lnav_data.ld_views[LNV_DB].set_left(0);

                if (dls.dls_rows.size() > 0) {
                    ensure_view(&lnav_data.ld_views[LNV_DB]);
                }
            }

            lnav_data.ld_bottom_source.update_loading(0, 0);
            lnav_data.ld_status[LNS_BOTTOM].do_update();
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_add_test(exec_context& ec,
             std::string cmdline,
             std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (args.size() > 1) {
        return ec.make_error("not expecting any arguments");
    } else if (ec.ec_dry_run) {
    } else {
        textview_curses* tc = *lnav_data.ld_view_stack.top();

        bookmark_vector<vis_line_t>& bv
            = tc->get_bookmarks()[&textview_curses::BM_USER];
        bookmark_vector<vis_line_t>::iterator iter;

        for (iter = bv.begin(); iter != bv.end(); ++iter) {
            auto_mem<FILE> file(fclose);
            char path[PATH_MAX];
            std::string line;

            tc->grep_value_for_line(*iter, line);

            line.insert(0, 13, ' ');

            snprintf(path,
                     sizeof(path),
                     "%s/test/log-samples/sample-%s.txt",
                     getenv("LNAV_SRC"),
                     hasher().update(line).to_string().c_str());

            if ((file = fopen(path, "w")) == nullptr) {
                perror("fopen failed");
            } else {
                fprintf(file, "%s\n", line.c_str());
            }
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_switch_to_view(exec_context& ec,
                   std::string cmdline,
                   std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("viewname");
    } else if (args.size() > 1) {
        bool found = false;

        for (int lpc = 0; lnav_view_strings[lpc] && !found; lpc++) {
            if (strcasecmp(args[1].c_str(), lnav_view_strings[lpc]) == 0) {
                if (!ec.ec_dry_run) {
                    if (args[0] == "switch-to-view") {
                        ensure_view(&lnav_data.ld_views[lpc]);
                    } else {
                        toggle_view(&lnav_data.ld_views[lpc]);
                    }
                }
                found = true;
            }
        }

        if (!found) {
            return ec.make_error("invalid view name -- {}", args[1]);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_toggle_filtering(exec_context& ec,
                     std::string cmdline,
                     std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        auto tc = *lnav_data.ld_view_stack.top();
        auto tss = tc->get_sub_source();

        tss->toggle_apply_filters();
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_zoom_to(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("zoomlevel");
    } else if (ec.ec_dry_run) {
    } else if (args.size() > 1) {
        bool found = false;

        for (size_t lpc = 0; lpc < lnav_zoom_strings.size() && !found; lpc++) {
            if (strcasecmp(args[1].c_str(), lnav_zoom_strings[lpc].c_str())
                == 0)
            {
                auto& ss = *lnav_data.ld_spectro_source;
                struct timeval old_time;

                lnav_data.ld_zoom_level = lpc;

                auto& hist_view = lnav_data.ld_views[LNV_HISTOGRAM];

                if (hist_view.get_inner_height() > 0) {
                    auto old_time_opt = lnav_data.ld_hist_source2.time_for_row(
                        lnav_data.ld_views[LNV_HISTOGRAM].get_top());
                    if (old_time_opt) {
                        old_time = old_time_opt.value().ri_time;
                        rebuild_hist();
                        lnav_data.ld_hist_source2.row_for_time(old_time) |
                            [](auto new_top) {
                                lnav_data.ld_views[LNV_HISTOGRAM].set_top(
                                    new_top);
                            };
                    }
                }

                auto& spectro_view = lnav_data.ld_views[LNV_SPECTRO];

                if (spectro_view.get_inner_height() > 0) {
                    auto old_time_opt
                        = lnav_data.ld_spectro_source->time_for_row(
                            lnav_data.ld_views[LNV_SPECTRO].get_selection());
                    ss.ss_granularity = ZOOM_LEVELS[lnav_data.ld_zoom_level];
                    ss.invalidate();
                    spectro_view.reload_data();
                    if (old_time_opt) {
                        lnav_data.ld_spectro_source->row_for_time(
                            old_time_opt.value().ri_time)
                            | [](auto new_top) {
                                  lnav_data.ld_views[LNV_SPECTRO].set_selection(
                                      new_top);
                              };
                    }
                }

                lnav_data.ld_view_stack.set_needs_update();

                found = true;
            }
        }

        if (!found) {
            auto um = lnav::console::user_message::error(
                          attr_line_t("invalid zoom level: ")
                              .append(lnav::roles::symbol(args[1])))
                          .with_snippets(ec.ec_source)
                          .with_help(attr_line_t("available levels: ")
                                         .join(lnav_zoom_strings, ", "))
                          .move();
            return Err(um);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_reset_session(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        reset_session();
        lnav_data.ld_views[LNV_LOG].reload_data();
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_load_session(exec_context& ec,
                 std::string cmdline,
                 std::vector<std::string>& args)
{
    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        load_session();
        lnav::session::restore_view_states();
        lnav_data.ld_views[LNV_LOG].reload_data();
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_save_session(exec_context& ec,
                 std::string cmdline,
                 std::vector<std::string>& args)
{
    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        save_session();
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_export_session_to(exec_context& ec,
                      std::string cmdline,
                      std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("filename");
    } else if (!ec.ec_dry_run) {
        auto_mem<FILE> outfile(fclose);
        auto fn = trim(remaining_args(cmdline, args));
        auto to_term = false;

        if (fn == "-" || fn == "/dev/stdout") {
            auto ec_out = ec.get_output();

            if (!ec_out) {
                outfile = auto_mem<FILE>::leak(stdout);
                nodelay(lnav_data.ld_window, 0);
                endwin();
                struct termios curr_termios;
                tcgetattr(1, &curr_termios);
                curr_termios.c_oflag |= ONLCR | OPOST;
                tcsetattr(1, TCSANOW, &curr_termios);
                setvbuf(stdout, nullptr, _IONBF, 0);
                to_term = true;
                fprintf(outfile,
                        "\n---------------- Press any key to exit "
                        "lo-fi "
                        "display "
                        "----------------\n\n");
            } else {
                outfile = auto_mem<FILE>::leak(ec_out.value());
            }
            if (outfile.in() == stdout) {
                lnav_data.ld_stdout_used = true;
            }
        } else if (fn == "/dev/clipboard") {
            auto open_res = sysclip::open(sysclip::type_t::GENERAL);
            if (open_res.isErr()) {
                alerter::singleton().chime("cannot open clipboard");
                return ec.make_error("Unable to copy to clipboard: {}",
                                     open_res.unwrapErr());
            }
            outfile = open_res.unwrap();
        } else if (lnav_data.ld_flags & LNF_SECURE_MODE) {
            return ec.make_error("{} -- unavailable in secure mode", args[0]);
        } else {
            if ((outfile = fopen(fn.c_str(), "we")) == nullptr) {
                return ec.make_error("unable to open file -- {}", fn);
            }
            fchmod(fileno(outfile.in()), S_IRWXU);
        }

        auto export_res = lnav::session::export_to(outfile.in());

        fflush(outfile.in());
        if (to_term) {
            cbreak();
            getch();
            refresh();
            nodelay(lnav_data.ld_window, 1);
        }
        if (export_res.isErr()) {
            return Err(export_res.unwrapErr());
        }

        retval = fmt::format(
            FMT_STRING("info: wrote session commands to -- {}"), fn);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_set_min_log_level(exec_context& ec,
                      std::string cmdline,
                      std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("levelname");
    } else if (ec.ec_dry_run) {
        retval = "";
    } else if (args.size() == 2) {
        auto& lss = lnav_data.ld_log_source;
        auto new_level = string2level(args[1].c_str(), args[1].size(), false);
        lss.set_min_log_level(new_level);

        retval = ("info: minimum log level is now -- "
                  + std::string(level_names[new_level]));
    } else {
        return ec.make_error("expecting a log level name");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_toggle_field(exec_context& ec,
                 std::string cmdline,
                 std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("colname");
    } else if (args.size() < 2) {
        return ec.make_error("Expecting a log message field name");
    } else {
        auto* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            retval = "error: hiding fields only works in the log view";
        } else if (ec.ec_dry_run) {
            // TODO: highlight the fields to be hidden.
            retval = "";
        } else {
            auto& lss = lnav_data.ld_log_source;
            bool hide = args[0] == "hide-fields";
            std::vector<std::string> found_fields, missing_fields;

            for (int lpc = 1; lpc < (int) args.size(); lpc++) {
                intern_string_t name;
                std::shared_ptr<log_format> format;
                size_t dot;

                if ((dot = args[lpc].find('.')) != std::string::npos) {
                    const intern_string_t format_name
                        = intern_string::lookup(args[lpc].c_str(), dot);

                    format = log_format::find_root_format(format_name.get());
                    if (!format) {
                        return ec.make_error("unknown format -- {}",
                                             format_name.to_string());
                    }
                    name = intern_string::lookup(&(args[lpc].c_str()[dot + 1]),
                                                 args[lpc].length() - dot - 1);
                } else if (tc->get_inner_height() == 0) {
                    return ec.make_error("no log messages to hide");
                } else {
                    auto cl = lss.at(tc->get_selection());
                    auto lf = lss.find(cl);
                    format = lf->get_format();
                    name = intern_string::lookup(args[lpc]);
                }

                if (format->hide_field(name, hide)) {
                    found_fields.push_back(args[lpc]);
                    if (hide) {
                        if (lnav_data.ld_rl_view != nullptr) {
                            lnav_data.ld_rl_view->set_alt_value(
                                HELP_MSG_1(x,
                                           "to quickly show hidden "
                                           "fields"));
                        }
                    }
                    tc->set_needs_update();
                } else {
                    missing_fields.push_back(args[lpc]);
                }
            }

            if (missing_fields.empty()) {
                auto visibility = hide ? "hiding" : "showing";
                retval = fmt::format(FMT_STRING("info: {} field(s) -- {}"),
                                     visibility,
                                     fmt::join(found_fields, ", "));
            } else {
                return ec.make_error("unknown field(s) -- {}",
                                     fmt::join(missing_fields, ", "));
            }
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_hide_line(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("move-time");
    } else if (args.size() == 1) {
        auto* tc = *lnav_data.ld_view_stack.top();
        auto& lss = lnav_data.ld_log_source;

        if (tc == &lnav_data.ld_views[LNV_LOG]) {
            auto min_time_opt = lss.get_min_log_time();
            auto max_time_opt = lss.get_max_log_time();
            char min_time_str[32], max_time_str[32];

            if (min_time_opt) {
                sql_strftime(
                    min_time_str, sizeof(min_time_str), min_time_opt.value());
            }
            if (max_time_opt) {
                sql_strftime(
                    max_time_str, sizeof(max_time_str), max_time_opt.value());
            }
            if (min_time_opt && max_time_opt) {
                retval
                    = fmt::format(FMT_STRING("info: hiding lines before {} and "
                                             "after {}"),
                                  min_time_str,
                                  max_time_str);
            } else if (min_time_opt) {
                retval = fmt::format(FMT_STRING("info: hiding lines before {}"),
                                     min_time_str);
            } else if (max_time_opt) {
                retval = fmt::format(FMT_STRING("info: hiding lines after {}"),
                                     max_time_str);
            } else {
                retval
                    = "info: no lines hidden by time, pass an "
                      "absolute or "
                      "relative time";
            }
        } else {
            return ec.make_error(
                "hiding lines by time only works in the log view");
        }
    } else if (args.size() >= 2) {
        std::string all_args = remaining_args(cmdline, args);
        auto* tc = *lnav_data.ld_view_stack.top();
        auto* ttt = dynamic_cast<text_time_translator*>(tc->get_sub_source());
        auto& lss = lnav_data.ld_log_source;
        date_time_scanner dts;
        struct timeval tv_abs;
        std::optional<timeval> tv_opt;
        auto parse_res = relative_time::from_str(all_args);

        if (parse_res.isOk()) {
            if (ttt != nullptr) {
                if (tc->get_inner_height() > 0) {
                    struct exttm tm;

                    auto vl = tc->get_selection();
                    auto log_vl_ri = ttt->time_for_row(vl);
                    if (log_vl_ri) {
                        tm = exttm::from_tv(log_vl_ri.value().ri_time);
                        tv_opt = parse_res.unwrap().adjust(tm).to_timeval();
                    }
                }
            } else {
                return ec.make_error(
                    "relative time values only work in a "
                    "time-based view");
            }
        } else if (dts.convert_to_timeval(all_args, tv_abs)) {
            tv_opt = tv_abs;
        }

        if (tv_opt && !ec.ec_dry_run) {
            char time_text[256];
            std::string relation;

            sql_strftime(time_text, sizeof(time_text), tv_opt.value());
            if (args[0] == "hide-lines-before") {
                lss.set_min_log_time(tv_opt.value());
                relation = "before";
            } else {
                lss.set_max_log_time(tv_opt.value());
                relation = "after";
            }

            if (ttt != nullptr && tc != &lnav_data.ld_views[LNV_LOG]) {
                tc->get_sub_source()->text_filters_changed();
                tc->reload_data();
            }

            retval = fmt::format(
                FMT_STRING("info: hiding lines {} {}"), relation, time_text);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_show_lines(exec_context& ec,
               std::string cmdline,
               std::vector<std::string>& args)
{
    std::string retval = "info: showing lines";

    if (ec.ec_dry_run) {
        retval = "";
    } else if (!args.empty()) {
        logfile_sub_source& lss = lnav_data.ld_log_source;
        textview_curses* tc = *lnav_data.ld_view_stack.top();

        if (tc == &lnav_data.ld_views[LNV_LOG]) {
            lss.clear_min_max_log_times();
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_hide_unmarked(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    std::string retval = "info: hid unmarked lines";

    if (args.empty()) {
    } else if (ec.ec_dry_run) {
        retval = "";
    } else {
        auto* tc = *lnav_data.ld_view_stack.top();
        const auto& bv = tc->get_bookmarks()[&textview_curses::BM_USER];
        const auto& bv_expr
            = tc->get_bookmarks()[&textview_curses::BM_USER_EXPR];

        if (bv.empty() && bv_expr.empty()) {
            return ec.make_error("no lines have been marked");
        } else {
            lnav_data.ld_log_source.set_marked_only(true);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_show_unmarked(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    std::string retval = "info: showing unmarked lines";

    if (ec.ec_dry_run) {
        retval = "";
    } else {
        lnav_data.ld_log_source.set_marked_only(false);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_rebuild(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        rescan_files(true);
        rebuild_indexes_repeatedly();
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_cd(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");

    if (args.empty()) {
        args.emplace_back("dirname");
        return Ok(std::string());
    }

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    std::vector<std::string> word_exp;
    std::string pat;

    pat = trim(remaining_args(cmdline, args));

    shlex lexer(pat);
    auto split_args_res = lexer.split(ec.create_resolver());
    if (split_args_res.isErr()) {
        auto split_err = split_args_res.unwrapErr();
        auto um
            = lnav::console::user_message::error("unable to parse file name")
                  .with_reason(split_err.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err)))
                  .move();

        return Err(um);
    }

    auto split_args = split_args_res.unwrap()
        | lnav::itertools::map([](const auto& elem) { return elem.se_value; });

    if (split_args.size() != 1) {
        return ec.make_error("expecting a single argument");
    }

    struct stat st;

    if (stat(split_args[0].c_str(), &st) != 0) {
        return Err(ec.make_error_msg("cannot access -- {}", split_args[0])
                       .with_errno_reason());
    }

    if (!S_ISDIR(st.st_mode)) {
        return ec.make_error("{} is not a directory", split_args[0]);
    }

    if (!ec.ec_dry_run) {
        chdir(split_args[0].c_str());
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_sh(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    if (args.empty()) {
        args.emplace_back("filename");
        return Ok(std::string());
    }

    if (lnav_data.ld_flags & LNF_SECURE_MODE) {
        return ec.make_error("{} -- unavailable in secure mode", args[0]);
    }

    static size_t EXEC_COUNT = 0;

    if (!ec.ec_dry_run) {
        std::optional<std::string> name_flag;

        shlex lexer(cmdline);
        auto cmd_start = args[0].size();
        auto split_res = lexer.split(ec.create_resolver());
        if (split_res.isOk()) {
            auto flags = split_res.unwrap();
            if (flags.size() >= 2) {
                static const char* NAME_FLAG = "--name=";

                if (startswith(flags[1].se_value, NAME_FLAG)) {
                    name_flag = flags[1].se_value.substr(strlen(NAME_FLAG));
                    cmd_start = flags[1].se_origin.sf_end;
                }
            }
        }

        auto carg = trim(cmdline.substr(cmd_start));

        log_info("executing: %s", carg.c_str());

        auto child_fds_res
            = auto_pipe::for_child_fds(STDOUT_FILENO, STDERR_FILENO);
        if (child_fds_res.isErr()) {
            auto um = lnav::console::user_message::error(
                          "unable to create child pipes")
                          .with_reason(child_fds_res.unwrapErr())
                          .move();
            ec.add_error_context(um);
            return Err(um);
        }
        auto child_res = lnav::pid::from_fork();
        if (child_res.isErr()) {
            auto um
                = lnav::console::user_message::error("unable to fork() child")
                      .with_reason(child_res.unwrapErr())
                      .move();
            ec.add_error_context(um);
            return Err(um);
        }

        auto child_fds = child_fds_res.unwrap();
        auto child = child_res.unwrap();
        for (auto& child_fd : child_fds) {
            child_fd.after_fork(child.in());
        }
        if (child.in_child()) {
            auto dev_null = open("/dev/null", O_RDONLY | O_CLOEXEC);

            dup2(dev_null, STDIN_FILENO);
            const char* exec_args[] = {
                getenv_opt("SHELL").value_or("bash"),
                "-c",
                carg.c_str(),
                nullptr,
            };

            for (const auto& pair : ec.ec_local_vars.top()) {
                pair.second.match(
                    [&pair](const std::string& val) {
                        setenv(pair.first.c_str(), val.c_str(), 1);
                    },
                    [&pair](const string_fragment& sf) {
                        setenv(pair.first.c_str(), sf.to_string().c_str(), 1);
                    },
                    [](null_value_t) {},
                    [&pair](int64_t val) {
                        setenv(
                            pair.first.c_str(), fmt::to_string(val).c_str(), 1);
                    },
                    [&pair](double val) {
                        setenv(
                            pair.first.c_str(), fmt::to_string(val).c_str(), 1);
                    });
            }

            execvp(exec_args[0], (char**) exec_args);
            _exit(EXIT_FAILURE);
        }

        std::string display_name;
        auto open_prov = ec.get_provenance<exec_context::file_open>();
        if (open_prov) {
            if (name_flag) {
                display_name = fmt::format(
                    FMT_STRING("{}/{}"), open_prov->fo_name, name_flag.value());
            } else {
                display_name = open_prov->fo_name;
            }
        } else if (name_flag) {
            display_name = name_flag.value();
        } else {
            display_name
                = fmt::format(FMT_STRING("sh-{} {}"), EXEC_COUNT++, carg);
        }

        auto name_base = display_name;
        size_t name_counter = 0;

        while (true) {
            auto fn_iter
                = lnav_data.ld_active_files.fc_file_names.find(display_name);
            if (fn_iter == lnav_data.ld_active_files.fc_file_names.end()) {
                break;
            }
            name_counter += 1;
            display_name
                = fmt::format(FMT_STRING("{} [{}]"), name_base, name_counter);
        }

        auto create_piper_res
            = lnav::piper::create_looper(display_name,
                                         std::move(child_fds[0].read_end()),
                                         std::move(child_fds[1].read_end()));

        if (create_piper_res.isErr()) {
            auto um
                = lnav::console::user_message::error("unable to create piper")
                      .with_reason(create_piper_res.unwrapErr())
                      .move();
            ec.add_error_context(um);
            return Err(um);
        }

        lnav_data.ld_active_files.fc_file_names[display_name].with_piper(
            create_piper_res.unwrap());
        lnav_data.ld_child_pollers.emplace_back(child_poller{
            display_name,
            std::move(child),
            [](auto& fc, auto& child) {},
        });
        lnav_data.ld_files_to_front.emplace_back(display_name,
                                                 file_location_t{});

        if (lnav_data.ld_rl_view != nullptr) {
            lnav_data.ld_rl_view->set_alt_value(
                HELP_MSG_CTRL(C, "to send SIGINT to child process"));
        }
        return Ok(fmt::format(FMT_STRING("info: executing -- {}"), carg));
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_shexec(exec_context& ec,
           std::string cmdline,
           std::vector<std::string>& args)
{
    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        log_perror(system(cmdline.substr(args[0].size()).c_str()));
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_poll_now(exec_context& ec,
             std::string cmdline,
             std::vector<std::string>& args)
{
    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        isc::to<curl_looper&, services::curl_streamer_t>().send_and_wait(
            [](auto& clooper) { clooper.process_all(); });
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_test_comment(exec_context& ec,
                 std::string cmdline,
                 std::vector<std::string>& args)
{
    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_redraw(exec_context& ec,
           std::string cmdline,
           std::vector<std::string>& args)
{
    if (args.empty()) {
    } else if (ec.ec_dry_run) {
    } else if (lnav_data.ld_window) {
        redrawwin(lnav_data.ld_window);
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_echo(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval = "error: expecting a message";

    if (args.empty()) {
    } else if (args.size() >= 1) {
        bool lf = true;
        std::string src;

        if (args.size() > 2 && args[1] == "-n") {
            std::string::size_type index_in_cmdline = cmdline.find(args[1]);

            lf = false;
            src = cmdline.substr(index_in_cmdline + args[1].length() + 1);
        } else if (args.size() >= 2) {
            src = cmdline.substr(args[0].length() + 1);
        } else {
            src = "";
        }

        auto lexer = shlex(src);
        lexer.eval(retval, ec.create_resolver());

        auto ec_out = ec.get_output();
        if (ec.ec_dry_run) {
            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "The text to output:");
            lnav_data.ld_preview_view[0].set_sub_source(
                &lnav_data.ld_preview_source[0]);
            lnav_data.ld_preview_source[0].replace_with(attr_line_t(retval));
            retval = "";
        } else if (ec_out) {
            FILE* outfile = *ec_out;

            if (outfile == stdout) {
                lnav_data.ld_stdout_used = true;
            }

            fprintf(outfile, "%s", retval.c_str());
            if (lf) {
                putc('\n', outfile);
            }
            fflush(outfile);

            retval = "";
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_alt_msg(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
    } else if (ec.ec_dry_run) {
        retval = "";
    } else if (args.size() == 1) {
        if (lnav_data.ld_rl_view != nullptr) {
            lnav_data.ld_rl_view->set_alt_value("");
        }
        retval = "";
    } else {
        std::string msg = remaining_args(cmdline, args);

        if (lnav_data.ld_rl_view != nullptr) {
            lnav_data.ld_rl_view->set_alt_value(msg);
        }

        retval = "";
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_eval(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("*");
    } else if (args.size() > 1) {
        static intern_string_t EVAL_SRC = intern_string::lookup(":eval");

        std::string all_args = remaining_args(cmdline, args);
        std::string expanded_cmd;
        shlex lexer(all_args.c_str(), all_args.size());

        log_debug("Evaluating: %s", all_args.c_str());
        if (!lexer.eval(expanded_cmd,
                        {
                            &ec.ec_local_vars.top(),
                            &ec.ec_global_vars,
                        }))
        {
            return ec.make_error("invalid arguments");
        }
        log_debug("Expanded command to evaluate: %s", expanded_cmd.c_str());

        if (expanded_cmd.empty()) {
            return ec.make_error("empty result after evaluation");
        }

        if (ec.ec_dry_run) {
            attr_line_t al(expanded_cmd);

            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "The command to be executed:");

            lnav_data.ld_preview_view[0].set_sub_source(
                &lnav_data.ld_preview_source[0]);
            lnav_data.ld_preview_source[0].replace_with(al);

            return Ok(std::string());
        }

        auto src_guard = ec.enter_source(EVAL_SRC, 1, expanded_cmd);
        auto content = string_fragment::from_str(expanded_cmd);
        multiline_executor me(ec, ":eval");
        for (auto line : content.split_lines()) {
            TRY(me.push_back(line));
        }
        retval = TRY(me.final());
    } else {
        return ec.make_error("expecting a command or query to evaluate");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_config(exec_context& ec,
           std::string cmdline,
           std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("config-option");
    } else if (args.size() > 1) {
        static const auto INPUT_SRC = intern_string::lookup("input");

        yajlpp_parse_context ypc(INPUT_SRC, &lnav_config_handlers);
        std::vector<lnav::console::user_message> errors, errors_ignored;
        std::string option = args[1];

        lnav_config = rollback_lnav_config;
        ypc.set_path(option)
            .with_obj(lnav_config)
            .with_error_reporter([&errors](const auto& ypc, auto msg) {
                if (msg.um_level == lnav::console::user_message::level::error) {
                    errors.push_back(msg);
                }
            });
        ypc.ypc_active_paths.insert(option);
        ypc.update_callbacks();

        const auto* jph = ypc.ypc_current_handler;

        if (jph == nullptr && !ypc.ypc_handler_stack.empty()) {
            jph = ypc.ypc_handler_stack.back();
        }

        if (jph != nullptr) {
            yajlpp_gen gen;
            yajlpp_gen_context ygc(gen, lnav_config_handlers);
            yajl_gen_config(gen, yajl_gen_beautify, 1);
            ygc.with_context(ypc);

            if (ypc.ypc_current_handler == nullptr) {
                ygc.gen();
            } else {
                jph->gen(ygc, gen);
            }

            auto old_value = gen.to_string_fragment().to_string();

            if (args.size() == 2 || ypc.ypc_current_handler == nullptr) {
                lnav_config = rollback_lnav_config;
                reload_config(errors);

                if (ec.ec_dry_run) {
                    attr_line_t al(old_value);

                    lnav_data.ld_preview_view[0].set_sub_source(
                        &lnav_data.ld_preview_source[0]);
                    lnav_data.ld_preview_source[0]
                        .replace_with(al)
                        .set_text_format(detect_text_format(old_value))
                        .truncate_to(10);
                    lnav_data.ld_preview_status_source[0]
                        .get_description()
                        .set_value("Value of option: %s", option.c_str());

                    char help_text[1024];

                    snprintf(help_text,
                             sizeof(help_text),
                             ANSI_BOLD("%s") " " ANSI_UNDERLINE("%s") " -- %s",
                             jph->jph_property.c_str(),
                             jph->jph_synopsis,
                             jph->jph_description);

                    retval = help_text;
                } else {
                    retval = fmt::format(
                        FMT_STRING("{} = {}"), option, trim(old_value));
                }
            } else if (lnav_data.ld_flags & LNF_SECURE_MODE
                       && !startswith(option, "/ui/"))
            {
                return ec.make_error(":config {} -- unavailable in secure mode",
                                     option);
            } else {
                auto value = remaining_args(cmdline, args, 2);
                bool changed = false;

                if (ec.ec_dry_run) {
                    char help_text[1024];

                    snprintf(help_text,
                             sizeof(help_text),
                             ANSI_BOLD("%s %s") " -- %s",
                             jph->jph_property.c_str(),
                             jph->jph_synopsis,
                             jph->jph_description);

                    retval = help_text;
                }

                if (ypc.ypc_current_handler->jph_callbacks.yajl_string) {
                    ypc.ypc_callbacks.yajl_string(
                        &ypc,
                        (const unsigned char*) value.c_str(),
                        value.size());
                    changed = true;
                } else if (ypc.ypc_current_handler->jph_callbacks.yajl_integer)
                {
                    auto scan_res = scn::scan_value<int64_t>(value);
                    if (!scan_res || !scan_res.empty()) {
                        return ec.make_error("expecting an integer, found: {}",
                                             value);
                    }
                    ypc.ypc_callbacks.yajl_integer(&ypc, scan_res.value());
                    changed = true;
                } else if (ypc.ypc_current_handler->jph_callbacks.yajl_boolean)
                {
                    bool bvalue = false;

                    if (strcasecmp(value.c_str(), "true") == 0) {
                        bvalue = true;
                    }
                    ypc.ypc_callbacks.yajl_boolean(&ypc, bvalue);
                    changed = true;
                } else {
                    return ec.make_error("unhandled type");
                }

                while (!errors.empty()) {
                    if (errors.back().um_level
                        == lnav::console::user_message::level::error)
                    {
                        break;
                    } else {
                        errors.pop_back();
                    }
                }

                if (!errors.empty()) {
                    return Err(errors.back());
                }

                if (changed) {
                    intern_string_t path = intern_string::lookup(option);

                    lnav_config_locations[path]
                        = ec.ec_source.back().s_location;
                    reload_config(errors);

                    while (!errors.empty()) {
                        if (errors.back().um_level
                            == lnav::console::user_message::level::error)
                        {
                            break;
                        } else {
                            errors.pop_back();
                        }
                    }

                    if (!errors.empty()) {
                        lnav_config = rollback_lnav_config;
                        reload_config(errors_ignored);
                        return Err(errors.back());
                    }
                    if (!ec.ec_dry_run) {
                        retval = "info: changed config option -- " + option;
                        rollback_lnav_config = lnav_config;
                        if (!(lnav_data.ld_flags & LNF_SECURE_MODE)) {
                            save_config();
                        }
                    }
                }
            }
        } else {
            return ec.make_error("unknown configuration option -- {}", option);
        }
    } else {
        return ec.make_error(
            "expecting a configuration option to read or write");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_reset_config(exec_context& ec,
                 std::string cmdline,
                 std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("config-option");
    } else if (args.size() == 1) {
        return ec.make_error("expecting a configuration option to reset");
    } else {
        static const auto INPUT_SRC = intern_string::lookup("input");

        yajlpp_parse_context ypc(INPUT_SRC, &lnav_config_handlers);
        std::string option = args[1];

        while (!option.empty() && option.back() == '/') {
            option.pop_back();
        }
        lnav_config = rollback_lnav_config;
        ypc.set_path(option).with_obj(lnav_config);
        ypc.ypc_active_paths.insert(option);
        ypc.update_callbacks();

        if (option == "*"
            || (ypc.ypc_current_handler != nullptr
                || !ypc.ypc_handler_stack.empty()))
        {
            if (!ec.ec_dry_run) {
                reset_config(option);
                rollback_lnav_config = lnav_config;
                if (!(lnav_data.ld_flags & LNF_SECURE_MODE)) {
                    save_config();
                }
            }
            if (option == "*") {
                retval = "info: reset all options";
            } else {
                retval = "info: reset option -- " + option;
            }
        } else {
            return ec.make_error("unknown configuration option -- {}", option);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_spectrogram(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    std::string retval;

    if (args.empty()) {
        args.emplace_back("numeric-colname");
    } else if (ec.ec_dry_run) {
        retval = "";
    } else if (args.size() == 2) {
        auto colname = remaining_args(cmdline, args);
        auto& ss = *lnav_data.ld_spectro_source;
        bool found = false;

        ss.ss_granularity = ZOOM_LEVELS[lnav_data.ld_zoom_level];
        if (ss.ss_value_source != nullptr) {
            delete ss.ss_value_source;
            ss.ss_value_source = nullptr;
        }
        ss.invalidate();

        if (*lnav_data.ld_view_stack.top() == &lnav_data.ld_views[LNV_DB]) {
            auto dsvs = std::make_unique<db_spectro_value_source>(colname);

            if (dsvs->dsvs_error_msg) {
                return Err(
                    dsvs->dsvs_error_msg.value().with_snippets(ec.ec_source));
            }
            ss.ss_value_source = dsvs.release();
            found = true;
        } else {
            auto lsvs = std::make_unique<log_spectro_value_source>(
                intern_string::lookup(colname));

            if (!lsvs->lsvs_found) {
                return ec.make_error("unknown numeric message field -- {}",
                                     colname);
            }
            ss.ss_value_source = lsvs.release();
            found = true;
        }

        if (found) {
            lnav_data.ld_views[LNV_SPECTRO].reload_data();
            ss.text_selection_changed(lnav_data.ld_views[LNV_SPECTRO]);
            ensure_view(&lnav_data.ld_views[LNV_SPECTRO]);

            if (lnav_data.ld_rl_view != nullptr) {
                lnav_data.ld_rl_view->set_alt_value(
                    HELP_MSG_2(z, Z, "to zoom in/out"));
            }

            retval = "info: visualizing field -- " + colname;
        }
    } else {
        return ec.make_error("expecting a message field name");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_quit(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        lnav_data.ld_looping = false;
    }
    return Ok(std::string());
}

static void
breadcrumb_prompt(std::vector<std::string>& args)
{
    set_view_mode(ln_mode_t::BREADCRUMBS);
}

static void
command_prompt(std::vector<std::string>& args)
{
    static const char* TEXT_VIEW_MODES[] = {"raw", "rendered", nullptr};

    auto* tc = *lnav_data.ld_view_stack.top();
    auto* rlc = lnav_data.ld_rl_view;

    rlc->clear_possibilities(ln_mode_t::COMMAND, "move-args");
    rlc->add_possibility(
        ln_mode_t::COMMAND, "text-view-modes", TEXT_VIEW_MODES);
    if (lnav_data.ld_views[LNV_LOG].get_inner_height() > 0) {
        static const char* MOVE_TIMES[]
            = {"here", "now", "today", "yesterday", nullptr};

        logfile_sub_source& lss = lnav_data.ld_log_source;
        textview_curses& log_view = lnav_data.ld_views[LNV_LOG];
        content_line_t cl = lss.at(log_view.get_selection());
        std::shared_ptr<logfile> lf = lss.find(cl);
        auto ll = lf->begin() + cl;
        log_data_helper ldh(lss);

        lnav_data.ld_exec_context.ec_top_line = tc->get_selection();

        lnav_data.ld_rl_view->clear_possibilities(ln_mode_t::COMMAND,
                                                  "numeric-colname");
        lnav_data.ld_rl_view->clear_possibilities(ln_mode_t::COMMAND,
                                                  "colname");

        ldh.parse_line(log_view.get_selection(), true);

        if (tc == &lnav_data.ld_views[LNV_DB]) {
            db_label_source& dls = lnav_data.ld_db_row_source;

            for (auto& dls_header : dls.dls_headers) {
                if (!dls_header.hm_graphable) {
                    continue;
                }

                lnav_data.ld_rl_view->add_possibility(
                    ln_mode_t::COMMAND, "numeric-colname", dls_header.hm_name);
            }
        } else {
            for (auto& ldh_line_value : ldh.ldh_line_values.lvv_values) {
                auto& meta = ldh_line_value.lv_meta;

                if (!meta.lvm_format) {
                    continue;
                }

                const auto* stats
                    = meta.lvm_format.value()->stats_for_value(meta.lvm_name);

                if (stats == nullptr) {
                    continue;
                }

                lnav_data.ld_rl_view->add_possibility(
                    ln_mode_t::COMMAND,
                    "numeric-colname",
                    meta.lvm_name.to_string());
            }
        }

        for (auto& cn_name : ldh.ldh_namer->cn_names) {
            lnav_data.ld_rl_view->add_possibility(
                ln_mode_t::COMMAND, "colname", cn_name.to_string());
        }
        for (const auto& iter : ldh.ldh_namer->cn_builtin_names) {
            if (iter == column_namer::BUILTIN_COL) {
                continue;
            }
            lnav_data.ld_rl_view->add_possibility(
                ln_mode_t::COMMAND, "colname", iter.to_string());
        }

        ldh.clear();

        rlc->clear_possibilities(ln_mode_t::COMMAND, "move-time");
        rlc->add_possibility(ln_mode_t::COMMAND, "move-time", MOVE_TIMES);
        rlc->add_possibility(ln_mode_t::COMMAND, "move-args", MOVE_TIMES);
        rlc->clear_possibilities(ln_mode_t::COMMAND, "line-time");
        {
            struct timeval tv = lf->get_time_offset();
            char buffer[64];

            sql_strftime(
                buffer, sizeof(buffer), ll->get_time(), ll->get_millis(), 'T');
            rlc->add_possibility(ln_mode_t::COMMAND, "line-time", buffer);
            rlc->add_possibility(ln_mode_t::COMMAND, "move-args", buffer);
            rlc->add_possibility(ln_mode_t::COMMAND, "move-time", buffer);
            sql_strftime(buffer,
                         sizeof(buffer),
                         ll->get_time() - tv.tv_sec,
                         ll->get_millis() - (tv.tv_usec / 1000),
                         'T');
            rlc->add_possibility(ln_mode_t::COMMAND, "line-time", buffer);
            rlc->add_possibility(ln_mode_t::COMMAND, "move-args", buffer);
            rlc->add_possibility(ln_mode_t::COMMAND, "move-time", buffer);
        }
    }

    rollback_lnav_config = lnav_config;
    lnav_data.ld_doc_status_source.set_title("Command Help");
    lnav_data.ld_doc_status_source.set_description(
        " See " ANSI_BOLD("https://docs.lnav.org/en/latest/"
                          "commands.html") " for more details");
    add_view_text_possibilities(lnav_data.ld_rl_view,
                                ln_mode_t::COMMAND,
                                "filter",
                                tc,
                                text_quoting::none);
    lnav_data.ld_rl_view->add_possibility(
        ln_mode_t::COMMAND, "filter", tc->get_current_search());
    add_filter_possibilities(tc);
    add_mark_possibilities();
    add_config_possibilities();
    add_env_possibilities(ln_mode_t::COMMAND);
    add_tag_possibilities();
    add_file_possibilities();
    add_recent_netlocs_possibilities();
    add_tz_possibilities(ln_mode_t::COMMAND);

    auto* ta = dynamic_cast<text_anchors*>(tc->get_sub_source());
    if (ta != nullptr) {
        rlc->add_possibility(
            ln_mode_t::COMMAND, "move-args", ta->get_anchors());
    }

    if (tc == &lnav_data.ld_views[LNV_LOG]) {
        add_filter_expr_possibilities(
            lnav_data.ld_rl_view, ln_mode_t::COMMAND, "filter-expr-syms");
    }
    lnav_data.ld_mode = ln_mode_t::COMMAND;
    lnav_data.ld_rl_view->focus(ln_mode_t::COMMAND,
                                cget(args, 2).value_or(":"),
                                cget(args, 3).value_or(""));

    rl_set_help();
}

static void
script_prompt(std::vector<std::string>& args)
{
    textview_curses* tc = *lnav_data.ld_view_stack.top();
    auto& scripts = injector::get<available_scripts&>();

    lnav_data.ld_mode = ln_mode_t::EXEC;

    lnav_data.ld_exec_context.ec_top_line = tc->get_selection();
    lnav_data.ld_rl_view->clear_possibilities(ln_mode_t::EXEC, "__command");
    find_format_scripts(lnav_data.ld_config_paths, scripts);
    for (const auto& iter : scripts.as_scripts) {
        lnav_data.ld_rl_view->add_possibility(
            ln_mode_t::EXEC, "__command", iter.first);
    }
    add_view_text_possibilities(
        lnav_data.ld_rl_view, ln_mode_t::EXEC, "*", tc, text_quoting::regex);
    add_env_possibilities(ln_mode_t::EXEC);
    lnav_data.ld_rl_view->focus(ln_mode_t::EXEC,
                                cget(args, 2).value_or("|"),
                                cget(args, 3).value_or(""));
    lnav_data.ld_bottom_source.set_prompt(
        "Enter a script to execute: (Press " ANSI_BOLD("CTRL+]") " to abort)");
}

static void
search_prompt(std::vector<std::string>& args)
{
    textview_curses* tc = *lnav_data.ld_view_stack.top();

    lnav_data.ld_mode = ln_mode_t::SEARCH;
    lnav_data.ld_search_start_line = tc->get_selection();
    add_view_text_possibilities(
        lnav_data.ld_rl_view, ln_mode_t::SEARCH, "*", tc, text_quoting::regex);
    lnav_data.ld_rl_view->focus(ln_mode_t::SEARCH,
                                cget(args, 2).value_or("/"),
                                cget(args, 3).value_or(""));
    lnav_data.ld_doc_status_source.set_title("Syntax Help");
    lnav_data.ld_doc_status_source.set_description("");
    rl_set_help();
    lnav_data.ld_bottom_source.set_prompt(
        "Search for:  "
        "(Press " ANSI_BOLD("CTRL+J") " to jump to a previous hit and "
        ANSI_BOLD("CTRL+]") " to abort)");
}

static void
search_filters_prompt(std::vector<std::string>& args)
{
    lnav_data.ld_mode = ln_mode_t::SEARCH_FILTERS;
    lnav_data.ld_filter_view.reload_data();
    add_view_text_possibilities(lnav_data.ld_rl_view,
                                ln_mode_t::SEARCH_FILTERS,
                                "*",
                                &lnav_data.ld_filter_view,
                                text_quoting::regex);
    lnav_data.ld_rl_view->focus(ln_mode_t::SEARCH_FILTERS,
                                cget(args, 2).value_or("/"),
                                cget(args, 3).value_or(""));
    lnav_data.ld_bottom_source.set_prompt(
        "Search for:  "
        "(Press " ANSI_BOLD("CTRL+J") " to jump to a previous hit and "
        ANSI_BOLD("CTRL+]") " to abort)");
}

static void
search_files_prompt(std::vector<std::string>& args)
{
    static const std::regex re_escape(R"(([.\^$*+?()\[\]{}\\|]))");

    lnav_data.ld_mode = ln_mode_t::SEARCH_FILES;
    for (const auto& lf : lnav_data.ld_active_files.fc_files) {
        auto path = lnav::pcre2pp::quote(lf->get_unique_path().string());
        lnav_data.ld_rl_view->add_possibility(
            ln_mode_t::SEARCH_FILES, "*", path);
    }
    lnav_data.ld_rl_view->focus(ln_mode_t::SEARCH_FILES,
                                cget(args, 2).value_or("/"),
                                cget(args, 3).value_or(""));
    lnav_data.ld_bottom_source.set_prompt(
        "Search for:  "
        "(Press " ANSI_BOLD("CTRL+J") " to jump to a previous hit and "
        ANSI_BOLD("CTRL+]") " to abort)");
}

static void
search_spectro_details_prompt(std::vector<std::string>& args)
{
    lnav_data.ld_mode = ln_mode_t::SEARCH_SPECTRO_DETAILS;
    add_view_text_possibilities(lnav_data.ld_rl_view,
                                ln_mode_t::SEARCH_SPECTRO_DETAILS,
                                "*",
                                &lnav_data.ld_spectro_details_view,
                                text_quoting::regex);
    lnav_data.ld_rl_view->focus(ln_mode_t::SEARCH_SPECTRO_DETAILS,
                                cget(args, 2).value_or("/"),
                                cget(args, 3).value_or(""));
    lnav_data.ld_bottom_source.set_prompt(
        "Search for:  "
        "(Press " ANSI_BOLD("CTRL+J") " to jump to a previous hit and "
        ANSI_BOLD("CTRL+]") " to abort)");
}

static void
sql_prompt(std::vector<std::string>& args)
{
    auto* tc = *lnav_data.ld_view_stack.top();
    auto& log_view = lnav_data.ld_views[LNV_LOG];

    lnav_data.ld_exec_context.ec_top_line = tc->get_selection();

    lnav_data.ld_mode = ln_mode_t::SQL;
    setup_logline_table(lnav_data.ld_exec_context);
    lnav_data.ld_rl_view->focus(ln_mode_t::SQL,
                                cget(args, 2).value_or(";"),
                                cget(args, 3).value_or(""));

    lnav_data.ld_doc_status_source.set_title("Query Help");
    lnav_data.ld_doc_status_source.set_description(
        "See " ANSI_BOLD("https://docs.lnav.org/en/latest/"
                         "sqlext.html") " for more details");
    rl_set_help();
    lnav_data.ld_bottom_source.update_loading(0, 0);
    lnav_data.ld_status[LNS_BOTTOM].do_update();

    auto* fos = (field_overlay_source*) log_view.get_overlay_source();
    fos->fos_contexts.top().c_show = true;
    tc->set_sync_selection_and_top(true);
    tc->reload_data();
    lnav_data.ld_bottom_source.set_prompt(
        "Enter an SQL query: (Press " ANSI_BOLD("CTRL+]") " to abort)");

    add_sqlite_possibilities();
}

static void
user_prompt(std::vector<std::string>& args)
{
    textview_curses* tc = *lnav_data.ld_view_stack.top();
    lnav_data.ld_exec_context.ec_top_line = tc->get_selection();

    lnav_data.ld_mode = ln_mode_t::USER;
    setup_logline_table(lnav_data.ld_exec_context);
    lnav_data.ld_rl_view->focus(ln_mode_t::USER,
                                cget(args, 2).value_or("? "),
                                cget(args, 3).value_or(""));

    lnav_data.ld_bottom_source.update_loading(0, 0);
    lnav_data.ld_status[LNS_BOTTOM].do_update();
}

static Result<std::string, lnav::console::user_message>
com_prompt(exec_context& ec,
           std::string cmdline,
           std::vector<std::string>& args)
{
    static std::map<std::string, std::function<void(std::vector<std::string>&)>>
        PROMPT_TYPES = {
            {"breadcrumb", breadcrumb_prompt},
            {"command", command_prompt},
            {"script", script_prompt},
            {"search", search_prompt},
            {"search-filters", search_filters_prompt},
            {"search-files", search_files_prompt},
            {"search-spectro-details", search_spectro_details_prompt},
            {"sql", sql_prompt},
            {"user", user_prompt},
        };

    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        static const intern_string_t SRC = intern_string::lookup("flags");

        auto lexer = shlex(cmdline);
        auto split_args_res = lexer.split(ec.create_resolver());
        if (split_args_res.isErr()) {
            auto split_err = split_args_res.unwrapErr();
            auto um
                = lnav::console::user_message::error("unable to parse prompt")
                      .with_reason(split_err.te_msg)
                      .with_snippet(lnav::console::snippet::from(
                          SRC, lexer.to_attr_line(split_err)))
                      .move();

            return Err(um);
        }

        auto split_args = split_args_res.unwrap()
            | lnav::itertools::map(
                              [](const auto& elem) { return elem.se_value; });

        auto alt_flag
            = std::find(split_args.begin(), split_args.end(), "--alt");
        auto is_alt = false;
        if (alt_flag != split_args.end()) {
            split_args.erase(alt_flag);
            is_alt = true;
        }

        auto prompter = PROMPT_TYPES.find(split_args[1]);

        if (prompter == PROMPT_TYPES.end()) {
            return ec.make_error("Unknown prompt type: {}", split_args[1]);
        }

        prompter->second(split_args);
        lnav_data.ld_rl_view->set_alt_focus(is_alt);
    }
    return Ok(std::string());
}

readline_context::command_t STD_COMMANDS[] = {
    {"prompt",
     com_prompt,

     help_text(":prompt")
         .with_summary("Open the given prompt")
         .with_parameter({"type",
                          "The type of prompt -- command, script, "
                          "search, sql, user"})
         .with_parameter(help_text("--alt",
                                   "Perform the alternate action "
                                   "for this prompt by default")
                             .optional())
         .with_parameter(help_text("prompt", "The prompt to display")
                             .with_enum_values({
                                 "breadcrumb",
                                 "command",
                                 "script",
                                 "search",
                                 "sql",
                             })
                             .optional())
         .with_parameter(
             help_text("initial-value",
                       "The initial value to fill in for the prompt")
                 .optional())
         .with_example({
             "To open the command prompt with 'filter-in' already filled in",
             "command : 'filter-in '",
         })
         .with_example({
             "To ask the user a question",
             "user 'Are you sure? '",
         })},

    {"adjust-log-time",
     com_adjust_log_time,

     help_text(":adjust-log-time")
         .with_summary("Change the timestamps of the top file to be relative "
                       "to the given date")
         .with_parameter(
             help_text("timestamp",
                       "The new timestamp for the top line in the view")
                 .with_format(help_parameter_format_t::HPF_DATETIME))
         .with_example({"To set the top timestamp to a given date",
                        "2017-01-02T05:33:00"})
         .with_example({"To set the top timestamp back an hour", "-1h"})},

    {"unix-time",
     com_unix_time,

     help_text(":unix-time")
         .with_summary("Convert epoch time to a human-readable form")
         .with_parameter(help_text("seconds", "The epoch timestamp to convert")
                             .with_format(help_parameter_format_t::HPF_INTEGER))
         .with_example({"To convert the epoch time 1490191111", "1490191111"})},
    {
        "convert-time-to",
        com_convert_time_to,
        help_text(":convert-time-to")
            .with_summary("Convert the focused timestamp to the "
                          "given timezone")
            .with_parameter(help_text("zone", "The timezone name")),
    },
    {
        "set-file-timezone",
        com_set_file_timezone,
        help_text(":set-file-timezone")
            .with_summary("Set the timezone to use for log messages that do "
                          "not include a timezone.  The timezone is applied "
                          "to "
                          "the focused file or the given glob pattern.")
            .with_parameter({"zone", "The timezone name"})
            .with_parameter(help_text{"pattern",
                                      "The glob pattern to match against "
                                      "files that should use this timezone"}
                                .optional())
            .with_tags({"file-options"}),
        com_set_file_timezone_prompt,
    },
    {
        "clear-file-timezone",
        com_clear_file_timezone,
        help_text(":clear-file-timezone")
            .with_summary("Clear the timezone setting for the "
                          "focused file or "
                          "the given glob pattern.")
            .with_parameter(help_text{"pattern",
                                      "The glob pattern to match against files "
                                      "that should "
                                      "no longer use this timezone"})
            .with_tags({"file-options"}),
        com_clear_file_timezone_prompt,
    },
    {
        "set-text-view-mode",
        com_set_text_view_mode,
        help_text(":set-text-view-mode")
            .with_summary("Set the display mode for text files")
            .with_parameter(help_text{"mode"}
                                .with_summary("The display mode")
                                .with_enum_values({"raw", "rendered"})
                                .with_tags({"display"})),
    },
    {"current-time",
     com_current_time,

     help_text(":current-time")
         .with_summary("Print the current time in human-readable form and "
                       "seconds since the epoch")},
    {"goto",
     com_goto,

     help_text(":goto")
         .with_summary("Go to the given location in the top view")
         .with_parameter(
             help_text("line#|N%|timestamp|#anchor",
                       "A line number, percent into the file, timestamp, "
                       "or an anchor in a text file"))
         .with_examples(
             {{"To go to line 22", "22"},
              {"To go to the line 75% of the way into the view", "75%"},
              {"To go to the first message on the first day of "
               "2017",
               "2017-01-01"},
              {"To go to the Screenshots section", "#screenshots"}})
         .with_tags({"navigation"})},
    {"relative-goto",
     com_relative_goto,

     help_text(":relative-goto")
         .with_summary("Move the current view up or down by the given amount")
         .with_parameter({"line-count|N%", "The amount to move the view by."})
         .with_examples({
             {"To move 22 lines down in the view", "+22"},
             {"To move 10 percent back in the view", "-10%"},
         })
         .with_tags({"navigation"})},

    {
        "annotate",
        com_annotate,

        help_text(":annotate")
            .with_summary("Analyze the focused log message and "
                          "attach annotations")
            .with_tags({"metadata"}),
    },

    {"mark",
     com_mark,

     help_text(":mark")
         .with_summary("Toggle the bookmark state for the top line in the "
                       "current view")
         .with_tags({"bookmarks"})},
    {
        "mark-expr",
        com_mark_expr,

        help_text(":mark-expr")
            .with_summary("Set the bookmark expression")
            .with_parameter(help_text("expr",
                                      "The SQL expression to evaluate for each "
                                      "log message.  "
                                      "The message values can be accessed "
                                      "using column names "
                                      "prefixed with a colon"))
            .with_opposites({"clear-mark-expr"})
            .with_tags({"bookmarks"})
            .with_example({"To mark lines from 'dhclient' that "
                           "mention 'eth0'",
                           ":log_procname = 'dhclient' AND "
                           ":log_body LIKE '%eth0%'"}),

        com_mark_expr_prompt,
    },
    {"clear-mark-expr",
     com_clear_mark_expr,

     help_text(":clear-mark-expr")
         .with_summary("Clear the mark expression")
         .with_opposites({"mark-expr"})
         .with_tags({"bookmarks"})},
    {"next-mark",
     com_goto_mark,

     help_text(":next-mark")
         .with_summary("Move to the next bookmark of the given type in the "
                       "current view")
         .with_parameter(help_text("type",
                                   "The type of bookmark -- error, warning, "
                                   "search, user, file, meta")
                             .one_or_more())
         .with_example({"To go to the next error", "error"})
         .with_tags({"bookmarks", "navigation"})},
    {"prev-mark",
     com_goto_mark,

     help_text(":prev-mark")
         .with_summary("Move to the previous bookmark of the given "
                       "type in the "
                       "current view")
         .with_parameter(help_text("type",
                                   "The type of bookmark -- error, warning, "
                                   "search, user, file, meta")
                             .one_or_more())
         .with_example({"To go to the previous error", "error"})
         .with_tags({"bookmarks", "navigation"})},
    {"next-location",
     com_goto_location,

     help_text(":next-location")
         .with_summary("Move to the next position in the location history")
         .with_tags({"navigation"})},
    {"prev-location",
     com_goto_location,

     help_text(":prev-location")
         .with_summary("Move to the previous position in the "
                       "location history")
         .with_tags({"navigation"})},

    {
        "next-section",
        com_next_section,

        help_text(":next-section")
            .with_summary("Move to the next section in the document")
            .with_tags({"navigation"}),
    },
    {
        "prev-section",
        com_prev_section,

        help_text(":prev-section")
            .with_summary("Move to the previous section in the document")
            .with_tags({"navigation"}),
    },

    {"help",
     com_help,

     help_text(":help").with_summary("Open the help text view")},
    {"hide-fields",
     com_toggle_field,

     help_text(":hide-fields")
         .with_summary("Hide log message fields by replacing them "
                       "with an ellipsis")
         .with_parameter(
             help_text("field-name",
                       "The name of the field to hide in the format for "
                       "the "
                       "top log line.  "
                       "A qualified name can be used where the field "
                       "name is "
                       "prefixed "
                       "by the format name and a dot to hide any field.")
                 .one_or_more())
         .with_example(
             {"To hide the log_procname fields in all formats", "log_procname"})
         .with_example({"To hide only the log_procname field in "
                        "the syslog format",
                        "syslog_log.log_procname"})
         .with_tags({"display"})},
    {"show-fields",
     com_toggle_field,

     help_text(":show-fields")
         .with_summary("Show log message fields that were previously hidden")
         .with_parameter(
             help_text("field-name", "The name of the field to show")
                 .one_or_more())
         .with_example({"To show all the log_procname fields in all formats",
                        "log_procname"})
         .with_opposites({"hide-fields"})
         .with_tags({"display"})},
    {"hide-lines-before",
     com_hide_line,

     help_text(":hide-lines-before")
         .with_summary("Hide lines that come before the given date")
         .with_parameter(help_text("date", "An absolute or relative date"))
         .with_examples({
             {"To hide the lines before the top line in the view", "here"},
             {"To hide the log messages before 6 AM today", "6am"},
         })
         .with_tags({"filtering"})},
    {"hide-lines-after",
     com_hide_line,

     help_text(":hide-lines-after")
         .with_summary("Hide lines that come after the given date")
         .with_parameter(help_text("date", "An absolute or relative date"))
         .with_examples({
             {"To hide the lines after the top line in the view", "here"},
             {"To hide the lines after 6 AM today", "6am"},
         })
         .with_tags({"filtering"})},
    {"show-lines-before-and-after",
     com_show_lines,

     help_text(":show-lines-before-and-after")
         .with_summary("Show lines that were hidden by the "
                       "'hide-lines' commands")
         .with_opposites({"hide-lines-before", "hide-lines-after"})
         .with_tags({"filtering"})},
    {"hide-unmarked-lines",
     com_hide_unmarked,

     help_text(":hide-unmarked-lines")
         .with_summary("Hide lines that have not been bookmarked")
         .with_tags({"filtering", "bookmarks"})},
    {"show-unmarked-lines",
     com_show_unmarked,

     help_text(":show-unmarked-lines")
         .with_summary("Show lines that have not been bookmarked")
         .with_opposites({"show-unmarked-lines"})
         .with_tags({"filtering", "bookmarks"})},
    {"highlight",
     com_highlight,

     help_text(":highlight")
         .with_summary("Add coloring to log messages fragments "
                       "that match the "
                       "given regular expression")
         .with_parameter(
             help_text("pattern", "The regular expression to match"))
         .with_tags({"display"})
         .with_example(
             {"To highlight numbers with three or more digits", R"(\d{3,})"})},
    {"clear-highlight",
     com_clear_highlight,

     help_text(":clear-highlight")
         .with_summary("Remove a previously set highlight regular expression")
         .with_parameter(help_text("pattern",
                                   "The regular expression previously used "
                                   "with :highlight"))
         .with_tags({"display"})
         .with_opposites({"highlight"})
         .with_example(
             {"To clear the highlight with the pattern 'foobar'", "foobar"})},
    {
        "filter-in",
        com_filter,

        help_text(":filter-in")
            .with_summary("Only show lines that match the given regular "
                          "expression in the current view")
            .with_parameter(
                help_text("pattern", "The regular expression to match"))
            .with_tags({"filtering"})
            .with_example({"To filter out log messages that do not have the "
                           "string 'dhclient'",
                           "dhclient"}),
        com_filter_prompt,
    },
    {
        "filter-out",
        com_filter,

        help_text(":filter-out")
            .with_summary("Remove lines that match the given "
                          "regular expression "
                          "in the current view")
            .with_parameter(
                help_text("pattern", "The regular expression to match"))
            .with_tags({"filtering"})
            .with_example({"To filter out log messages that "
                           "contain the string "
                           "'last message repeated'",
                           "last message repeated"}),
        com_filter_prompt,
    },
    {"delete-filter",
     com_delete_filter,

     help_text(":delete-filter")
         .with_summary("Delete the filter created with " ANSI_BOLD(
             ":filter-in") " or " ANSI_BOLD(":filter-out"))
         .with_parameter(
             help_text("pattern", "The regular expression to match"))
         .with_opposites({"filter-in", "filter-out"})
         .with_tags({"filtering"})
         .with_example({"To delete the filter with the pattern 'last "
                        "message repeated'",
                        "last message repeated"})},
    {
        "filter-expr",
        com_filter_expr,

        help_text(":filter-expr")
            .with_summary("Set the filter expression")
            .with_parameter(help_text("expr",
                                      "The SQL expression to evaluate for each "
                                      "log message.  "
                                      "The message values can be accessed "
                                      "using column names "
                                      "prefixed with a colon"))
            .with_opposites({"clear-filter-expr"})
            .with_tags({"filtering"})
            .with_example({"To set a filter expression that matched syslog "
                           "messages from 'syslogd'",
                           ":log_procname = 'syslogd'"})
            .with_example({"To set a filter expression that "
                           "matches log messages "
                           "where "
                           "'id' is followed by a number and "
                           "contains the string "
                           "'foo'",
                           ":log_body REGEXP 'id\\d+' AND "
                           ":log_body REGEXP 'foo'"}),

        com_filter_expr_prompt,
    },
    {"clear-filter-expr",
     com_clear_filter_expr,

     help_text(":clear-filter-expr")
         .with_summary("Clear the filter expression")
         .with_opposites({"filter-expr"})
         .with_tags({"filtering"})},
    {"append-to",
     com_save_to,

     help_text(":append-to")
         .with_summary("Append marked lines in the current view to "
                       "the given file")
         .with_parameter(help_text("path", "The path to the file to append to"))
         .with_tags({"io"})
         .with_example({"To append marked lines to the file "
                        "/tmp/interesting-lines.txt",
                        "/tmp/interesting-lines.txt"})},
    {"write-to",
     com_save_to,

     help_text(":write-to")
         .with_summary("Overwrite the given file with any marked "
                       "lines in the "
                       "current view")
         .with_parameter(
             help_text("--anonymize", "Anonymize the lines").optional())
         .with_parameter(help_text("path", "The path to the file to write"))
         .with_tags({"io", "scripting"})
         .with_example({"To write marked lines to the file "
                        "/tmp/interesting-lines.txt",
                        "/tmp/interesting-lines.txt"})},
    {"write-csv-to",
     com_save_to,

     help_text(":write-csv-to")
         .with_summary("Write SQL results to the given file in CSV format")
         .with_parameter(
             help_text("--anonymize", "Anonymize the row contents").optional())
         .with_parameter(help_text("path", "The path to the file to write"))
         .with_tags({"io", "scripting", "sql"})
         .with_example({"To write SQL results as CSV to /tmp/table.csv",
                        "/tmp/table.csv"})},
    {"write-json-to",
     com_save_to,

     help_text(":write-json-to")
         .with_summary("Write SQL results to the given file in JSON format")
         .with_parameter(
             help_text("--anonymize", "Anonymize the JSON values").optional())
         .with_parameter(help_text("path", "The path to the file to write"))
         .with_tags({"io", "scripting", "sql"})
         .with_example({"To write SQL results as JSON to /tmp/table.json",
                        "/tmp/table.json"})},
    {"write-jsonlines-to",
     com_save_to,

     help_text(":write-jsonlines-to")
         .with_summary("Write SQL results to the given file in "
                       "JSON Lines format")
         .with_parameter(
             help_text("--anonymize", "Anonymize the JSON values").optional())
         .with_parameter(help_text("path", "The path to the file to write"))
         .with_tags({"io", "scripting", "sql"})
         .with_example({"To write SQL results as JSON Lines to "
                        "/tmp/table.json",
                        "/tmp/table.json"})},
    {"write-table-to",
     com_save_to,

     help_text(":write-table-to")
         .with_summary("Write SQL results to the given file in a "
                       "tabular format")
         .with_parameter(
             help_text("--anonymize", "Anonymize the table contents")
                 .optional())
         .with_parameter(help_text("path", "The path to the file to write"))
         .with_tags({"io", "scripting", "sql"})
         .with_example({"To write SQL results as text to /tmp/table.txt",
                        "/tmp/table.txt"})},
    {"write-raw-to",
     com_save_to,

     help_text(":write-raw-to")
         .with_summary("In the log view, write the original log file content "
                       "of the marked messages to the file.  In the DB view, "
                       "the contents of the cells are written to the output "
                       "file.")
         .with_parameter(help_text("--view={log,db}",
                                   "The view to use as the source of data")
                             .optional())
         .with_parameter(
             help_text("--anonymize", "Anonymize the lines").optional())
         .with_parameter(help_text("path", "The path to the file to write"))
         .with_tags({"io", "scripting", "sql"})
         .with_example({"To write the marked lines in the log view "
                        "to /tmp/table.txt",
                        "/tmp/table.txt"})},
    {"write-view-to",
     com_save_to,

     help_text(":write-view-to")
         .with_summary("Write the text in the top view to the given file "
                       "without any formatting")
         .with_parameter(
             help_text("--anonymize", "Anonymize the lines").optional())
         .with_parameter(help_text("path", "The path to the file to write"))
         .with_tags({"io", "scripting", "sql"})
         .with_example(
             {"To write the top view to /tmp/table.txt", "/tmp/table.txt"})},
    {"write-screen-to",
     com_save_to,

     help_text(":write-screen-to")
         .with_summary("Write the displayed text or SQL results to the given "
                       "file without any formatting")
         .with_parameter(
             help_text("--anonymize", "Anonymize the lines").optional())
         .with_parameter(help_text("path", "The path to the file to write"))
         .with_tags({"io", "scripting", "sql"})
         .with_example({"To write only the displayed text to /tmp/table.txt",
                        "/tmp/table.txt"})},
    {"pipe-to",
     com_pipe_to,

     help_text(":pipe-to")
         .with_summary("Pipe the marked lines to the given shell command")
         .with_parameter(
             help_text("shell-cmd", "The shell command-line to execute"))
         .with_tags({"io"})
         .with_example({"To write marked lines to 'sed' for processing",
                        "sed -e s/foo/bar/g"})},
    {"pipe-line-to",
     com_pipe_to,

     help_text(":pipe-line-to")
         .with_summary("Pipe the focused line to the given shell "
                       "command.  Any fields "
                       "defined by the format will be set as "
                       "environment variables.")
         .with_parameter(
             help_text("shell-cmd", "The shell command-line to execute"))
         .with_tags({"io"})
         .with_example({"To write the top line to 'sed' for processing",
                        "sed -e 's/foo/bar/g'"})},
    {"redirect-to",
     com_redirect_to,

     help_text(":redirect-to")
         .with_summary("Redirect the output of commands that write to "
                       "stdout to the given file")
         .with_parameter(help_text("path",
                                   "The path to the file to write."
                                   "  If not specified, the current redirect "
                                   "will be cleared")
                             .optional())
         .with_tags({"io", "scripting"})
         .with_example({"To write the output of lnav commands to the file "
                        "/tmp/script-output.txt",
                        "/tmp/script-output.txt"})},
    {"enable-filter",
     com_enable_filter,

     help_text(":enable-filter")
         .with_summary("Enable a previously created and disabled filter")
         .with_parameter(help_text(
             "pattern", "The regular expression used in the filter command"))
         .with_tags({"filtering"})
         .with_opposites({"disable-filter"})
         .with_example({"To enable the disabled filter with the "
                        "pattern 'last "
                        "message repeated'",
                        "last message repeated"})},
    {"disable-filter",
     com_disable_filter,

     help_text(":disable-filter")
         .with_summary("Disable a filter created with filter-in/filter-out")
         .with_parameter(help_text(
             "pattern", "The regular expression used in the filter command"))
         .with_tags({"filtering"})
         .with_opposites({"filter-out", "filter-in"})
         .with_example({"To disable the filter with the pattern 'last "
                        "message repeated'",
                        "last message repeated"})},
    {"enable-word-wrap",
     com_enable_word_wrap,

     help_text(":enable-word-wrap")
         .with_summary("Enable word-wrapping for the current view")
         .with_tags({"display"})},
    {"disable-word-wrap",
     com_disable_word_wrap,

     help_text(":disable-word-wrap")
         .with_summary("Disable word-wrapping for the current view")
         .with_opposites({"enable-word-wrap"})
         .with_tags({"display"})},
    {"create-logline-table",
     com_create_logline_table,

     help_text(":create-logline-table")
         .with_summary("Create an SQL table using the top line of "
                       "the log view "
                       "as a template")
         .with_parameter(help_text("table-name", "The name for the new table"))
         .with_tags({"vtables", "sql"})
         .with_example({"To create a logline-style table named "
                        "'task_durations'",
                        "task_durations"})},
    {"delete-logline-table",
     com_delete_logline_table,

     help_text(":delete-logline-table")
         .with_summary("Delete a table created with create-logline-table")
         .with_parameter(
             help_text("table-name", "The name of the table to delete"))
         .with_opposites({"delete-logline-table"})
         .with_tags({"vtables", "sql"})
         .with_example({"To delete the logline-style table named "
                        "'task_durations'",
                        "task_durations"})},
    {"create-search-table",
     com_create_search_table,

     help_text(":create-search-table")
         .with_summary("Create an SQL table based on a regex search")
         .with_parameter(
             help_text("table-name", "The name of the table to create"))
         .with_parameter(
             help_text("pattern",
                       "The regular expression used to capture the table "
                       "columns.  "
                       "If not given, the current search pattern is "
                       "used.")
                 .optional())
         .with_tags({"vtables", "sql"})
         .with_example({"To create a table named 'task_durations' that "
                        "matches log "
                        "messages with the pattern "
                        "'duration=(?<duration>\\d+)'",
                        R"(task_durations duration=(?<duration>\d+))"})},
    {"delete-search-table",
     com_delete_search_table,

     help_text(":delete-search-table")
         .with_summary("Create an SQL table based on a regex search")
         .with_parameter(
             help_text("table-name", "The name of the table to create"))
         .with_opposites({"create-search-table"})
         .with_tags({"vtables", "sql"})
         .with_example({"To delete the search table named 'task_durations'",
                        "task_durations"})},
    {"open",
     com_open,

     help_text(":open")
         .with_summary("Open the given file(s) in lnav.  Opening files on "
                       "machines "
                       "accessible via SSH can be done using the syntax: "
                       "[user@]host:/path/to/logs")
         .with_parameter(
             help_text{"path", "The path to the file to open"}.one_or_more())
         .with_example({"To open the file '/path/to/file'", "/path/to/file"})
         .with_example({"To open the remote file '/var/log/syslog.log'",
                        "dean@host1.example.com:/var/log/syslog.log"})},
    {"xopen",
     com_xopen,

     help_text(":xopen")
         .with_summary("Use an external command to open the given file(s)")
         .with_parameter(
             help_text{"path", "The path to the file to open"}.one_or_more())
         .with_example({"To open the file '/path/to/file'", "/path/to/file"})},
    {"hide-file",
     com_hide_file,

     help_text(":hide-file")
         .with_summary("Hide the given file(s) and skip indexing until it "
                       "is shown again.  If no path is given, the current "
                       "file in the view is hidden")
         .with_parameter(help_text{"path",
                                   "A path or glob pattern that "
                                   "specifies the files to hide"}
                             .zero_or_more())
         .with_opposites({"show-file"})},
    {"show-file",
     com_show_file,

     help_text(":show-file")
         .with_summary("Show the given file(s) and resume indexing.")
         .with_parameter(help_text{"path",
                                   "The path or glob pattern that "
                                   "specifies the files to show"}
                             .zero_or_more())
         .with_opposites({"hide-file"})},
    {"show-only-this-file",
     com_show_only_this_file,

     help_text(":show-only-this-file")
         .with_summary("Show only the file for the top line in the view")
         .with_opposites({"hide-file"})},
    {"close",
     com_close,

     help_text(":close")
         .with_summary("Close the given file(s) or the top file in the view")
         .with_parameter(help_text{"path",
                                   "A path or glob pattern that "
                                   "specifies the files to close"}
                             .zero_or_more())
         .with_opposites({"open"})},
    {
        "comment",
        com_comment,

        help_text(":comment")
            .with_summary("Attach a comment to the top log line.  The "
                          "comment will be "
                          "displayed right below the log message it is "
                          "associated with. "
                          "The comment can be formatted using markdown and "
                          "you can add "
                          "new-lines with '\\n'.")
            .with_parameter(help_text("text", "The comment text"))
            .with_example({"To add the comment 'This is where it all went "
                           "wrong' to the top line",
                           "This is where it all went wrong"})
            .with_tags({"metadata"}),

        com_comment_prompt,
    },
    {"clear-comment",
     com_clear_comment,

     help_text(":clear-comment")
         .with_summary("Clear the comment attached to the top log line")
         .with_opposites({"comment"})
         .with_tags({"metadata"})},
    {
        "tag",
        com_tag,

        help_text(":tag")
            .with_summary("Attach tags to the top log line")
            .with_parameter(
                help_text("tag", "The tags to attach").one_or_more())
            .with_example({"To add the tags '#BUG123' and '#needs-review' to "
                           "the top line",
                           "#BUG123 #needs-review"})
            .with_tags({"metadata"}),
    },
    {"untag",
     com_untag,

     help_text(":untag")
         .with_summary("Detach tags from the top log line")
         .with_parameter(help_text("tag", "The tags to detach").one_or_more())
         .with_example({"To remove the tags '#BUG123' and "
                        "'#needs-review' from "
                        "the top line",
                        "#BUG123 #needs-review"})
         .with_opposites({"tag"})
         .with_tags({"metadata"})},
    {"delete-tags",
     com_delete_tags,

     help_text(":delete-tags")
         .with_summary("Remove the given tags from all log lines")
         .with_parameter(help_text("tag", "The tags to delete").one_or_more())
         .with_example({"To remove the tags '#BUG123' and "
                        "'#needs-review' from "
                        "all log lines",
                        "#BUG123 #needs-review"})
         .with_opposites({"tag"})
         .with_tags({"metadata"})},
    {"partition-name",
     com_partition_name,

     help_text(":partition-name")
         .with_summary("Mark the top line in the log view as the start of a "
                       "new partition with the given name")
         .with_parameter(help_text("name", "The name for the new partition"))
         .with_example({"To mark the top line as the start of the partition "
                        "named 'boot #1'",
                        "boot #1"})},
    {"clear-partition",
     com_clear_partition,

     help_text(":clear-partition")
         .with_summary("Clear the partition the top line is a part of")
         .with_opposites({"partition-name"})},
    {"session",
     com_session,

     help_text(":session")
         .with_summary("Add the given command to the session file "
                       "(~/.lnav/session)")
         .with_parameter(help_text("lnav-command", "The lnav command to save."))
         .with_example({"To add the command ':highlight foobar' to "
                        "the session file",
                        ":highlight foobar"})},
    {"summarize",
     com_summarize,

     help_text(":summarize")
         .with_summary("Execute a SQL query that computes the "
                       "characteristics "
                       "of the values in the given column")
         .with_parameter(
             help_text("column-name", "The name of the column to analyze."))
         .with_example({"To get a summary of the sc_bytes column in the "
                        "access_log table",
                        "sc_bytes"})},
    {"switch-to-view",
     com_switch_to_view,

     help_text(":switch-to-view")
         .with_summary("Switch to the given view")
         .with_parameter(
             help_text("view-name", "The name of the view to switch to."))
         .with_example({"To switch to the 'schema' view", "schema"})},
    {"toggle-view",
     com_switch_to_view,

     help_text(":toggle-view")
         .with_summary("Switch to the given view or, if it is "
                       "already displayed, "
                       "switch to the previous view")
         .with_parameter(help_text(
             "view-name", "The name of the view to toggle the display of."))
         .with_example({"To switch to the 'schema' view if it is "
                        "not displayed "
                        "or switch back to the previous view",
                        "schema"})},
    {"toggle-filtering",
     com_toggle_filtering,

     help_text(":toggle-filtering")
         .with_summary("Toggle the filtering flag for the current view")
         .with_tags({"filtering"})},
    {"reset-session",
     com_reset_session,

     help_text(":reset-session")
         .with_summary("Reset the session state, clearing all filters, "
                       "highlights, and bookmarks")},
    {"load-session",
     com_load_session,

     help_text(":load-session").with_summary("Load the latest session state")},
    {"save-session",
     com_save_session,

     help_text(":save-session")
         .with_summary("Save the current state as a session")},
    {"export-session-to",
     com_export_session_to,

     help_text(":export-session-to")
         .with_summary("Export the current lnav state to an executable lnav "
                       "script file that contains the commands needed to "
                       "restore the current session")
         .with_parameter(help_text("path", "The path to the file to write"))
         .with_tags({"io", "scripting"})},
    {"rebuild",
     com_rebuild,
     help_text(":rebuild")
         .with_summary("Forcefully rebuild file indexes")
         .with_tags({"scripting"})},
    {"set-min-log-level",
     com_set_min_log_level,

     help_text(":set-min-log-level")
         .with_summary("Set the minimum log level to display in the log view")
         .with_parameter(help_text("log-level", "The new minimum log level"))
         .with_example(
             {"To set the minimum log level displayed to error", "error"})},
    {"redraw",
     com_redraw,

     help_text(":redraw").with_summary("Do a full redraw of the screen")},
    {"zoom-to",
     com_zoom_to,

     help_text(":zoom-to")
         .with_summary("Zoom the histogram view to the given level")
         .with_parameter(help_text("zoom-level", "The zoom level"))
         .with_example({"To set the zoom level to '1-week'", "1-week"})},
    {"echo",
     com_echo,

     help_text(":echo")
         .with_summary("Echo the given message to the screen or, if "
                       ":redirect-to has "
                       "been called, to output file specified in the "
                       "redirect.  "
                       "Variable substitution is performed on the message.  "
                       "Use a "
                       "backslash to escape any special characters, like '$'")
         .with_parameter(help_text("-n",
                                   "Do not print a line-feed at "
                                   "the end of the output")
                             .optional())
         .with_parameter(help_text("msg", "The message to display"))
         .with_tags({"io", "scripting"})
         .with_example({"To output 'Hello, World!'", "Hello, World!"})},
    {"alt-msg",
     com_alt_msg,

     help_text(":alt-msg")
         .with_summary("Display a message in the alternate command position")
         .with_parameter(help_text("msg", "The message to display"))
         .with_tags({"scripting"})
         .with_example({"To display 'Press t to switch to the text view' on "
                        "the bottom right",
                        "Press t to switch to the text view"})},
    {"eval",
     com_eval,

     help_text(":eval")
         .with_summary("Evaluate the given command/query after doing "
                       "environment variable substitution")
         .with_parameter(help_text(
             "command", "The command or query to perform substitution on."))
         .with_tags({"scripting"})
         .with_examples({{"To substitute the table name from a variable",
                          ";SELECT * FROM ${table}"}})},

    {
        "sh",
        com_sh,

        help_text(":sh")
            .with_summary("Execute the given command-line and display the "
                          "captured output")
            .with_parameter(help_text(
                "--name=<name>", "The name to give to the captured output"))
            .with_parameter(
                help_text("cmdline", "The command-line to execute."))
            .with_tags({"scripting"}),
    },

    {
        "cd",
        com_cd,

        help_text(":cd")
            .with_summary("Change the current directory")
            .with_parameter(help_text("dir", "The new current directory"))
            .with_tags({"scripting"}),
    },

    {"config",
     com_config,

     help_text(":config")
         .with_summary("Read or write a configuration option")
         .with_parameter({"option", "The path to the option to read or write"})
         .with_parameter(help_text("value",
                                   "The value to write.  If not given, the "
                                   "current value is returned")
                             .optional())
         .with_example({"To read the configuration of the "
                        "'/ui/clock-format' option",
                        "/ui/clock-format"})
         .with_example({"To set the '/ui/dim-text' option to 'false'",
                        "/ui/dim-text false"})
         .with_tags({"configuration"})},
    {"reset-config",
     com_reset_config,

     help_text(":reset-config")
         .with_summary("Reset the configuration option to its default value")
         .with_parameter(help_text("option", "The path to the option to reset"))
         .with_example({"To reset the '/ui/clock-format' option back to the "
                        "builtin default",
                        "/ui/clock-format"})
         .with_tags({"configuration"})},
    {"spectrogram",
     com_spectrogram,

     help_text(":spectrogram")
         .with_summary("Visualize the given message field or database column "
                       "using a spectrogram")
         .with_parameter(help_text(
             "field-name", "The name of the numeric field to visualize."))
         .with_example({"To visualize the sc_bytes field in the "
                        "access_log format",
                        "sc_bytes"})},
    {"quit",
     com_quit,

     help_text(":quit").with_summary("Quit lnav")}};

static std::unordered_map<char const*, std::vector<char const*>> aliases = {
    {"quit", {"q", "q!"}},
    {"write-table-to", {"write-cols-to"}},
};

static Result<std::string, lnav::console::user_message>
com_crash(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    if (args.empty()) {
    } else if (!ec.ec_dry_run) {
        int* nums = nullptr;

        return ec.make_error(FMT_STRING("oops... {}"), nums[0]);
    }
    return Ok(std::string());
}

void
init_lnav_commands(readline_context::command_map_t& cmd_map)
{
    for (auto& cmd : STD_COMMANDS) {
        cmd.c_help.index_tags();
        cmd_map[cmd.c_name] = &cmd;

        auto itr = aliases.find(cmd.c_name);
        if (itr != aliases.end()) {
            for (char const* alias : itr->second) {
                cmd_map[alias] = &cmd;
            }
        }
    }

    if (getenv("LNAV_SRC") != nullptr) {
        static readline_context::command_t add_test(com_add_test);

        cmd_map["add-test"] = &add_test;
    }
    if (getenv("lnav_test") != nullptr) {
        static readline_context::command_t shexec(com_shexec),
            poll_now(com_poll_now), test_comment(com_test_comment),
            crasher(com_crash);

        cmd_map["shexec"] = &shexec;
        cmd_map["poll-now"] = &poll_now;
        cmd_map["test-comment"] = &test_comment;
        cmd_map["crash"] = &crasher;
    }
}
