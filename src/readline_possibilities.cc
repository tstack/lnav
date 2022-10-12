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

#include <regex>
#include <string>

#include "readline_possibilities.hh"

#include "base/isc.hh"
#include "base/opt_util.hh"
#include "config.h"
#include "data_parser.hh"
#include "lnav.hh"
#include "lnav_config.hh"
#include "service_tags.hh"
#include "session_data.hh"
#include "sql_help.hh"
#include "sql_util.hh"
#include "sqlite-extension-func.hh"
#include "sysclip.hh"
#include "tailer/tailer.looper.hh"
#include "yajlpp/yajlpp_def.hh"

static int
handle_collation_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    if (lnav_data.ld_rl_view != nullptr) {
        lnav_data.ld_rl_view->add_possibility(
            ln_mode_t::SQL, "*", colvalues[1]);
    }

    return 0;
}

static int
handle_db_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    if (lnav_data.ld_rl_view != nullptr) {
        lnav_data.ld_rl_view->add_possibility(
            ln_mode_t::SQL, "*", colvalues[1]);
    }

    return 0;
}

static int
handle_table_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    if (lnav_data.ld_rl_view != nullptr) {
        std::string table_name = colvalues[0];

        if (sqlite_function_help.count(table_name) == 0) {
            lnav_data.ld_rl_view->add_possibility(
                ln_mode_t::SQL, "*", colvalues[0]);
        }

        lnav_data.ld_table_ddl[colvalues[0]] = colvalues[1];
    }

    return 0;
}

static int
handle_table_info(void* ptr, int ncols, char** colvalues, char** colnames)
{
    if (lnav_data.ld_rl_view != nullptr) {
        auto_mem<char, sqlite3_free> quoted_name;

        quoted_name = sql_quote_ident(colvalues[1]);
        lnav_data.ld_rl_view->add_possibility(
            ln_mode_t::SQL, "*", std::string(quoted_name));
    }
    if (strcmp(colvalues[5], "1") == 0) {
        lnav_data.ld_db_key_names.emplace_back(colvalues[1]);
    }
    return 0;
}

static int
handle_foreign_key_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    lnav_data.ld_db_key_names.emplace_back(colvalues[3]);
    lnav_data.ld_db_key_names.emplace_back(colvalues[4]);
    return 0;
}

struct sqlite_metadata_callbacks lnav_sql_meta_callbacks = {
    handle_collation_list,
    handle_db_list,
    handle_table_list,
    handle_table_info,
    handle_foreign_key_list,
};

static void
add_text_possibilities(readline_curses* rlc,
                       int context,
                       const std::string& type,
                       const std::string& str,
                       text_quoting tq)
{
    static const std::regex re_escape(R"(([.\^$*+?()\[\]{}\\|]))");
    static const std::regex re_escape_no_dot(R"(([\^$*+?()\[\]{}\\|]))");

    data_scanner ds(str);

    while (true) {
        auto tok_res = ds.tokenize2();

        if (!tok_res) {
            break;
        }
        if (tok_res->tr_capture.length() < 4) {
            continue;
        }

        switch (tok_res->tr_token) {
            case DT_DATE:
            case DT_TIME:
            case DT_WHITE:
                continue;
            default:
                break;
        }

        switch (tq) {
            case text_quoting::sql: {
                auto token_value = tok_res->to_string();
                auto_mem<char, sqlite3_free> quoted_token;

                quoted_token = sqlite3_mprintf("%Q", token_value.c_str());
                rlc->add_possibility(context, type, std::string(quoted_token));
                break;
            }
            default: {
                auto token_value_no_dot = tok_res->to_string();
                auto token_value = std::regex_replace(
                    token_value_no_dot, re_escape, R"(\\\1)");
                token_value_no_dot = std::regex_replace(
                    token_value_no_dot, re_escape_no_dot, R"(\\\1)");
                rlc->add_possibility(context, type, token_value);
                if (token_value != token_value_no_dot) {
                    rlc->add_possibility(context, type, token_value_no_dot);
                }
                break;
            }
        }

        switch (tok_res->tr_token) {
            case DT_QUOTED_STRING:
                add_text_possibilities(
                    rlc,
                    context,
                    type,
                    ds.to_string_fragment(tok_res->tr_inner_capture)
                        .to_string(),
                    tq);
                break;
            default:
                break;
        }
    }
}

void
add_view_text_possibilities(readline_curses* rlc,
                            int context,
                            const std::string& type,
                            textview_curses* tc,
                            text_quoting tq)
{
    text_sub_source* tss = tc->get_sub_source();

    rlc->clear_possibilities(context, type);

    if (tc->get_inner_height() > 0_vl) {
        for (vis_line_t curr_line = tc->get_top();
             curr_line <= tc->get_bottom();
             ++curr_line)
        {
            std::string line;

            tss->text_value_for_line(
                *tc, curr_line, line, text_sub_source::RF_RAW);

            add_text_possibilities(rlc, context, type, line, tq);
        }
    }

    rlc->add_possibility(context, type, bookmark_metadata::KNOWN_TAGS);
}

