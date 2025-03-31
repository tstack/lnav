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
#include "cmd.parser.hh"
#include "command_executor.hh"
#include "config.h"
#include "curl_looper.hh"
#include "date/tz.h"
#include "db_sub_source.hh"
#include "field_overlay_source.hh"
#include "hasher.hh"
#include "itertools.similar.hh"
#include "lnav.indexing.hh"
#include "lnav.prompt.hh"
#include "lnav_commands.hh"
#include "lnav_config.hh"
#include "lnav_util.hh"
#include "log.annotate.hh"
#include "log_data_helper.hh"
#include "log_data_table.hh"
#include "log_format_loader.hh"
#include "log_search_table.hh"
#include "log_search_table_fwd.hh"
#include "md2attr_line.hh"
#include "md4cpp.hh"
#include "ptimec.hh"
#include "readline_callbacks.hh"
#include "readline_highlighters.hh"
#include "relative_time.hh"
#include "scn/scan.h"
#include "service_tags.hh"
#include "session.export.hh"
#include "session_data.hh"
#include "shlex.hh"
#include "spectro_impls.hh"
#include "sql_util.hh"
#include "sqlite-extension-func.hh"
#include "sysclip.hh"
#include "url_handler.cfg.hh"
#include "url_loader.hh"
#include "vtab_module.hh"
#include "yajl/api/yajl_parse.h"
#include "yajlpp/json_op.hh"
#include "yajlpp/yajlpp.hh"

using namespace lnav::roles::literals;

inline attr_line_t&
symbol_reducer(const std::string& elem, attr_line_t& accum)
{
    return accum.append("\n   ").append(lnav::roles::symbol(elem));
}

std::string
remaining_args(const std::string& cmdline,
               const std::vector<std::string>& args,
               size_t index)
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

    auto retval = cmdline.substr(index_in_cmdline);
    while (!retval.empty() && retval.back() == ' ') {
        retval.pop_back();
    }

    return retval;
}

string_fragment
remaining_args_frag(const std::string& cmdline,
                    const std::vector<std::string>& args,
                    size_t index)
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

std::optional<std::string>
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

bookmark_vector<vis_line_t>
combined_user_marks(vis_bookmarks& vb)
{
    const auto& bv = vb[&textview_curses::BM_USER];
    const auto& bv_expr = vb[&textview_curses::BM_USER_EXPR];
    bookmark_vector<vis_line_t> retval;

    for (const auto& row : bv.bv_tree) {
        retval.insert_once(row);
    }
    for (const auto& row : bv_expr.bv_tree) {
        retval.insert_once(row);
    }
    return retval;
}

