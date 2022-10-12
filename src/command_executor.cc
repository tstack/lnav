/**
 * Copyright (c) 2015, Timothy Stack
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

#include <vector>

#include "command_executor.hh"

#include "base/ansi_scrubber.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/itertools.hh"
#include "base/string_util.hh"
#include "bound_tags.hh"
#include "config.h"
#include "db_sub_source.hh"
#include "help_text_formatter.hh"
#include "lnav.hh"
#include "lnav.indexing.hh"
#include "lnav_config.hh"
#include "lnav_util.hh"
#include "log_format_loader.hh"
#include "readline_highlighters.hh"
#include "service_tags.hh"
#include "shlex.hh"
#include "sql_util.hh"
#include "vtab_module.hh"
#include "yajlpp/json_ptr.hh"

using namespace lnav::roles::literals;

exec_context INIT_EXEC_CONTEXT;

static const std::string MSG_FORMAT_STMT = R"(
SELECT count(*) AS total, min(log_line) AS log_line, log_msg_format
    FROM all_logs
    GROUP BY log_msg_format
    ORDER BY total DESC
)";

int
sql_progress(const struct log_cursor& lc)
{
    ssize_t total = lnav_data.ld_log_source.text_line_count();
    off_t off = lc.lc_curr_line;

    if (off < 0 || off >= total) {
        return 0;
    }

    if (lnav_data.ld_window == nullptr) {
        return 0;
    }

    if (!lnav_data.ld_looping) {
        return 1;
    }

    static sig_atomic_t sql_counter = 0;

    if (ui_periodic_timer::singleton().time_to_update(sql_counter)) {
        lnav_data.ld_bottom_source.update_loading(off, total);
        lnav_data.ld_status_refresher();
    }

    return 0;
}

void
sql_progress_finished()
{
    if (lnav_data.ld_window == nullptr) {
        return;
    }

    lnav_data.ld_bottom_source.update_loading(0, 0);
    lnav_data.ld_status_refresher();
    lnav_data.ld_views[LNV_DB].redo_search();
}

static Result<std::string, lnav::console::user_message> execute_from_file(
    exec_context& ec,
    const ghc::filesystem::path& path,
    int line_number,
    const std::string& cmdline);

Result<std::string, lnav::console::user_message>
execute_command(exec_context& ec, const std::string& cmdline)
{
    std::vector<std::string> args;

    log_info("Executing: %s", cmdline.c_str());

    split_ws(cmdline, args);

    if (!args.empty()) {
        readline_context::command_map_t::iterator iter;

        if ((iter = lnav_commands.find(args[0])) == lnav_commands.end()) {
            return ec.make_error("unknown command - {}", args[0]);
        }

        ec.ec_current_help = &iter->second->c_help;
        auto retval = iter->second->c_func(ec, cmdline, args);
        if (retval.isErr()) {
            auto um = retval.unwrapErr();

            ec.add_error_context(um);
            ec.ec_current_help = nullptr;
            return Err(um);
        }
        ec.ec_current_help = nullptr;

        return retval;
    }

    return ec.make_error("no command to execute");
}

static Result<std::map<std::string, scoped_value_t>,
              lnav::console::user_message>
bind_sql_parameters(exec_context& ec, sqlite3_stmt* stmt)
{
    std::map<std::string, scoped_value_t> retval;
    auto param_count = sqlite3_bind_parameter_count(stmt);
    for (int lpc = 0; lpc < param_count; lpc++) {
        std::map<std::string, std::string>::iterator ov_iter;
        const auto* name = sqlite3_bind_parameter_name(stmt, lpc + 1);
        if (name == nullptr) {
            auto um
                = lnav::console::user_message::error("invalid SQL statement")
                      .with_reason(
                          "using a question-mark (?) for bound variables "
                          "is not supported, only named bound parameters "
                          "are supported")
                      .with_help(
                          "named parameters start with a dollar-sign "
                          "($) or colon (:) followed by the variable name");
            ec.add_error_context(um);

            return Err(um);
        }

        ov_iter = ec.ec_override.find(name);
        if (ov_iter != ec.ec_override.end()) {
            sqlite3_bind_text(stmt,
                              lpc,
                              ov_iter->second.c_str(),
                              ov_iter->second.length(),
                              SQLITE_TRANSIENT);
        } else if (name[0] == '$') {
            const auto& lvars = ec.ec_local_vars.top();
            const auto& gvars = ec.ec_global_vars;
            std::map<std::string, scoped_value_t>::const_iterator local_var,
                global_var;
            const char* env_value;

            if (lnav_data.ld_window) {
                char buf[32];
                int lines, cols;

                getmaxyx(lnav_data.ld_window, lines, cols);
                if (strcmp(name, "$LINES") == 0) {
                    snprintf(buf, sizeof(buf), "%d", lines);
                    sqlite3_bind_text(stmt, lpc + 1, buf, -1, SQLITE_TRANSIENT);
                } else if (strcmp(name, "$COLS") == 0) {
                    snprintf(buf, sizeof(buf), "%d", cols);
                    sqlite3_bind_text(stmt, lpc + 1, buf, -1, SQLITE_TRANSIENT);
                }
            }

            if ((local_var = lvars.find(&name[1])) != lvars.end()) {
                mapbox::util::apply_visitor(
                    sqlitepp::bind_visitor(stmt, lpc + 1), local_var->second);
                retval[name] = local_var->second;
            } else if ((global_var = gvars.find(&name[1])) != gvars.end()) {
                mapbox::util::apply_visitor(
                    sqlitepp::bind_visitor(stmt, lpc + 1), global_var->second);
                retval[name] = global_var->second;
            } else if ((env_value = getenv(&name[1])) != nullptr) {
                sqlite3_bind_text(stmt, lpc + 1, env_value, -1, SQLITE_STATIC);
                retval[name] = env_value;
            }
        } else if (name[0] == ':' && ec.ec_line_values != nullptr) {
            for (auto& lv : ec.ec_line_values->lvv_values) {
                if (lv.lv_meta.lvm_name != &name[1]) {
                    continue;
                }
                switch (lv.lv_meta.lvm_kind) {
                    case value_kind_t::VALUE_BOOLEAN:
                        sqlite3_bind_int64(stmt, lpc + 1, lv.lv_value.i);
                        retval[name] = fmt::to_string(lv.lv_value.i);
                        break;
                    case value_kind_t::VALUE_FLOAT:
                        sqlite3_bind_double(stmt, lpc + 1, lv.lv_value.d);
                        retval[name] = fmt::to_string(lv.lv_value.d);
                        break;
                    case value_kind_t::VALUE_INTEGER:
                        sqlite3_bind_int64(stmt, lpc + 1, lv.lv_value.i);
                        retval[name] = fmt::to_string(lv.lv_value.i);
                        break;
                    case value_kind_t::VALUE_NULL:
                        sqlite3_bind_null(stmt, lpc + 1);
                        retval[name] = db_label_source::NULL_STR;
                        break;
                    default:
                        sqlite3_bind_text(stmt,
                                          lpc + 1,
                                          lv.text_value(),
                                          lv.text_length(),
                                          SQLITE_TRANSIENT);
                        retval[name] = lv.to_string();
                        break;
                }
            }
        } else {
            sqlite3_bind_null(stmt, lpc + 1);
            log_warning("Could not bind variable: %s", name);
            retval[name] = db_label_source::NULL_STR;
        }
    }

    return Ok(retval);
}

static void
execute_search(const std::string& search_cmd)
{
    lnav_data.ld_view_stack.top() | [&search_cmd](auto tc) {
        auto search_term
            = string_fragment(search_cmd)
                  .find_right_boundary(0, string_fragment::tag1{'\n'})
                  .to_string();
        tc->execute_search(search_term);
    };
}

Result<std::string, lnav::console::user_message>
execute_sql(exec_context& ec, const std::string& sql, std::string& alt_msg)
{
    db_label_source& dls = lnav_data.ld_db_row_source;
    auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
    struct timeval start_tv, end_tv;
    std::string stmt_str = trim(sql);
    std::string retval;
    int retcode = SQLITE_OK;

    log_info("Executing SQL: %s", sql.c_str());

    auto old_mode = lnav_data.ld_mode;
    lnav_data.ld_mode = ln_mode_t::BUSY;
    auto mode_fin = finally([old_mode]() { lnav_data.ld_mode = old_mode; });
    lnav_data.ld_bottom_source.grep_error("");

    if (startswith(stmt_str, ".")) {
        std::vector<std::string> args;
        split_ws(stmt_str, args);

        auto* sql_cmd_map = injector::get<readline_context::command_map_t*,
                                          sql_cmd_map_tag>();
        auto cmd_iter = sql_cmd_map->find(args[0]);

        if (cmd_iter != sql_cmd_map->end()) {
            ec.ec_current_help = &cmd_iter->second->c_help;
            auto retval = cmd_iter->second->c_func(ec, stmt_str, args);
            ec.ec_current_help = nullptr;

            return retval;
        }
    }

    if (stmt_str == ".msgformats") {
        stmt_str = MSG_FORMAT_STMT;
    }

    ec.ec_accumulator->clear();

    const auto& source = ec.ec_source.back();
    sql_progress_guard progress_guard(sql_progress,
                                      sql_progress_finished,
                                      source.s_location,
                                      source.s_content);
    gettimeofday(&start_tv, nullptr);

    const auto* curr_stmt = stmt_str.c_str();
    auto last_is_readonly = false;
    while (curr_stmt != nullptr) {
        const char* tail = nullptr;
        while (isspace(*curr_stmt)) {
            curr_stmt += 1;
        }
        retcode = sqlite3_prepare_v2(
            lnav_data.ld_db.in(), curr_stmt, -1, stmt.out(), &tail);
        if (retcode != SQLITE_OK) {
            const char* errmsg = sqlite3_errmsg(lnav_data.ld_db);

            alt_msg = "";

            auto um = lnav::console::user_message::error(
                          "failed to compile SQL statement")
                          .with_reason(errmsg)
                          .with_snippets(ec.ec_source);

            auto annotated_sql = annotate_sql_with_error(
                lnav_data.ld_db.in(), curr_stmt, tail);
            auto loc = um.um_snippets.back().s_location;
            if (curr_stmt == stmt_str.c_str()) {
                um.um_snippets.pop_back();
            } else {
                auto tail_iter = stmt_str.begin();

                std::advance(tail_iter, (curr_stmt - stmt_str.c_str()));
                loc.sl_line_number
                    += std::count(stmt_str.begin(), tail_iter, '\n');
            }

            um.with_snippet(lnav::console::snippet::from(loc, annotated_sql));

            return Err(um);
        }
        if (stmt == nullptr) {
            retcode = SQLITE_DONE;
            break;
        }
#ifdef HAVE_SQLITE3_STMT_READONLY
        last_is_readonly = sqlite3_stmt_readonly(stmt.in());
        if (ec.is_read_only() && !last_is_readonly) {
            return ec.make_error(
                "modifying statements are not allowed in this context: {}",
                sql);
        }
#endif
        bool done = false;

        auto bound_values = TRY(bind_sql_parameters(ec, stmt.in()));
        if (lnav_data.ld_rl_view != nullptr) {
            if (lnav_data.ld_rl_view) {
                lnav_data.ld_rl_view->set_attr_value(
                    lnav::console::user_message::info(
                        attr_line_t("executing SQL statement, press ")
                            .append("CTRL+]"_hotkey)
                            .append(" to cancel"))
                        .to_attr_line());
                lnav_data.ld_rl_view->do_update();
            }
        }

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
                    attr_line_t bound_note;

                    if (!bound_values.empty()) {
                        bound_note.append(
                            "the bound parameters are set as follows:\n");
                        for (const auto& bval : bound_values) {
                            auto val_as_str = fmt::to_string(bval.second);
                            auto sql_type = bval.second.match(
                                [](const std::string&) { return SQLITE_TEXT; },
                                [](const string_fragment&) {
                                    return SQLITE_TEXT;
                                },
                                [](int64_t) { return SQLITE_INTEGER; },
                                [](null_value_t) { return SQLITE_NULL; },
                                [](double) { return SQLITE_FLOAT; });
                            auto scrubbed_val = scrub_ws(val_as_str.c_str());
                            truncate_to(scrubbed_val, 40);
                            bound_note.append("  ")
                                .append(lnav::roles::variable(bval.first))
                                .append(":")
                                .append(sqlite3_type_to_string(sql_type))
                                .append(" = ")
                                .append_quoted(scrubbed_val)
                                .append("\n");
                        }
                    }

                    log_error("sqlite3_step error code: %d", retcode);
                    auto um = sqlite3_error_to_user_message(lnav_data.ld_db)
                                  .with_snippets(ec.ec_source)
                                  .with_note(bound_note);

                    return Err(um);
                }
            }
        }

        curr_stmt = tail;
    }

    if (lnav_data.ld_rl_view != nullptr) {
        lnav_data.ld_rl_view->clear_value();
    }

    gettimeofday(&end_tv, nullptr);
    if (retcode == SQLITE_DONE) {
        if (lnav_data.ld_log_source.is_line_meta_changed()) {
            lnav_data.ld_log_source.text_filters_changed();
            lnav_data.ld_views[LNV_LOG].reload_data();
        }
        lnav_data.ld_filter_view.reload_data();
        lnav_data.ld_files_view.reload_data();
        lnav_data.ld_views[LNV_DB].reload_data();
        lnav_data.ld_views[LNV_DB].set_left(0);

        lnav_data.ld_active_files.fc_files
            | lnav::itertools::for_each(&logfile::dump_stats);
        if (ec.ec_sql_callback != sql_callback) {
            retval = ec.ec_accumulator->get_string();
        } else if (!dls.dls_rows.empty()) {
            if (lnav_data.ld_flags & LNF_HEADLESS) {
                if (ec.ec_local_vars.size() == 1) {
                    ensure_view(&lnav_data.ld_views[LNV_DB]);
                }

                retval = "";
                alt_msg = "";
            } else if (dls.dls_rows.size() == 1) {
                auto& row = dls.dls_rows[0];

                if (dls.dls_headers.size() == 1) {
                    retval = row[0];
                } else {
                    for (unsigned int lpc = 0; lpc < dls.dls_headers.size();
                         lpc++)
                    {
                        if (lpc > 0) {
                            retval.append("; ");
                        }
                        retval.append(dls.dls_headers[lpc].hm_name);
                        retval.push_back('=');
                        retval.append(row[lpc]);
                    }
                }
            } else {
                int row_count = dls.dls_rows.size();
                char row_count_buf[128];
                struct timeval diff_tv;

                timersub(&end_tv, &start_tv, &diff_tv);
                snprintf(row_count_buf,
                         sizeof(row_count_buf),
                         ANSI_BOLD("%'d") " row%s matched in " ANSI_BOLD(
                             "%ld.%03ld") " seconds",
                         row_count,
                         row_count == 1 ? "" : "s",
                         diff_tv.tv_sec,
                         std::max((long) diff_tv.tv_usec / 1000, 1L));
                retval = row_count_buf;
                if (dls.has_log_time_column()) {
                    alt_msg = HELP_MSG_1(Q,
                                         "to switch back to the previous view "
                                         "at the matching 'log_time' value");
                } else {
                    alt_msg = "";
                }
            }
        }
#ifdef HAVE_SQLITE3_STMT_READONLY
        else if (last_is_readonly)
        {
            retval = "info: No rows matched";
            alt_msg = "";

            if (lnav_data.ld_flags & LNF_HEADLESS) {
                if (ec.ec_local_vars.size() == 1) {
                    ensure_view(&lnav_data.ld_views[LNV_DB]);
                }
            }
        }
#endif
    }

    return Ok(retval);
}

static Result<std::string, lnav::console::user_message>
execute_file_contents(exec_context& ec,
                      const ghc::filesystem::path& path,
                      bool multiline)
{
    static ghc::filesystem::path stdin_path("-");
    static ghc::filesystem::path dev_stdin_path("/dev/stdin");

    std::string retval;
    FILE* file;

    if (path == stdin_path || path == dev_stdin_path) {
        if (isatty(STDIN_FILENO)) {
            return ec.make_error("stdin has already been consumed");
        }
        file = stdin;
    } else if ((file = fopen(path.c_str(), "r")) == nullptr) {
        return ec.make_error("unable to open file");
    }

    int line_number = 0, starting_line_number = 0;
    auto_mem<char> line;
    size_t line_max_size;
    ssize_t line_size;
    nonstd::optional<std::string> cmdline;

    ec.ec_path_stack.emplace_back(path.parent_path());
    exec_context::output_guard og(ec);
    while ((line_size = getline(line.out(), &line_max_size, file)) != -1) {
        line_number += 1;

        if (trim(line.in()).empty()) {
            if (multiline && cmdline) {
                cmdline = cmdline.value() + "\n";
            }
            continue;
        }
        if (line[0] == '#') {
            continue;
        }

        switch (line[0]) {
            case ':':
            case '/':
            case ';':
            case '|':
                if (cmdline) {
                    retval = TRY(execute_from_file(
                        ec, path, starting_line_number, trim(cmdline.value())));
                }

                starting_line_number = line_number;
                cmdline = std::string(line);
                break;
            default:
                if (multiline) {
                    cmdline = fmt::format("{}{}", cmdline.value(), line);
                } else {
                    retval = TRY(execute_from_file(
                        ec, path, line_number, fmt::format(":{}", line.in())));
                }
                break;
        }
    }

    if (cmdline) {
        retval = TRY(execute_from_file(
            ec, path, starting_line_number, trim(cmdline.value())));
    }

    if (file == stdin) {
        if (isatty(STDOUT_FILENO)) {
            log_perror(dup2(STDOUT_FILENO, STDIN_FILENO));
        }
    } else {
        fclose(file);
    }
    ec.ec_path_stack.pop_back();

    return Ok(retval);
}

Result<std::string, lnav::console::user_message>
execute_file(exec_context& ec, const std::string& path_and_args, bool multiline)
{
    available_scripts scripts;
    std::vector<std::string> split_args;
    std::string retval, msg;
    shlex lexer(path_and_args);

    log_info("Executing file: %s", path_and_args.c_str());

    if (!lexer.split(split_args, ec.ec_local_vars.top())) {
        return ec.make_error("unable to parse path");
    }
    if (split_args.empty()) {
        return ec.make_error("no script specified");
    }

    ec.ec_local_vars.push({});

    auto script_name = split_args[0];
    auto& vars = ec.ec_local_vars.top();
    char env_arg_name[32];
    std::string star, open_error = "file not found";

    add_ansi_vars(vars);

    snprintf(
        env_arg_name, sizeof(env_arg_name), "%d", (int) split_args.size() - 1);

    vars["#"] = env_arg_name;
    for (size_t lpc = 0; lpc < split_args.size(); lpc++) {
        snprintf(env_arg_name, sizeof(env_arg_name), "%lu", lpc);
        vars[env_arg_name] = split_args[lpc];
    }
    for (size_t lpc = 1; lpc < split_args.size(); lpc++) {
        if (lpc > 1) {
            star.append(" ");
        }
        star.append(split_args[lpc]);
    }
    vars["__all__"] = star;

    std::vector<script_metadata> paths_to_exec;

    find_format_scripts(lnav_data.ld_config_paths, scripts);
    auto iter = scripts.as_scripts.find(script_name);
    if (iter != scripts.as_scripts.end()) {
        paths_to_exec = iter->second;
    }
    if (script_name == "-" || script_name == "/dev/stdin") {
        paths_to_exec.push_back({script_name, "", "", ""});
    } else if (access(script_name.c_str(), R_OK) == 0) {
        struct script_metadata meta;

        meta.sm_path = script_name;
        extract_metadata_from_file(meta);
        paths_to_exec.push_back(meta);
    } else if (errno != ENOENT) {
        open_error = strerror(errno);
    } else {
        auto script_path = ghc::filesystem::path(script_name);

        if (!script_path.is_absolute()) {
            script_path = ec.ec_path_stack.back() / script_path;
        }

        if (ghc::filesystem::is_regular_file(script_path)) {
            struct script_metadata meta;

            meta.sm_path = script_path;
            extract_metadata_from_file(meta);
            paths_to_exec.push_back(meta);
        } else if (errno != ENOENT) {
            open_error = strerror(errno);
        }
    }

    if (!paths_to_exec.empty()) {
        for (auto& path_iter : paths_to_exec) {
            retval
                = TRY(execute_file_contents(ec, path_iter.sm_path, multiline));
        }
    }
    ec.ec_local_vars.pop();

    if (paths_to_exec.empty()) {
        return ec.make_error(
            "unknown script -- {} -- {}", script_name, open_error);
    }

    return Ok(retval);
}

Result<std::string, lnav::console::user_message>
execute_from_file(exec_context& ec,
                  const ghc::filesystem::path& path,
                  int line_number,
                  const std::string& cmdline)
{
    std::string retval, alt_msg;
    auto _sg = ec.enter_source(
        intern_string::lookup(path.string()), line_number, cmdline);

    switch (cmdline[0]) {
        case ':':
            retval = TRY(execute_command(ec, cmdline.substr(1)));
            break;
        case '/':
            execute_search(cmdline.substr(1));
            break;
        case ';':
            setup_logline_table(ec);
            retval = TRY(execute_sql(ec, cmdline.substr(1), alt_msg));
            break;
        case '|':
            retval = TRY(execute_file(ec, cmdline.substr(1)));
            break;
        default:
            retval = TRY(execute_command(ec, cmdline));
            break;
    }

    log_info("%s:%d:execute result -- %s",
             path.c_str(),
             line_number,
             retval.c_str());

    return Ok(retval);
}

Result<std::string, lnav::console::user_message>
execute_any(exec_context& ec, const std::string& cmdline_with_mode)
{
    std::string retval, alt_msg, cmdline = cmdline_with_mode.substr(1);
    auto _cleanup = finally([&ec] {
        if (ec.is_read_write() &&
            // only rebuild in a script or non-interactive mode so we don't
            // block the UI.
            (lnav_data.ld_flags & LNF_HEADLESS || ec.ec_path_stack.size() > 1))
        {
            rescan_files();
            rebuild_indexes_repeatedly();
        }
    });

    switch (cmdline_with_mode[0]) {
        case ':':
            retval = TRY(execute_command(ec, cmdline));
            break;
        case '/':
            execute_search(cmdline);
            break;
        case ';':
            setup_logline_table(ec);
            retval = TRY(execute_sql(ec, cmdline, alt_msg));
            break;
        case '|': {
            retval = TRY(execute_file(ec, cmdline));
            break;
        }
        default:
            retval = TRY(execute_command(ec, cmdline));
            break;
    }

    return Ok(retval);
}

void
execute_init_commands(
    exec_context& ec,
    std::vector<std::pair<Result<std::string, lnav::console::user_message>,
                          std::string>>& msgs)
{
    if (lnav_data.ld_cmd_init_done) {
        return;
    }

    nonstd::optional<exec_context::output_t> ec_out;
    auto_fd fd_copy;

    if (!(lnav_data.ld_flags & LNF_HEADLESS)) {
        auto_mem<FILE> tmpout(fclose);

        tmpout = std::tmpfile();
        if (!tmpout) {
            msgs.emplace_back(Err(lnav::console::user_message::error(
                                      "Unable to open temporary output file")
                                      .with_errno_reason()),
                              "");
            return;
        }
        fd_copy = auto_fd::dup_of(fileno(tmpout));
        ec_out = std::make_pair(tmpout.release(), fclose);
    }

    auto& dls = lnav_data.ld_db_row_source;
    int option_index = 1;

    {
        log_info("Executing initial commands");
        exec_context::output_guard og(ec, "tmp", ec_out);

        for (auto& cmd : lnav_data.ld_commands) {
            static const auto COMMAND_OPTION_SRC
                = intern_string::lookup("command-option");

            std::string alt_msg;

            wait_for_children();

            log_debug("init cmd: %s", cmd.c_str());
            {
                auto _sg
                    = ec.enter_source(COMMAND_OPTION_SRC, option_index++, cmd);
                switch (cmd.at(0)) {
                    case ':':
                        msgs.emplace_back(execute_command(ec, cmd.substr(1)),
                                          alt_msg);
                        break;
                    case '/':
                        execute_search(cmd.substr(1));
                        break;
                    case ';':
                        setup_logline_table(ec);
                        msgs.emplace_back(
                            execute_sql(ec, cmd.substr(1), alt_msg), alt_msg);
                        break;
                    case '|':
                        msgs.emplace_back(execute_file(ec, cmd.substr(1)),
                                          alt_msg);
                        break;
                }

                rescan_files();
                rebuild_indexes_repeatedly();
            }
            if (dls.dls_rows.size() > 1) {
                ensure_view(LNV_DB);
            }
        }
    }
    lnav_data.ld_commands.clear();

    struct stat st;

    if (ec_out && fstat(fd_copy, &st) != -1 && st.st_size > 0) {
        static const auto OUTPUT_NAME = std::string("Initial command output");

        lnav_data.ld_active_files.fc_file_names[OUTPUT_NAME]
            .with_fd(std::move(fd_copy))
            .with_include_in_session(false)
            .with_detect_format(false);
        lnav_data.ld_files_to_front.emplace_back(OUTPUT_NAME, 0_vl);

        if (lnav_data.ld_rl_view != nullptr) {
            lnav_data.ld_rl_view->set_alt_value(
                HELP_MSG_1(X, "to close the file"));
        }
    }

    lnav_data.ld_cmd_init_done = true;
}

int
sql_callback(exec_context& ec, sqlite3_stmt* stmt)
{
    auto& dls = lnav_data.ld_db_row_source;

    if (!sqlite3_stmt_busy(stmt)) {
        dls.clear();

        return 0;
    }

    auto& chart = dls.dls_chart;
    auto& vc = view_colors::singleton();
    int ncols = sqlite3_column_count(stmt);
    int row_number;
    int lpc, retval = 0;
    auto set_vars = false;

    row_number = dls.dls_rows.size();
    dls.dls_rows.resize(row_number + 1);
    if (dls.dls_headers.empty()) {
        for (lpc = 0; lpc < ncols; lpc++) {
            int type = sqlite3_column_type(stmt, lpc);
            std::string colname = sqlite3_column_name(stmt, lpc);
            bool graphable;

            graphable = ((type == SQLITE_INTEGER || type == SQLITE_FLOAT)
                         && !binary_search(lnav_data.ld_db_key_names.begin(),
                                           lnav_data.ld_db_key_names.end(),
                                           colname));

            dls.push_header(colname, type, graphable);
            if (graphable) {
                auto name_for_ident_attrs = colname;
                auto attrs = vc.attrs_for_ident(name_for_ident_attrs);
                for (size_t attempt = 0;
                     chart.attrs_in_use(attrs) && attempt < 3;
                     attempt++)
                {
                    name_for_ident_attrs += " ";
                    attrs = vc.attrs_for_ident(name_for_ident_attrs);
                }
                chart.with_attrs_for_ident(colname, attrs);
                dls.dls_headers.back().hm_title_attrs = attrs;
            }
        }
        set_vars = true;
    }
    for (lpc = 0; lpc < ncols; lpc++) {
        auto* raw_value = sqlite3_column_value(stmt, lpc);
        auto value_type = sqlite3_value_type(raw_value);
        scoped_value_t value;
        auto& hm = dls.dls_headers[lpc];

        switch (value_type) {
            case SQLITE_INTEGER:
                value = (int64_t) sqlite3_value_int64(raw_value);
                break;
            case SQLITE_FLOAT:
                value = sqlite3_value_double(raw_value);
                break;
            case SQLITE_NULL:
                value = null_value_t{};
                break;
            default:
                value = string_fragment::from_bytes(
                    sqlite3_value_text(raw_value),
                    sqlite3_value_bytes(raw_value));
                break;
        }
        dls.push_column(value);
        if ((hm.hm_column_type == SQLITE_TEXT
             || hm.hm_column_type == SQLITE_NULL)
            && hm.hm_sub_type == 0)
        {
            switch (value_type) {
                case SQLITE_TEXT:
                    hm.hm_column_type = SQLITE_TEXT;
                    hm.hm_sub_type = sqlite3_value_subtype(raw_value);
                    break;
            }
        }
        if (set_vars && !ec.ec_local_vars.empty() && !ec.ec_dry_run) {
            if (sql_ident_needs_quote(hm.hm_name.c_str())) {
                continue;
            }
            auto& vars = ec.ec_local_vars.top();

            if (value.is<string_fragment>()) {
                value = value.get<string_fragment>().to_string();
            }
            vars[hm.hm_name] = value;
        }
    }

    return retval;
}

std::future<std::string>
pipe_callback(exec_context& ec, const std::string& cmdline, auto_fd& fd)
{
    auto out = ec.get_output();

    if (out) {
        FILE* file = *out;

        return std::async(std::launch::async, [&fd, file]() {
            char buffer[1024];
            ssize_t rc;

            if (file == stdout) {
                lnav_data.ld_stdout_used = true;
            }

            while ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
                fwrite(buffer, rc, 1, file);
            }

            return std::string();
        });
    }
    auto tmp_fd
        = lnav::filesystem::open_temp_file(
              ghc::filesystem::temp_directory_path() / "lnav.out.XXXXXX")
              .map([](auto pair) {
                  ghc::filesystem::remove(pair.first);

                  return std::move(pair.second);
              })
              .expect("Cannot create temporary file for callback");
    auto pp
        = std::make_shared<piper_proc>(std::move(fd), false, std::move(tmp_fd));
    static int exec_count = 0;

    lnav_data.ld_pipers.push_back(pp);
    auto desc
        = fmt::format(FMT_STRING("[{}] Output of {}"), exec_count++, cmdline);
    lnav_data.ld_active_files.fc_file_names[desc]
        .with_fd(pp->get_fd())
        .with_include_in_session(false)
        .with_detect_format(false);
    lnav_data.ld_files_to_front.emplace_back(desc, 0_vl);
    if (lnav_data.ld_rl_view != nullptr) {
        lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(X, "to close the file"));
    }

    return lnav::futures::make_ready_future(std::string());
}

void
add_global_vars(exec_context& ec)
{
    for (const auto& iter : lnav_config.lc_global_vars) {
        shlex subber(iter.second);
        std::string str;

        if (!subber.eval(str, ec.ec_global_vars)) {
            log_error("Unable to evaluate global variable value: %s",
                      iter.second.c_str());
            continue;
        }

        ec.ec_global_vars[iter.first] = str;
    }
}

void
exec_context::set_output(const std::string& name,
                         FILE* file,
                         int (*closer)(FILE*))
{
    log_info("redirecting command output to: %s", name.c_str());
    this->ec_output_stack.back().second | [](auto out) {
        if (out.second != nullptr) {
            out.second(out.first);
        }
    };
    this->ec_output_stack.back()
        = std::make_pair(name, std::make_pair(file, closer));
}

void
exec_context::clear_output()
{
    log_info("redirecting command output to screen");
    this->ec_output_stack.back().second | [](auto out) {
        if (out.second != nullptr) {
            out.second(out.first);
        }
    };
    this->ec_output_stack.back() = std::make_pair("default", nonstd::nullopt);
}

exec_context::exec_context(logline_value_vector* line_values,
                           sql_callback_t sql_callback,
                           pipe_callback_t pipe_callback)
    : ec_line_values(line_values),
      ec_accumulator(std::make_unique<attr_line_t>()),
      ec_sql_callback(sql_callback), ec_pipe_callback(pipe_callback)
{
    static const auto COMMAND_SRC = intern_string::lookup("command");

    this->ec_local_vars.push(std::map<std::string, scoped_value_t>());
    this->ec_path_stack.emplace_back(".");
    this->ec_source.emplace_back(
        lnav::console::snippet::from(COMMAND_SRC, "").with_line(1));
    this->ec_output_stack.emplace_back("screen", nonstd::nullopt);
    this->ec_error_callback_stack.emplace_back(
        [](const auto& um) { lnav::console::print(stderr, um); });
}

void
exec_context::execute(const std::string& cmdline)
{
    auto exec_res = execute_any(*this, cmdline);
    if (exec_res.isErr()) {
        this->ec_error_callback_stack.back()(exec_res.unwrapErr());
    }
}

void
exec_context::add_error_context(lnav::console::user_message& um)
{
    switch (um.um_level) {
        case lnav::console::user_message::level::raw:
        case lnav::console::user_message::level::info:
        case lnav::console::user_message::level::ok:
            return;
        default:
            break;
    }

    if (um.um_snippets.empty()) {
        um.with_snippets(this->ec_source);
    }

    if (this->ec_current_help != nullptr && um.um_help.empty()) {
        attr_line_t help;

        format_help_text_for_term(*this->ec_current_help,
                                  70,
                                  help,
                                  help_text_content::synopsis_and_summary);
        um.with_help(help);
    }
}

exec_context::source_guard
exec_context::enter_source(intern_string_t path,
                           int line_number,
                           const std::string& content)
{
    attr_line_t content_al{content};
    content_al.with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE));
    readline_lnav_highlighter(content_al, -1);
    this->ec_source.emplace_back(
        lnav::console::snippet::from(path, content_al).with_line(line_number));
    return {this};
}

exec_context::output_guard::output_guard(exec_context& context,
                                         std::string name,
                                         const nonstd::optional<output_t>& file)
    : sg_context(context)
{
    if (file) {
        log_info("redirecting command output to: %s", name.c_str());
    }
    context.ec_output_stack.emplace_back(std::move(name), file);
}

exec_context::output_guard::~output_guard()
{
    this->sg_context.clear_output();
    this->sg_context.ec_output_stack.pop_back();
}