void
add_filter_expr_possibilities(readline_curses* rlc,
                              int context,
                              const std::string& type)
{
    static const char* BUILTIN_VARS[] = {
        ":log_level",
        ":log_time",
        ":log_time_msecs",
        ":log_mark",
        ":log_comment",
        ":log_tags",
        ":log_opid",
        ":log_format",
        ":log_path",
        ":log_unique_path",
        ":log_text",
        ":log_body",
        ":log_raw_text",
    };

    textview_curses* tc = *lnav_data.ld_view_stack.top();
    auto& lss = lnav_data.ld_log_source;
    auto bottom = tc->get_bottom();

    rlc->clear_possibilities(context, type);
    rlc->add_possibility(
        context, type, std::begin(BUILTIN_VARS), std::end(BUILTIN_VARS));
    for (auto curr_line = tc->get_top(); curr_line < bottom; ++curr_line) {
        auto cl = lss.at(curr_line);
        auto lf = lss.find(cl);
        auto ll = lf->begin() + cl;

        if (!ll->is_message()) {
            continue;
        }

        auto format = lf->get_format();
        string_attrs_t sa;
        logline_value_vector values;

        lf->read_full_message(ll, values.lvv_sbr);
        values.lvv_sbr.erase_ansi();
        format->annotate(cl, sa, values);
        for (auto& lv : values.lvv_values) {
            if (!lv.lv_meta.lvm_struct_name.empty()) {
                continue;
            }

            auto_mem<char> ident(sqlite3_free);

            ident = sql_quote_ident(lv.lv_meta.lvm_name.get());
            auto bound_name = fmt::format(FMT_STRING(":{}"), ident);
            rlc->add_possibility(context, type, bound_name);
            switch (lv.lv_meta.lvm_kind) {
                case value_kind_t::VALUE_BOOLEAN:
                case value_kind_t::VALUE_FLOAT:
                case value_kind_t::VALUE_NULL:
                    break;
                case value_kind_t::VALUE_INTEGER:
                    rlc->add_possibility(
                        context, type, std::to_string(lv.lv_value.i));
                    break;
                default: {
                    auto_mem<char, sqlite3_free> str;

                    str = sqlite3_mprintf(
                        "%.*Q", lv.text_length(), lv.text_value());
                    rlc->add_possibility(context, type, std::string(str.in()));
                    break;
                }
            }
        }
    }
    rlc->add_possibility(
        context, type, std::begin(sql_keywords), std::end(sql_keywords));
    rlc->add_possibility(context, type, sql_function_names);
    for (int lpc = 0; sqlite_registration_funcs[lpc]; lpc++) {
        struct FuncDef* basic_funcs;
        struct FuncDefAgg* agg_funcs;

        sqlite_registration_funcs[lpc](&basic_funcs, &agg_funcs);
        for (int lpc2 = 0; basic_funcs && basic_funcs[lpc2].zName; lpc2++) {
            const FuncDef& func_def = basic_funcs[lpc2];

            rlc->add_possibility(
                context,
                type,
                std::string(func_def.zName) + (func_def.nArg ? "(" : "()"));
        }
        for (int lpc2 = 0; agg_funcs && agg_funcs[lpc2].zName; lpc2++) {
            const FuncDefAgg& func_def = agg_funcs[lpc2];

            rlc->add_possibility(
                context,
                type,
                std::string(func_def.zName) + (func_def.nArg ? "(" : "()"));
        }
    }
}

void
add_env_possibilities(ln_mode_t context)
{
    extern char** environ;
    readline_curses* rlc = lnav_data.ld_rl_view;

    for (char** var = environ; *var != nullptr; var++) {
        rlc->add_possibility(
            context, "*", "$" + std::string(*var, strchr(*var, '=')));
    }

    exec_context& ec = lnav_data.ld_exec_context;

    if (!ec.ec_local_vars.empty()) {
        for (const auto& iter : ec.ec_local_vars.top()) {
            rlc->add_possibility(context, "*", "$" + iter.first);
        }
    }

    for (const auto& iter : ec.ec_global_vars) {
        rlc->add_possibility(context, "*", "$" + iter.first);
    }

    if (lnav_data.ld_window) {
        rlc->add_possibility(context, "*", "$LINES");
        rlc->add_possibility(context, "*", "$COLS");
    }
}

void
add_filter_possibilities(textview_curses* tc)
{
    readline_curses* rc = lnav_data.ld_rl_view;
    text_sub_source* tss = tc->get_sub_source();
    filter_stack& fs = tss->get_filters();

    rc->clear_possibilities(ln_mode_t::COMMAND, "all-filters");
    rc->clear_possibilities(ln_mode_t::COMMAND, "disabled-filter");
    rc->clear_possibilities(ln_mode_t::COMMAND, "enabled-filter");
    for (const auto& tf : fs) {
        rc->add_possibility(ln_mode_t::COMMAND, "all-filters", tf->get_id());
        if (tf->is_enabled()) {
            rc->add_possibility(
                ln_mode_t::COMMAND, "enabled-filter", tf->get_id());
        } else {
            rc->add_possibility(
                ln_mode_t::COMMAND, "disabled-filter", tf->get_id());
        }
    }
}