static Result<std::string, lnav::console::user_message>
com_write_debug_log_to(exec_context& ec,
                       std::string cmdline,
                       std::vector<std::string>& args)
{
    if (args.size() < 2) {
        return ec.make_error("expecting a file path");
    }

    if (lnav_log_file.has_value()) {
        return ec.make_error("debug log is already being written to a file");
    }

    std::string retval;
    if (ec.ec_dry_run) {
        return Ok(retval);
    }

    auto fp = fopen(args[1].c_str(), "we");
    if (fp == nullptr) {
        auto um = lnav::console::user_message::error(
                      attr_line_t("unable to open file for write: ")
                          .append(lnav::roles::file(args[1])))
                      .with_errno_reason();
        return Err(um);
    }
    auto fd = fileno(fp);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    log_write_ring_to(fd);
    lnav_log_level = lnav_log_level_t::TRACE;
    lnav_log_file = fp;

    retval = fmt::format(FMT_STRING("info: wrote debug log to -- {}"), args[1]);

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_adjust_log_time(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
    std::string retval;

    if (lnav_data.ld_views[LNV_LOG].get_inner_height() == 0) {
        return ec.make_error("no log messages");
    }
    if (args.size() >= 2) {
        auto& lss = lnav_data.ld_log_source;
        struct timeval top_time, time_diff;
        struct timeval new_time = {0, 0};
        content_line_t top_content;
        date_time_scanner dts;
        vis_line_t top_line;
        struct exttm tm;
        struct tm base_tm;

        top_line = lnav_data.ld_views[LNV_LOG].get_selection();
        top_content = lss.at(top_line);
        auto lf = lss.find(top_content);

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
com_clear_adjusted_log_time(exec_context& ec,
                            std::string cmdline,
                            std::vector<std::string>& args)
{
    if (lnav_data.ld_views[LNV_LOG].get_inner_height() == 0) {
        return ec.make_error("no log messages");
    }

    auto& lss = lnav_data.ld_log_source;
    auto sel_line = lnav_data.ld_views[LNV_LOG].get_selection();
    auto sel_pair = lss.find_line_with_file(sel_line);
    if (sel_pair) {
        auto lf = sel_pair->first;
        lf->clear_time_offset();
        lss.set_force_rebuild();
    }

    return Ok(std::string("info: cleared time offset"));
}

static Result<std::string, lnav::console::user_message>
com_unix_time(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() >= 2) {
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
                          .with_reason(split_err.se_error.te_msg)
                          .with_snippet(lnav::console::snippet::from(
                              SRC, lexer.to_attr_line(split_err.se_error)))
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
                pattern_arg = lnav::filesystem::escape_path(
                    match_res->first, lnav::filesystem::path_type::pattern);

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
com_convert_time_to(exec_context& ec,
                    std::string cmdline,
                    std::vector<std::string>& args)
{
    std::string retval;

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
                ll->get_time<std::chrono::seconds>()};
            auto cz_time = lnav::to_sys_time(utime);
            auto dz_time = date::make_zoned(dst_tz, cz_time);
            auto etime = std::chrono::duration_cast<std::chrono::seconds>(
                dz_time.get_local_time().time_since_epoch());
            char ftime[128];
            sql_strftime(
                ftime,
                sizeof(ftime),
                etime.count(),
                ll->get_subsecond_time<std::chrono::milliseconds>().count(),
                'T');
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

#if 0
            if (!ec.ec_dry_run && !rt.is_absolute()
                && lnav_data.ld_rl_view != nullptr)
            {
                lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                    r, R, "to move forward/backward the same amount of time"));
            }
#endif
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
                tm.et_tm.tm_yday = -1;
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
com_mark_expr(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() < 2) {
        return ec.make_error("expecting an SQL expression");
    }
    if (*lnav_data.ld_view_stack.top() != &lnav_data.ld_views[LNV_LOG]) {
        return ec.make_error(":mark-expr is only supported for the LOG view");
    }
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
        auto expr_al
            = attr_line_t(expr)
                  .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE))
                  .move();
        readline_sqlite_highlighter(expr_al, std::nullopt);
        auto um = lnav::console::user_message::error(
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
            lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();
    } else {
        auto set_res = lss.set_sql_marker(expr, stmt.release());

        if (set_res.isErr()) {
            return Err(set_res.unwrapErr());
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
                | [tc](auto new_top) { tc->set_selection(new_top); };
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
        if (tc->is_selectable() && adj_opt.value() >= 2_vl) {
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
        if (tc->is_selectable() && adj_opt.value() >= 2_vl) {
            tc->set_top(adj_opt.value() - 2_vl, false);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_highlight(exec_context& ec,
              std::string cmdline,
              std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
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
            hl_attrs |= text_attrs::style::blink;
        }

        hl.with_attrs(hl_attrs);

        if (ec.ec_dry_run) {
            hm[{highlight_source_t::PREVIEW, "preview"}] = hl;

            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "Matches are highlighted in the view");
            lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();

            retval = "";
        } else {
            hm[{highlight_source_t::INTERACTIVE, args[1]}] = hl;

#if 0
            if (lnav_data.ld_rl_view != nullptr) {
                lnav_data.ld_rl_view->add_possibility(
                    ln_mode_t::COMMAND, "highlight", args[1]);
            }
#endif

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

    if (args.size() > 1 && args[1][0] != '$') {
        textview_curses* tc = *lnav_data.ld_view_stack.top();
        auto& hm = tc->get_highlights();

        args[1] = remaining_args(cmdline, args);
        auto hm_iter = hm.find({highlight_source_t::INTERACTIVE, args[1]});
        if (hm_iter == hm.end()) {
            return ec.make_error("highlight does not exist -- {}", args[1]);
        }
        if (ec.ec_dry_run) {
            retval = "";
        } else {
            hm.erase(hm_iter);
            retval = "info: highlight pattern cleared";
            tc->reload_data();

#if 0
            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->rem_possibility(
                    ln_mode_t::COMMAND, "highlight", args[1]);
            }
#endif
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

    if (!ec.ec_dry_run) {
        ensure_view(&lnav_data.ld_views[LNV_HELP]);
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_filter_expr(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
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
            auto expr_al
                = attr_line_t(expr)
                      .with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE))
                      .move();
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
            lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();
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

    if (!ec.ec_dry_run) {
        lnav_data.ld_log_source.set_sql_filter("", nullptr);
        lnav_data.ld_log_source.text_filters_changed();
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_enable_word_wrap(exec_context& ec,
                     std::string cmdline,
                     std::vector<std::string>& args)
{
    std::string retval;

    if (!ec.ec_dry_run) {
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

    if (!ec.ec_dry_run) {
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

    if (args.size() == 2) {
        auto& log_view = lnav_data.ld_views[LNV_LOG];

        if (log_view.get_inner_height() == 0) {
            return ec.make_error("no log data available");
        }
        auto vl = log_view.get_selection();
        auto cl = lnav_data.ld_log_source.at_base(vl);
        auto ldt
            = std::make_shared<log_data_table>(lnav_data.ld_log_source,
                                               *lnav_data.ld_vtab_manager,
                                               cl,
                                               intern_string::lookup(args[1]));
        ldt->vi_provenance = log_vtab_impl::provenance_t::user;
        if (ec.ec_dry_run) {
            attr_line_t al(ldt->get_table_statement());

            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "The following table will be created:");
            lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();
            lnav_data.ld_preview_view[0].set_sub_source(
                &lnav_data.ld_preview_source[0]);
            lnav_data.ld_preview_source[0].replace_with(al).set_text_format(
                text_format_t::TF_SQL);

            return Ok(std::string());
        }

        auto errmsg = lnav_data.ld_vtab_manager->register_vtab(ldt);
        if (errmsg.empty()) {
            custom_logline_tables.insert(args[1]);
#if 0
                    if (lnav_data.ld_rl_view != NULL) {
                        lnav_data.ld_rl_view->add_possibility(
                            ln_mode_t::COMMAND, "custom-table", args[1]);
                    }
#endif
            retval = "info: created new log table -- " + args[1];
        } else {
            return ec.make_error("unable to create table -- {}", errmsg);
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

    if (args.size() == 2) {
        if (custom_logline_tables.find(args[1]) == custom_logline_tables.end())
        {
            return ec.make_error("unknown logline table -- {}", args[1]);
        }

        if (ec.ec_dry_run) {
            return Ok(std::string());
        }

        std::string rc = lnav_data.ld_vtab_manager->unregister_vtab(args[1]);

        if (rc.empty()) {
#if 0
            if (lnav_data.ld_rl_view != NULL) {
                lnav_data.ld_rl_view->rem_possibility(
                    ln_mode_t::COMMAND, "custom-table", args[1]);
            }
#endif
            retval = "info: deleted logline table";
        } else {
            return ec.make_error("{}", rc);
        }
    } else {
        return ec.make_error("expecting a table name");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_create_search_table(exec_context& ec,
                        std::string cmdline,
                        std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() >= 2) {
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
            hl.with_attrs(text_attrs::with_blink());

            hm[{highlight_source_t::PREVIEW, "preview"}] = hl;
            tc->reload_data();

            attr_line_t al(lst->get_table_statement());

            lnav_data.ld_preview_status_source[0].get_description().set_value(
                "The following table will be created:");
            lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();

            lnav_data.ld_preview_view[0].set_sub_source(
                &lnav_data.ld_preview_source[0]);
            lnav_data.ld_preview_source[0].replace_with(al).set_text_format(
                text_format_t::TF_SQL);

            return Ok(std::string());
        }

        lst->vi_provenance = log_vtab_impl::provenance_t::user;
        auto existing = lnav_data.ld_vtab_manager->lookup_impl(tab_name);
        if (existing != nullptr) {
            if (existing->vi_provenance != log_vtab_impl::provenance_t::user) {
                return ec.make_error(
                    FMT_STRING("a table with the name '{}' already exists"),
                    tab_name->to_string_fragment());
            }
            lnav_data.ld_vtab_manager->unregister_vtab(
                tab_name->to_string_fragment());
        }

        auto errmsg = lnav_data.ld_vtab_manager->register_vtab(lst);
        if (errmsg.empty()) {
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

    if (args.size() < 2) {
        return ec.make_error("expecting a table name");
    }
    for (auto lpc = size_t{1}; lpc < args.size(); lpc++) {
        auto& table_name = args[lpc];
        auto tab = lnav_data.ld_vtab_manager->lookup_impl(table_name);
        if (tab == nullptr
            || dynamic_cast<log_search_table*>(tab.get()) == nullptr
            || tab->vi_provenance != log_vtab_impl::provenance_t::user)
        {
            return ec.make_error("unknown search table -- {}", table_name);
        }

        if (ec.ec_dry_run) {
            continue;
        }

        auto rc = lnav_data.ld_vtab_manager->unregister_vtab(args[1]);

        if (rc.empty()) {
            retval = "info: deleted search table";
        } else {
            return ec.make_error("{}", rc);
        }
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_session(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    std::string retval;

    if (ec.ec_dry_run) {
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
        }
        if (getenv("HOME") == NULL) {
            return ec.make_error("the HOME environment variable is not set");
        }
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
    } else {
        return ec.make_error("expecting a command to save to the session file");
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
                          .with_reason(split_err.se_error.te_msg)
                          .with_snippet(lnav::console::snippet::from(
                              SRC, lexer.to_attr_line(split_err.se_error)))
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
com_comment(exec_context& ec,
            std::string cmdline,
            std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
        args[1] = trim(remaining_args(cmdline, args));

        if (ec.ec_dry_run) {
            md2attr_line mdal;

            auto parse_res = md4cpp::parse(args[1], mdal);
            if (parse_res.isOk()) {
                auto al = parse_res.unwrap();
                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_value("Comment rendered as markdown:");
            lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();
                lnav_data.ld_preview_view[0].set_sub_source(
                    &lnav_data.ld_preview_source[0]);
                lnav_data.ld_preview_source[0].replace_with(al);
            }

            return Ok(std::string());
        }
        auto* tc = *lnav_data.ld_view_stack.top();

        if (tc != &lnav_data.ld_views[LNV_LOG]) {
            return ec.make_error(
                "The :comment command only works in the log view");
        }
        auto& lss = lnav_data.ld_log_source;

        auto unquoted = auto_buffer::alloc(args[1].size() + 1);
        auto unquoted_len = unquote_content(
            unquoted.in(), args[1].c_str(), args[1].size(), 0);
        unquoted.resize(unquoted_len + 1);

        auto vl = ec.ec_top_line;
        tc->set_user_mark(&textview_curses::BM_META, vl, true);

        auto& line_meta = lss.get_bookmark_metadata(vl);

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

        return {trim(cmdline) + " " + trimmed_comment};
    }

    return {""};
}

static Result<std::string, lnav::console::user_message>
com_clear_comment(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    std::string retval;

    if (ec.ec_dry_run) {
        return Ok(std::string());
    }
    auto* tc = *lnav_data.ld_view_stack.top();
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

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_tag(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
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

    if (args.size() > 1) {
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

    if (args.size() > 1) {
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

        for (auto iter = vbm.bv_tree.begin(); iter != vbm.bv_tree.end();) {
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
                size_t off = std::distance(vbm.bv_tree.begin(), iter);
                auto vl = *iter;
                tc->set_user_mark(&textview_curses::BM_META, vl, false);
                if (line_meta->empty(bookmark_metadata::categories::any)) {
                    lss.erase_bookmark_metadata(vl);
                }

                iter = std::next(vbm.bv_tree.begin(), off);
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

    if (args.size() > 1) {
        if (ec.ec_dry_run) {
            retval = "";
        } else {
            auto& tc = lnav_data.ld_views[LNV_LOG];
            auto& lss = lnav_data.ld_log_source;

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

    if (args.size() == 1) {
        auto& tc = lnav_data.ld_views[LNV_LOG];
        auto& lss = lnav_data.ld_log_source;
        auto& bv = tc.get_bookmarks()[&textview_curses::BM_PARTITION];
        std::optional<vis_line_t> part_start;

        if (bv.bv_tree.exists(tc.get_selection())) {
            part_start = tc.get_selection();
        } else {
            part_start = bv.prev(tc.get_selection());
        }
        if (!part_start) {
            return ec.make_error("focused line is not in a partition");
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

    if (!setup_logline_table(ec)) {
        return ec.make_error("no log data available");
    }
    if (args.size() == 1) {
        return ec.make_error("no columns specified");
    }
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
    for (auto iter = other_columns.begin(); iter != other_columns.end(); ++iter)
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

    for (auto iter = num_columns.begin(); iter != num_columns.end(); ++iter) {
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

    for (auto iter = other_columns.begin(); iter != other_columns.end(); ++iter)
    {
        if (iter == other_columns.begin()) {
            query += " GROUP BY ";
        } else {
            query += ",";
        }
        query_frag = sqlite3_mprintf(" \"c_%s\"", iter->c_str());
        query += query_frag;
    }

    for (auto iter = other_columns.begin(); iter != other_columns.end(); ++iter)
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

            if (dls.dls_row_cursors.size() > 0) {
                ensure_view(&lnav_data.ld_views[LNV_DB]);
            }
        }

        lnav_data.ld_bottom_source.update_loading(0, 0);
        lnav_data.ld_status[LNS_BOTTOM].set_needs_update();
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_add_test(exec_context& ec,
             std::string cmdline,
             std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
        return ec.make_error("not expecting any arguments");
    }
    if (!ec.ec_dry_run) {
        auto* tc = *lnav_data.ld_view_stack.top();

        auto& bv = tc->get_bookmarks()[&textview_curses::BM_USER];
        for (auto iter = bv.bv_tree.begin(); iter != bv.bv_tree.end(); ++iter) {
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

    if (args.size() > 1) {
        auto view_index_opt = view_from_string(args[1].c_str());
        if (!view_index_opt) {
            return ec.make_error("invalid view name -- {}", args[1]);
        }
        if (!ec.ec_dry_run) {
            auto* tc = &lnav_data.ld_views[view_index_opt.value()];
            if (args[0] == "switch-to-view") {
                ensure_view(tc);
            } else {
                toggle_view(tc);
            }
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

    if (!ec.ec_dry_run) {
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

    if (ec.ec_dry_run) {
    } else if (args.size() > 1) {
        bool found = false;

        for (size_t lpc = 0; lpc < lnav_zoom_strings.size() && !found; lpc++) {
            if (strcasecmp(args[1].c_str(), lnav_zoom_strings[lpc]) == 0) {
                auto& ss = *lnav_data.ld_spectro_source;
                timeval old_time;

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
    if (!ec.ec_dry_run) {
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
    if (!ec.ec_dry_run) {
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
    if (!ec.ec_dry_run) {
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

    if (!ec.ec_dry_run) {
        auto_mem<FILE> outfile(fclose);
        auto fn = trim(remaining_args(cmdline, args));
        auto to_term = false;

        if (fn == "-" || fn == "/dev/stdout") {
            auto ec_out = ec.get_output();

            if (!ec_out) {
                outfile = auto_mem<FILE>::leak(stdout);

                if (ec.ec_ui_callbacks.uc_pre_stdout_write) {
                    ec.ec_ui_callbacks.uc_pre_stdout_write();
                }
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
            if (ec.ec_ui_callbacks.uc_post_stdout_write) {
                ec.ec_ui_callbacks.uc_post_stdout_write();
            }
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

    if (ec.ec_dry_run) {
        retval = "";
    } else if (args.size() == 2) {
        auto& lss = lnav_data.ld_log_source;
        auto new_level = string2level(args[1].c_str(), args[1].size(), false);
        lss.set_min_log_level(new_level);

        retval = fmt::format(FMT_STRING("info: minimum log level is now -- {}"),
                             level_names[new_level]);
    } else {
        return ec.make_error("expecting a log level name");
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_hide_unmarked(exec_context& ec,
                  std::string cmdline,
                  std::vector<std::string>& args)
{
    std::string retval = "info: hid unmarked lines";

    if (ec.ec_dry_run) {
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
    if (!ec.ec_dry_run) {
        rescan_files(true);
        rebuild_indexes_repeatedly();
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_cd(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    static const intern_string_t SRC = intern_string::lookup("path");

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
                  .with_reason(split_err.se_error.te_msg)
                  .with_snippet(lnav::console::snippet::from(
                      SRC, lexer.to_attr_line(split_err.se_error)))
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
        setenv("PWD", split_args[0].c_str(), 1);
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_sh(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
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
                    },
                    [&pair](bool val) {
                        setenv(pair.first.c_str(), val ? "1" : "0", 1);
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
        lnav_data.ld_files_to_front.emplace_back(display_name);

        return Ok(fmt::format(FMT_STRING("info: executing -- {}"), carg));
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_shexec(exec_context& ec,
           std::string cmdline,
           std::vector<std::string>& args)
{
    if (!ec.ec_dry_run) {
        log_perror(system(cmdline.substr(args[0].size()).c_str()));
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_poll_now(exec_context& ec,
             std::string cmdline,
             std::vector<std::string>& args)
{
    if (!ec.ec_dry_run) {
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
    if (ec.ec_dry_run) {
    } else if (ec.ec_ui_callbacks.uc_redraw) {
        ec.ec_ui_callbacks.uc_redraw();
    }

    return Ok(std::string());
}

static Result<std::string, lnav::console::user_message>
com_echo(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval = "error: expecting a message";

    if (args.size() >= 1) {
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
            lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();
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
    static auto& prompt = lnav::prompt::get();

    std::string retval;

    if (ec.ec_dry_run) {
        retval = "";
    } else if (args.size() == 1) {
        prompt.p_editor.clear_alt_value();
        retval = "";
    } else {
        std::string msg = remaining_args(cmdline, args);

        prompt.p_editor.set_alt_value(msg);
        retval = "";
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_eval(exec_context& ec, std::string cmdline, std::vector<std::string>& args)
{
    std::string retval;

    if (args.size() > 1) {
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
            lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();

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
        TRY(me.final());
        retval = std::move(me.me_last_result);
    } else {
        return ec.make_error("expecting a command or query to evaluate");
    }

    return Ok(retval);
}

static auto CONFIG_HELP
    = help_text(":config")
          .with_summary("Read or write a configuration option")
          .with_parameter(
              help_text{"option", "The path to the option to read or write"}
                  .with_format(help_parameter_format_t::HPF_CONFIG_PATH))
          .with_parameter(
              help_text("value",
                        "The value to write.  If not given, the "
                        "current value is returned")
                  .optional()
                  .with_format(help_parameter_format_t::HPF_CONFIG_VALUE))
          .with_example({"To read the configuration of the "
                         "'/ui/clock-format' option",
                         "/ui/clock-format"})
          .with_example({"To set the '/ui/dim-text' option to 'false'",
                         "/ui/dim-text false"})
          .with_tags({"configuration"});

static Result<std::string, lnav::console::user_message>
com_config(exec_context& ec,
           std::string cmdline,
           std::vector<std::string>& args)
{
    static auto& prompt = lnav::prompt::get();

    std::string retval;

    if (args.size() > 1) {
        static const intern_string_t INPUT_SRC = intern_string::lookup("input");

        auto cmdline_sf = string_fragment::from_str(cmdline);
        auto parse_res = lnav::command::parse_for_call(
            ec,
            cmdline_sf.split_pair(string_fragment::tag1{' '})->second,
            CONFIG_HELP);
        if (parse_res.isErr()) {
            return Err(parse_res.unwrapErr());
        }
        auto parsed_args = parse_res.unwrap();

        log_debug("config dry run %d %d",
                  args.size(),
                  prompt.p_editor.tc_popup.is_visible());
        if (ec.ec_dry_run && args.size() == 2
            && prompt.p_editor.tc_popup.is_visible())
        {
            prompt.p_editor.tc_popup.map_top_row(
                [&parsed_args](const attr_line_t& al) {
                    auto sub_opt = get_string_attr(al.al_attrs,
                                                   lnav::prompt::SUBST_TEXT);
                    if (sub_opt) {
                        auto sub = sub_opt->get();

                        log_debug("doing dry run with popup value");
                        auto& value_arg = parsed_args.p_args["value"];
                        value_arg.a_help = &CONFIG_HELP.ht_parameters[1];
                        value_arg.a_values.emplace_back(
                            shlex::split_element_t{{}, sub});
                    } else {
                        log_debug("completion does not have attr");
                    }
                });
        }

        yajlpp_parse_context ypc(INPUT_SRC, &lnav_config_handlers);
        std::vector<lnav::console::user_message> errors, errors_ignored;
        const auto& option = parsed_args.p_args["option"].a_values[0].se_value;

        lnav_config = rollback_lnav_config;
        ypc.set_path(option)
            .with_obj(lnav_config)
            .with_error_reporter([&errors](const auto& ypc, auto msg) {
                if (msg.um_level == lnav::console::user_message::level::error) {
                    errors.push_back(msg);
                }
            });
        ypc.ypc_active_paths[option] = 0;
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
            const auto& option_value = parsed_args.p_args["value"];

            if (option_value.a_values.empty()
                || ypc.ypc_current_handler == nullptr)
            {
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
                    lnav_data.ld_status[LNS_PREVIEW0].set_needs_update();

                    auto help_text = fmt::format(
                        FMT_STRING(
                            ANSI_BOLD("{}") " " ANSI_UNDERLINE("{}") " -- {}"),
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
                const auto& value = option_value.a_values[0].se_value;
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
                    yajl_string_props_t props{};
                    ypc.ypc_callbacks.yajl_string(
                        &ypc,
                        (const unsigned char*) value.c_str(),
                        value.size(),
                        &props);
                    changed = true;
                } else if (ypc.ypc_current_handler->jph_callbacks.yajl_integer)
                {
                    auto scan_res = scn::scan_value<int64_t>(value);
                    if (!scan_res || !scan_res->range().empty()) {
                        return ec.make_error("expecting an integer, found: {}",
                                             value);
                    }
                    ypc.ypc_callbacks.yajl_integer(&ypc, scan_res->value());
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
                    }
                    errors.pop_back();
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
                        }
                        errors.pop_back();
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

    if (args.size() == 1) {
        return ec.make_error("expecting a configuration option to reset");
    }
    static const auto INPUT_SRC = intern_string::lookup("input");

    yajlpp_parse_context ypc(INPUT_SRC, &lnav_config_handlers);
    std::string option = args[1];

    while (!option.empty() && option.back() == '/') {
        option.pop_back();
    }
    lnav_config = rollback_lnav_config;
    ypc.set_path(option).with_obj(lnav_config);
    ypc.ypc_active_paths[option] = 0;
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

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
com_spectrogram(exec_context& ec,
                std::string cmdline,
                std::vector<std::string>& args)
{
    std::string retval;

    if (ec.ec_dry_run) {
        retval = "";
    } else if (args.size() == 2) {
        auto colname = remaining_args(cmdline, args);
        auto& ss = *lnav_data.ld_spectro_source;
        bool found = false;

        ss.ss_granularity = ZOOM_LEVELS[lnav_data.ld_zoom_level];
        if (ss.ss_value_source != nullptr) {
            delete std::exchange(ss.ss_value_source, nullptr);
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

#if 0
            if (lnav_data.ld_rl_view != nullptr) {
                lnav_data.ld_rl_view->set_alt_value(
                    HELP_MSG_2(z, Z, "to zoom in/out"));
            }
#endif

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
    if (!ec.ec_dry_run) {
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
    static auto& prompt = lnav::prompt::get();
    auto* tc = *lnav_data.ld_view_stack.top();

    rollback_lnav_config = lnav_config;
    lnav_data.ld_doc_status_source.set_title("Command Help");
    lnav_data.ld_doc_status_source.set_description(
        " See " ANSI_BOLD("https://docs.lnav.org/en/latest/"
                          "commands.html") " for more details");

    set_view_mode(ln_mode_t::COMMAND);
    lnav_data.ld_exec_context.ec_top_line = tc->get_selection();
    prompt.focus_for(*tc, ':', args);

    rl_set_help();
}

static void
script_prompt(std::vector<std::string>& args)
{
    static auto& prompt = lnav::prompt::get();

    auto* tc = *lnav_data.ld_view_stack.top();

    set_view_mode(ln_mode_t::EXEC);

    lnav_data.ld_exec_context.ec_top_line = tc->get_selection();
    prompt.focus_for(*tc, '|', args);
    lnav_data.ld_bottom_source.set_prompt(
        "Enter a script to execute: (Press " ANSI_BOLD("Esc") " to abort)");
    lnav_data.ld_status[LNS_BOTTOM].set_needs_update();
}

static void
search_prompt(std::vector<std::string>& args)
{
    static auto& prompt = lnav::prompt::get();

    auto* tc = *lnav_data.ld_view_stack.top();

    log_debug("search prompt");
    set_view_mode(ln_mode_t::SEARCH);
    lnav_data.ld_exec_context.ec_top_line = tc->get_selection();
    lnav_data.ld_search_start_line = tc->get_selection();
    prompt.focus_for(*tc, '/', args);
    lnav_data.ld_doc_status_source.set_title("Syntax Help");
    lnav_data.ld_doc_status_source.set_description("");
    rl_set_help();
    lnav_data.ld_bottom_source.set_prompt(
        "Search for:  "
        "(Press " ANSI_BOLD("CTRL+J") " to jump to a previous hit and "
        ANSI_BOLD("Esc") " to abort)");
    lnav_data.ld_status[LNS_BOTTOM].set_needs_update();
}

static void
search_filters_prompt(std::vector<std::string>& args)
{
    static auto& prompt = lnav::prompt::get();

    set_view_mode(ln_mode_t::SEARCH_FILTERS);
    auto* tc = get_textview_for_mode(lnav_data.ld_mode);
    lnav_data.ld_filter_view.reload_data();
    prompt.focus_for(*tc, '/', args);
    lnav_data.ld_bottom_source.set_prompt(
        "Search for:  "
        "(Press " ANSI_BOLD("CTRL+J") " to jump to a previous hit and "
        ANSI_BOLD("Esc") " to abort)");
    lnav_data.ld_status[LNS_BOTTOM].set_needs_update();
}

static void
search_files_prompt(std::vector<std::string>& args)
{
    static auto& prompt = lnav::prompt::get();

    set_view_mode(ln_mode_t::SEARCH_FILES);
    auto* tc = get_textview_for_mode(lnav_data.ld_mode);
    prompt.focus_for(*tc, '/', args);
    lnav_data.ld_bottom_source.set_prompt(
        "Search for:  "
        "(Press " ANSI_BOLD("CTRL+J") " to jump to a previous hit and "
        ANSI_BOLD("Esc") " to abort)");
    lnav_data.ld_status[LNS_BOTTOM].set_needs_update();
}

static void
search_spectro_details_prompt(std::vector<std::string>& args)
{
    static auto& prompt = lnav::prompt::get();

    set_view_mode(ln_mode_t::SEARCH_SPECTRO_DETAILS);
    auto* tc = get_textview_for_mode(lnav_data.ld_mode);
    prompt.focus_for(*tc, '/', args);

    lnav_data.ld_bottom_source.set_prompt(
        "Search for:  "
        "(Press " ANSI_BOLD("CTRL+J") " to jump to a previous hit and "
        ANSI_BOLD("Esc") " to abort)");
    lnav_data.ld_status[LNS_BOTTOM].set_needs_update();
}

static void
sql_prompt(std::vector<std::string>& args)
{
    static auto& prompt = lnav::prompt::get();

    auto* tc = *lnav_data.ld_view_stack.top();
    auto& log_view = lnav_data.ld_views[LNV_LOG];

    lnav_data.ld_exec_context.ec_top_line = tc->get_selection();

    set_view_mode(ln_mode_t::SQL);
    setup_logline_table(lnav_data.ld_exec_context);
    prompt.focus_for(*tc, ';', args);

    lnav_data.ld_doc_status_source.set_title("Query Help");
    lnav_data.ld_doc_status_source.set_description(
        "See " ANSI_BOLD("https://docs.lnav.org/en/latest/"
                         "sqlext.html") " for more details");
    rl_set_help();
    lnav_data.ld_bottom_source.update_loading(0, 0);
    lnav_data.ld_status[LNS_BOTTOM].set_needs_update();

    auto* fos = (field_overlay_source*) log_view.get_overlay_source();
    fos->fos_contexts.top().c_show = true;
    tc->set_sync_selection_and_top(true);
    tc->reload_data();
    tc->set_overlay_selection(3_vl);
    lnav_data.ld_bottom_source.set_prompt(
        "Enter an SQL query: (Press " ANSI_BOLD(
            "CTRL+L") " for multi-line mode and " ANSI_BOLD("Esc") " to "
                                                                   "abort)");
}

static void
user_prompt(std::vector<std::string>& args)
{
    static auto& prompt = lnav::prompt::get();

    auto* tc = *lnav_data.ld_view_stack.top();
    lnav_data.ld_exec_context.ec_top_line = tc->get_selection();

    set_view_mode(ln_mode_t::USER);
    setup_logline_table(lnav_data.ld_exec_context);
    prompt.focus_for(*tc, '\0', args);

    lnav_data.ld_bottom_source.update_loading(0, 0);
    lnav_data.ld_status[LNS_BOTTOM].set_needs_update();
}

static Result<std::string, lnav::console::user_message>
com_prompt(exec_context& ec,
           std::string cmdline,
           std::vector<std::string>& args)
{
    static auto& prompt = lnav::prompt::get();
    static const std::map<std::string,
                          std::function<void(std::vector<std::string>&)>>
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

    if (!ec.ec_dry_run) {
        static const intern_string_t SRC = intern_string::lookup("flags");

        auto lexer = shlex(cmdline);
        auto split_args_res = lexer.split(ec.create_resolver());
        if (split_args_res.isErr()) {
            auto split_err = split_args_res.unwrapErr();
            auto um
                = lnav::console::user_message::error("unable to parse prompt")
                      .with_reason(split_err.se_error.te_msg)
                      .with_snippet(lnav::console::snippet::from(
                          SRC, lexer.to_attr_line(split_err.se_error)))
                      .move();

            return Err(um);
        }

        auto split_args = split_args_res.unwrap()
            | lnav::itertools::map(
                              [](const auto& elem) { return elem.se_value; });

        auto alt_flag
            = std::find(split_args.begin(), split_args.end(), "--alt");
        prompt.p_alt_mode = alt_flag != split_args.end();
        if (prompt.p_alt_mode) {
            split_args.erase(alt_flag);
        }

        auto prompter = PROMPT_TYPES.find(split_args[1]);

        if (prompter == PROMPT_TYPES.end()) {
            return ec.make_error("Unknown prompt type: {}", split_args[1]);
        }

        prompter->second(split_args);
    }
    return Ok(std::string());
}

readline_context::command_t STD_COMMANDS[] = {
    {
        "prompt",
        com_prompt,

        help_text(":prompt")
            .with_summary("Open the given prompt")
            .with_parameter(
                help_text{"type", "The type of prompt"}.with_enum_values({
                    "breadcrumb",
                    "command",
                    "script",
                    "search",
                    "sql",
                }))
            .with_parameter(help_text("--alt",
                                      "Perform the alternate action "
                                      "for this prompt by default")
                                .optional())
            .with_parameter(
                help_text("prompt", "The prompt to display").optional())
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
            }),
    },

    {
        "adjust-log-time",
        com_adjust_log_time,

        help_text(":adjust-log-time")
            .with_summary(
                "Change the timestamps of the focused file to be relative "
                "to the given date")
            .with_parameter(
                help_text("timestamp",
                          "The new timestamp for the focused line in the view")
                    .with_format(help_parameter_format_t::HPF_ADJUSTED_TIME))
            .with_example({"To set the focused timestamp to a given date",
                           "2017-01-02T05:33:00"})
            .with_example({"To set the focused timestamp back an hour", "-1h"})
            .with_opposites({"clear-adjusted-log-time"}),
    },
    {
        "clear-adjusted-log-time",
        com_clear_adjusted_log_time,

        help_text(":clear-adjusted-log-time")
            .with_summary(
                "Clear the adjusted time for the focused line in the view")
            .with_opposites({":adjust-log-time"}),
    },

    {
        "unix-time",
        com_unix_time,

        help_text(":unix-time")
            .with_summary("Convert epoch time to a human-readable form")
            .with_parameter(
                help_text("seconds", "The epoch timestamp to convert")
                    .with_format(help_parameter_format_t::HPF_INTEGER))
            .with_example(
                {"To convert the epoch time 1490191111", "1490191111"}),
    },
    {
        "convert-time-to",
        com_convert_time_to,
        help_text(":convert-time-to")
            .with_summary("Convert the focused timestamp to the "
                          "given timezone")
            .with_parameter(
                help_text("zone", "The timezone name")
                    .with_format(help_parameter_format_t::HPF_TIMEZONE)),
    },
    {
        "set-file-timezone",
        com_set_file_timezone,
        help_text(":set-file-timezone")
            .with_summary("Set the timezone to use for log messages that do "
                          "not include a timezone.  The timezone is applied "
                          "to "
                          "the focused file or the given glob pattern.")
            .with_parameter(help_text{"zone", "The timezone name"}.with_format(
                help_parameter_format_t::HPF_TIMEZONE))
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
            .with_parameter(
                help_text{"pattern",
                          "The glob pattern to match against files "
                          "that should no longer use this timezone"}
                    .with_format(help_parameter_format_t::HPF_FILE_WITH_ZONE))
            .with_tags({"file-options"}),
        com_clear_file_timezone_prompt,
    },
    {"current-time",
     com_current_time,

     help_text(":current-time")
         .with_summary("Print the current time in human-readable form and "
                       "seconds since the epoch")},
    {
        "goto",
        com_goto,

        help_text(":goto")
            .with_summary("Go to the given location in the top view")
            .with_parameter(
                help_text("line#|N%|timestamp|#anchor",
                          "A line number, percent into the file, timestamp, "
                          "or an anchor in a text file")
                    .with_format(help_parameter_format_t::HPF_LOCATION))
            .with_examples(
                {{"To go to line 22", "22"},
                 {"To go to the line 75% of the way into the view", "75%"},
                 {"To go to the first message on the first day of "
                  "2017",
                  "2017-01-01"},
                 {"To go to the Screenshots section", "#screenshots"}})
            .with_tags({"navigation"}),
    },
    {
        "relative-goto",
        com_relative_goto,
        help_text(":relative-goto")
            .with_summary(
                "Move the current view up or down by the given amount")
            .with_parameter(
                {"line-count|N%", "The amount to move the view by."})
            .with_examples({
                {"To move 22 lines down in the view", "+22"},
                {"To move 10 percent back in the view", "-10%"},
            })
            .with_tags({"navigation"}),
    },

    {
        "annotate",
        com_annotate,

        help_text(":annotate")
            .with_summary("Analyze the focused log message and "
                          "attach annotations")
            .with_tags({"metadata"}),
    },

    {
        "mark-expr",
        com_mark_expr,

        help_text(":mark-expr")
            .with_summary("Set the bookmark expression")
            .with_parameter(
                help_text("expr",
                          "The SQL expression to evaluate for each "
                          "log message.  "
                          "The message values can be accessed "
                          "using column names "
                          "prefixed with a colon")
                    .with_format(help_parameter_format_t::HPF_SQL_EXPR))
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

    {
        "help",
        com_help,

        help_text(":help").with_summary("Open the help text view"),
    },
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
         .with_parameter(help_text("pattern", "The regular expression to match")
                             .with_format(help_parameter_format_t::HPF_REGEX))
         .with_tags({"display"})
         .with_example(
             {"To highlight numbers with three or more digits", R"(\d{3,})"})},
    {
        "clear-highlight",
        com_clear_highlight,

        help_text(":clear-highlight")
            .with_summary(
                "Remove a previously set highlight regular expression")
            .with_parameter(
                help_text("pattern",
                          "The regular expression previously used "
                          "with :highlight")
                    .with_format(help_parameter_format_t::HPF_HIGHLIGHTS))
            .with_tags({"display"})
            .with_opposites({"highlight"})
            .with_example(
                {"To clear the highlight with the pattern 'foobar'", "foobar"}),
    },
    {
        "filter-expr",
        com_filter_expr,

        help_text(":filter-expr")
            .with_summary("Set the filter expression")
            .with_parameter(
                help_text("expr",
                          "The SQL expression to evaluate for each "
                          "log message.  "
                          "The message values can be accessed "
                          "using column names "
                          "prefixed with a colon")
                    .with_format(help_parameter_format_t::HPF_SQL_EXPR))
            .with_opposites({"clear-filter-expr"})
            .with_tags({"filtering"})
            .with_example({"To set a filter expression that matched syslog "
                           "messages from 'syslogd'",
                           ":log_procname = 'syslogd'"})
            .with_example(
                {"To set a filter expression that matches log "
                 "messages where 'id' is followed by a number and contains the "
                 "string 'foo'",
                 ":log_body REGEXP 'id\\d+' AND :log_body REGEXP 'foo'"}),

        com_filter_expr_prompt,
    },
    {"clear-filter-expr",
     com_clear_filter_expr,

     help_text(":clear-filter-expr")
         .with_summary("Clear the filter expression")
         .with_opposites({"filter-expr"})
         .with_tags({"filtering"})},
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
         .with_summary("Create an SQL table using the focused line of "
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
             help_text("table-name", "The name of the table to delete")
                 .with_format(help_parameter_format_t::HPF_LOGLINE_TABLE))
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
                 .optional()
                 .with_format(help_parameter_format_t::HPF_REGEX))
         .with_tags({"vtables", "sql"})
         .with_example({"To create a table named 'task_durations' that "
                        "matches log "
                        "messages with the pattern "
                        "'duration=(?<duration>\\d+)'",
                        R"(task_durations duration=(?<duration>\d+))"})},
    {"delete-search-table",
     com_delete_search_table,

     help_text(":delete-search-table")
         .with_summary("Delete a search table")
         .with_parameter(
             help_text("table-name", "The name of the table to delete")
                 .one_or_more()
                 .with_format(help_parameter_format_t::HPF_SEARCH_TABLE))
         .with_opposites({"create-search-table"})
         .with_tags({"vtables", "sql"})
         .with_example({"To delete the search table named 'task_durations'",
                        "task_durations"})},
    {
        "hide-file",
        com_file_visibility,

        help_text(":hide-file")
            .with_summary("Hide the given file(s) and skip indexing until it "
                          "is shown again.  If no path is given, the current "
                          "file in the view is hidden")
            .with_parameter(
                help_text{"path",
                          "A path or glob pattern that "
                          "specifies the files to hide"}
                    .with_format(help_parameter_format_t::HPF_VISIBLE_FILES)
                    .zero_or_more())
            .with_opposites({"show-file"}),
    },
    {
        "show-file",
        com_file_visibility,

        help_text(":show-file")
            .with_summary("Show the given file(s) and resume indexing.")
            .with_parameter(
                help_text{"path",
                          "The path or glob pattern that "
                          "specifies the files to show"}
                    .with_format(help_parameter_format_t::HPF_HIDDEN_FILES)
                    .zero_or_more())
            .with_opposites({"hide-file"}),
    },
    {
        "show-only-this-file",
        com_file_visibility,

        help_text(":show-only-this-file")
            .with_summary("Show only the file for the focused line in the view")
            .with_opposites({"hide-file"}),
    },
    {
        "comment",
        com_comment,

        help_text(":comment")
            .with_summary("Attach a comment to the focused log line.  The "
                          "comment will be "
                          "displayed right below the log message it is "
                          "associated with. "
                          "The comment can contain Markdown directives for "
                          "styling and linking.")
            .with_parameter(
                help_text("text", "The comment text")
                    .with_format(help_parameter_format_t::HPF_MULTILINE_TEXT))
            .with_example({"To add the comment 'This is where it all went "
                           "wrong' to the focused line",
                           "This is where it all went wrong"})
            .with_tags({"metadata"}),

        com_comment_prompt,
    },
    {"clear-comment",
     com_clear_comment,

     help_text(":clear-comment")
         .with_summary("Clear the comment attached to the focused log line")
         .with_opposites({"comment"})
         .with_tags({"metadata"})},
    {
        "tag",
        com_tag,

        help_text(":tag")
            .with_summary("Attach tags to the focused log line")
            .with_parameter(help_text("tag", "The tags to attach")
                                .one_or_more()
                                .with_format(help_parameter_format_t::HPF_TAG))
            .with_example({"To add the tags '#BUG123' and '#needs-review' to "
                           "the focused line",
                           "#BUG123 #needs-review"})
            .with_tags({"metadata"}),
    },
    {
        "untag",
        com_untag,

        help_text(":untag")
            .with_summary("Detach tags from the focused log line")
            .with_parameter(
                help_text("tag", "The tags to detach")
                    .one_or_more()
                    .with_format(help_parameter_format_t::HPF_LINE_TAG))
            .with_example({"To remove the tags '#BUG123' and "
                           "'#needs-review' from the focused line",
                           "#BUG123 #needs-review"})
            .with_opposites({"tag"})
            .with_tags({"metadata"}),
    },
    {"delete-tags",
     com_delete_tags,

     help_text(":delete-tags")
         .with_summary("Remove the given tags from all log lines")
         .with_parameter(help_text("tag", "The tags to delete")
                             .one_or_more()
                             .with_format(help_parameter_format_t::HPF_TAG))
         .with_example({"To remove the tags '#BUG123' and "
                        "'#needs-review' from "
                        "all log lines",
                        "#BUG123 #needs-review"})
         .with_opposites({"tag"})
         .with_tags({"metadata"})},
    {"partition-name",
     com_partition_name,

     help_text(":partition-name")
         .with_summary(
             "Mark the focused line in the log view as the start of a "
             "new partition with the given name")
         .with_parameter(help_text("name", "The name for the new partition")
                             .with_format(help_parameter_format_t::HPF_TEXT))
         .with_example(
             {"To mark the focused line as the start of the partition "
              "named 'boot #1'",
              "boot #1"})},
    {"clear-partition",
     com_clear_partition,

     help_text(":clear-partition")
         .with_summary("Clear the partition the focused line is a part of")
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
    {
        "summarize",
        com_summarize,

        help_text(":summarize")
            .with_summary("Execute a SQL query that computes the "
                          "characteristics "
                          "of the values in the given column")
            .with_parameter(
                help_text("column-name", "The name of the column to analyze.")
                    .with_format(help_parameter_format_t::HPF_FORMAT_FIELD))
            .with_example({"To get a summary of the sc_bytes column in the "
                           "access_log table",
                           "sc_bytes"}),
    },
    {"switch-to-view",
     com_switch_to_view,

     help_text(":switch-to-view")
         .with_summary("Switch to the given view")
         .with_parameter(
             help_text("view-name", "The name of the view to switch to.")
                 .with_enum_values(lnav_view_strings))
         .with_example({"To switch to the 'schema' view", "schema"})},
    {"toggle-view",
     com_switch_to_view,

     help_text(":toggle-view")
         .with_summary("Switch to the given view or, if it is "
                       "already displayed, "
                       "switch to the previous view")
         .with_parameter(
             help_text("view-name",
                       "The name of the view to toggle the display of.")
                 .with_enum_values(lnav_view_strings))
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
         .with_parameter(
             help_text("path", "The path to the file to write")
                 .with_format(help_parameter_format_t::HPF_LOCAL_FILENAME))
         .with_tags({"io", "scripting"})},
    {
        "rebuild",
        com_rebuild,
        help_text(":rebuild")
            .with_summary("Forcefully rebuild file indexes")
            .with_tags({"scripting"}),
    },
    {
        "set-min-log-level",
        com_set_min_log_level,

        help_text(":set-min-log-level")
            .with_summary(
                "Set the minimum log level to display in the log view")
            .with_parameter(help_text("log-level", "The new minimum log level")
                                .with_enum_values(level_names))
            .with_example(
                {"To set the minimum log level displayed to error", "error"}),
    },
    {"redraw",
     com_redraw,

     help_text(":redraw").with_summary("Do a full redraw of the screen")},
    {
        "zoom-to",
        com_zoom_to,

        help_text(":zoom-to")
            .with_summary("Zoom the histogram view to the given level")
            .with_parameter(help_text("zoom-level", "The zoom level")
                                .with_enum_values(lnav_zoom_strings))
            .with_example({"To set the zoom level to '1-week'", "1-week"}),
    },
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
                             .optional()
                             .with_format(help_parameter_format_t::HPF_TEXT))
         .with_parameter(help_text("msg", "The message to display"))
         .with_tags({"io", "scripting"})
         .with_example({"To output 'Hello, World!'", "Hello, World!"})},
    {"alt-msg",
     com_alt_msg,

     help_text(":alt-msg")
         .with_summary("Display a message in the alternate command position")
         .with_parameter(help_text("msg", "The message to display")
                             .with_format(help_parameter_format_t::HPF_TEXT))
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
            .with_parameter(
                help_text("dir", "The new current directory")
                    .with_format(help_parameter_format_t::HPF_DIRECTORY))
            .with_tags({"scripting"}),
    },

    {
        "config",
        com_config,
        CONFIG_HELP,
    },
    {"reset-config",
     com_reset_config,

     help_text(":reset-config")
         .with_summary("Reset the configuration option to its default value")
         .with_parameter(
             help_text("option", "The path to the option to reset")
                 .with_format(help_parameter_format_t::HPF_CONFIG_PATH))
         .with_example({"To reset the '/ui/clock-format' option back to the "
                        "builtin default",
                        "/ui/clock-format"})
         .with_tags({"configuration"})},
    {
        "spectrogram",
        com_spectrogram,

        help_text(":spectrogram")
            .with_summary(
                "Visualize the given message field or database column "
                "using a spectrogram")
            .with_parameter(
                help_text("field-name",
                          "The name of the numeric field to visualize.")
                    .with_format(help_parameter_format_t::HPF_NUMERIC_FIELD))
            .with_example({"To visualize the sc_bytes field in the "
                           "access_log format",
                           "sc_bytes"}),
    },
    {
        "quit",
        com_quit,

        help_text(":quit").with_summary("Quit lnav"),
    },
    {
        "write-debug-log-to",
        com_write_debug_log_to,
        help_text(":write-debug-log-to")
            .with_summary(
                "Write lnav's internal debug log to the given path.  This can "
                "be useful if the `-d` flag was not passed on the command line")
            .with_parameter(
                help_text("path", "The destination path for the debug log")
                    .with_format(help_parameter_format_t::HPF_LOCAL_FILENAME)),
    },
};

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