void
add_file_possibilities()
{
    static const std::regex sh_escape(R"(([\s\'\"]+))");

    readline_curses* rc = lnav_data.ld_rl_view;

    rc->clear_possibilities(ln_mode_t::COMMAND, "visible-files");
    rc->clear_possibilities(ln_mode_t::COMMAND, "hidden-files");
    for (const auto& lf : lnav_data.ld_active_files.fc_files) {
        if (lf.get() == nullptr) {
            continue;
        }

        lnav_data.ld_log_source.find_data(lf) | [&lf, rc](auto ld) {
            auto escaped_fn
                = std::regex_replace(lf->get_filename(), sh_escape, R"(\\\1)");

            rc->add_possibility(
                ln_mode_t::COMMAND,
                ld->is_visible() ? "visible-files" : "hidden-files",
                escaped_fn);
        };
    }
}

void
add_mark_possibilities()
{
    readline_curses* rc = lnav_data.ld_rl_view;

    rc->clear_possibilities(ln_mode_t::COMMAND, "mark-type");
    for (auto iter = bookmark_type_t::type_begin();
         iter != bookmark_type_t::type_end();
         ++iter)
    {
        bookmark_type_t* bt = (*iter);

        if (bt->get_name().empty()) {
            continue;
        }
        rc->add_possibility(ln_mode_t::COMMAND, "mark-type", bt->get_name());
    }
}

void
add_config_possibilities()
{
    readline_curses* rc = lnav_data.ld_rl_view;
    std::set<std::string> visited;
    auto cb = [rc, &visited](const json_path_handler_base& jph,
                             const std::string& path,
                             void* mem) {
        if (jph.jph_children) {
            const auto named_caps = jph.jph_regex->get_named_captures();

            if (named_caps.empty()) {
                rc->add_possibility(ln_mode_t::COMMAND, "config-option", path);
            }
            for (const auto& named_cap : named_caps) {
                if (visited.count(named_cap.get_name().to_string()) == 0) {
                    rc->clear_possibilities(ln_mode_t::COMMAND,
                                            named_cap.get_name().to_string());
                    visited.insert(named_cap.get_name().to_string());
                }

                ghc::filesystem::path path_obj(path);
                rc->add_possibility(ln_mode_t::COMMAND,
                                    named_cap.get_name().to_string(),
                                    path_obj.parent_path().filename().string());
            }
        } else {
            rc->add_possibility(ln_mode_t::COMMAND, "config-option", path);
            if (jph.jph_synopsis) {
                rc->add_prefix(ln_mode_t::COMMAND,
                               std::vector<std::string>{"config", path},
                               jph.jph_synopsis);
            }
        }
    };

    rc->clear_possibilities(ln_mode_t::COMMAND, "config-option");
    for (const auto& jph : lnav_config_handlers.jpc_children) {
        jph.walk(cb, &lnav_config);
    }
}

void
add_tag_possibilities()
{
    readline_curses* rc = lnav_data.ld_rl_view;

    rc->clear_possibilities(ln_mode_t::COMMAND, "tag");
    rc->clear_possibilities(ln_mode_t::COMMAND, "line-tags");
    rc->add_possibility(
        ln_mode_t::COMMAND, "tag", bookmark_metadata::KNOWN_TAGS);
    if (lnav_data.ld_view_stack.top().value_or(nullptr)
        == &lnav_data.ld_views[LNV_LOG])
    {
        logfile_sub_source& lss = lnav_data.ld_log_source;
        if (lss.text_line_count() > 0) {
            auto line_meta_opt = lss.find_bookmark_metadata(
                lnav_data.ld_views[LNV_LOG].get_top());
            if (line_meta_opt) {
                rc->add_possibility(ln_mode_t::COMMAND,
                                    "line-tags",
                                    line_meta_opt.value()->bm_tags);
            }
        }
    }
}

void
add_recent_netlocs_possibilities()
{
    readline_curses* rc = lnav_data.ld_rl_view;

    rc->clear_possibilities(ln_mode_t::COMMAND, "recent-netlocs");
    std::set<std::string> netlocs;

    isc::to<tailer::looper&, services::remote_tailer_t>().send_and_wait(
        [&netlocs](auto& tlooper) { netlocs = tlooper.active_netlocs(); });
    netlocs.insert(session_data.sd_recent_netlocs.begin(),
                   session_data.sd_recent_netlocs.end());
    rc->add_possibility(ln_mode_t::COMMAND, "recent-netlocs", netlocs);
}
