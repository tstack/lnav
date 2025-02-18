/**
 * Copyright (c) 2025, Timothy Stack
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

#include <string>

#include "lnav.prompt.hh"

#include <glob.h>

#include "base/fs_util.hh"
#include "base/itertools.hh"
#include "base/string_attr_type.hh"
#include "itertools.similar.hh"
#include "lnav.hh"
#include "log_format_ext.hh"
#include "readline_highlighters.hh"
#include "readline_possibilities.hh"
#include "scn/scan.h"
#include "shlex.hh"
#include "sql_help.hh"
#include "sql_util.hh"

extern char** environ;

namespace lnav {

constexpr string_attr_type<std::string> prompt::SUBST_TEXT("subst-text");

static int
handle_collation_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    auto* smc = static_cast<sqlite_metadata_callbacks*>(ptr);
    auto* lp = static_cast<prompt*>(smc->smc_userdata);

    lp->insert_sql_completion(colvalues[1], prompt::sql_collation_t{});

    return 0;
}

static int
handle_db_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    auto* smc = static_cast<sqlite_metadata_callbacks*>(ptr);
    auto* lp = static_cast<prompt*>(smc->smc_userdata);

    lp->insert_sql_completion(colvalues[1], prompt::sql_db_t{});

    return 0;
}

static size_t
files_with_format(log_format* format)
{
    auto retval = size_t{0};
    for (const auto& lf : lnav_data.ld_active_files.fc_files) {
        if (lf->get_format_name() == format->get_name()) {
            retval += 1;
        }
    }

    return retval;
}

static int
handle_table_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    auto* smc = static_cast<sqlite_metadata_callbacks*>(ptr);
    auto* lp = static_cast<prompt*>(smc->smc_userdata);
    std::string table_name = colvalues[0];
    intern_string_t table_intern = intern_string::lookup(table_name);
    auto format = log_format::find_root_format(table_name.c_str());
    auto add_poss = true;

    if (format != nullptr) {
        if (files_with_format(format.get()) == 0) {
            add_poss = false;
        }
    } else if (sqlite_function_help.count(table_name) != 0) {
        add_poss = false;
    } else {
        for (const auto& lf : log_format::get_root_formats()) {
            auto* elf = dynamic_cast<external_log_format*>(lf.get());
            if (elf == nullptr) {
                continue;
            }

            if (elf->elf_search_tables.find(table_intern)
                    != elf->elf_search_tables.end()
                && files_with_format(lf.get()) == 0)
            {
                add_poss = false;
            }
        }
    }

    if (add_poss) {
        lp->insert_sql_completion(table_name, prompt::sql_table_t{});
    }

    lnav_data.ld_table_ddl[colvalues[0]] = colvalues[1];

    return 0;
}

static int
handle_table_info(void* ptr, int ncols, char** colvalues, char** colnames)
{
    auto* smc = static_cast<sqlite_metadata_callbacks*>(ptr);
    auto* lp = static_cast<prompt*>(smc->smc_userdata);

    auto quoted_name = sql_quote_ident(colvalues[1]);
    lp->insert_sql_completion(std::string(quoted_name), prompt::sql_column_t{});
    if (strcmp(colvalues[5], "1") == 0) {
        lnav_data.ld_db_key_names.emplace(colvalues[1]);
    }
    return 0;
}

static int
handle_foreign_key_list(void* ptr, int ncols, char** colvalues, char** colnames)
{
    lnav_data.ld_db_key_names.emplace(colvalues[3]);
    lnav_data.ld_db_key_names.emplace(colvalues[4]);
    return 0;
}

void
prompt::insert_sql_completion(const std::string& name, const sql_item_t& item)
{
    const auto range = this->p_sql_completions.equal_range(name);
    for (auto iter = range.first; iter != range.second; ++iter) {
        if (iter->first == name && iter->second.which() == item.which()) {
            return;
        }
    }
    this->p_sql_completions.emplace(name, item);
}

prompt&
prompt::get()
{
    static prompt retval = {
        textinput::history::for_context("sql"_frag),
        textinput::history::for_context("cmd"_frag),
        textinput::history::for_context("search"_frag),
        textinput::history::for_context("script"_frag),
    };

    return retval;
}

void
prompt::focus_for(char sigil)
{
    this->p_editor.tc_prefix.clear();
    if (sigil) {
        this->p_editor.tc_prefix.al_string.push_back(sigil);
    }
    this->p_editor.tc_height = 1;
    this->p_editor.set_content("");
    this->p_editor.focus();
}

void
prompt::refresh_sql_completions(textview_curses& tc)
{
    static auto& ec = injector::get<exec_context&>();
    static constexpr const char* hidden_table_columns[] = {
        "log_time_msecs",
        "log_path",
        "log_text",
        "log_body",
        "log_opid",
    };

    sqlite_metadata_callbacks lnav_sql_meta_callbacks = {
        handle_collation_list,
        handle_db_list,
        handle_table_list,
        handle_table_info,
        handle_foreign_key_list,
        this,
    };

    this->p_sql_completions.clear();
    for (const auto& [name, func] : sqlite_function_help) {
        switch (func->ht_context) {
            case help_context_t::HC_SQL_KEYWORD:
            case help_context_t::HC_SQL_INFIX:
                this->insert_sql_completion(name, sql_keyword_t{});
                break;
            case help_context_t::HC_SQL_FUNCTION:
                this->insert_sql_completion(name, sql_function_t{});
                break;
            case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION:
                this->insert_sql_completion(name,
                                            sql_table_valued_function_t{});
                break;
            default:
                break;
        }
    }
    for (const auto& col : hidden_table_columns) {
        this->insert_sql_completion(col, sql_column_t{});
    }
    for (char** var = environ; *var != nullptr; var++) {
        auto pair_sf = string_fragment::from_c_str(*var);
        auto name_sf = pair_sf.split_when(string_fragment::tag1{'='}).first;
        auto var_name = fmt::format(FMT_STRING("${}"), name_sf);

        this->insert_sql_completion(var_name, sql_var_t{});
    }
    this->insert_sql_completion("$LINES", sql_var_t{});
    this->insert_sql_completion("$COLS", sql_var_t{});
    for (const auto& [name, value] : ec.ec_global_vars) {
        this->insert_sql_completion(fmt::format(FMT_STRING("${}"), name),
                                    sql_var_t{});
    }

    walk_sqlite_metadata(lnav_data.ld_db.in(), lnav_sql_meta_callbacks);

    for (const auto& str : view_text_possibilities(tc)) {
        auto scan_res = scn::scan_value<double>(str);
        if (scan_res && scan_res->range().empty()) {
            this->insert_sql_completion(str, sql_number_t{});
        } else {
            this->insert_sql_completion(sql_quote_text(str), sql_string_t{});
        }
    }
}

void
prompt::rl_history(textinput_curses& tc)
{
    auto sigil = tc.tc_prefix.al_string.front();
    auto& hist = this->get_history_for(sigil);
    std::vector<attr_line_t> poss;
    auto cb = [&poss, sigil](const auto& e) {
        auto al = attr_line_t()
                      .append(e.e_content)
                      .with_attr_for_all(SUBST_TEXT.value(e.e_content));
        switch (sigil) {
            case ':':
                readline_command_highlighter(al, std::nullopt);
                break;
            case ';':
                readline_sqlite_highlighter(al, std::nullopt);
                break;
            case '/':
                readline_regex_highlighter(al, std::nullopt);
                break;
        }
        poss.emplace_back(al.move());
    };
    hist.query_entries(tc.get_content(), cb);
    if (poss.empty()) {
        hist.query_entries(""_frag, cb);
    }
    tc.open_popup_for_history(poss);
}

void
prompt::rl_completion(textinput_curses& tc)
{
    tc.tc_selection = tc.tc_complete_range;
    const auto& al
        = tc.tc_popup_source.get_lines()[tc.tc_popup.get_selection()].tl_value;
    auto sub = get_string_attr(al.al_attrs, SUBST_TEXT)->get();
    tc.replace_selection(sub);
}

struct sql_item_visitor {
    template<typename T>
    const prompt::sql_item_meta& operator()(const T&) const;
};

template<>
const prompt::sql_item_meta&
sql_item_visitor::operator()(const prompt::sql_keyword_t&) const
{
    static constexpr auto retval = prompt::sql_item_meta{
        " ",
        "",
        " ",
        role_t::VCR_KEYWORD,
    };

    return retval;
}

template<>
const prompt::sql_item_meta&
sql_item_visitor::operator()(const prompt::sql_collation_t&) const
{
    static constexpr auto retval = prompt::sql_item_meta{
        " ",
        "",
        " ",
        role_t::VCR_IDENTIFIER,
    };

    return retval;
}

template<>
const prompt::sql_item_meta&
sql_item_visitor::operator()(const prompt::sql_db_t&) const
{
    static constexpr auto retval = prompt::sql_item_meta{
        "\u26c1",
        "",
        ".",
        role_t::VCR_IDENTIFIER,
    };

    return retval;
}

template<>
const prompt::sql_item_meta&
sql_item_visitor::operator()(const prompt::sql_table_t&) const
{
    static constexpr auto retval = prompt::sql_item_meta{
        "\U0001f143",
        "",
        " ",
        role_t::VCR_IDENTIFIER,
    };

    return retval;
}

template<>
const prompt::sql_item_meta&
sql_item_visitor::operator()(const prompt::sql_table_valued_function_t&) const
{
    static constexpr auto retval = prompt::sql_item_meta{
        "\U0001D453",
        "()",
        "(",
        role_t::VCR_FUNCTION,
    };

    return retval;
}

template<>
const prompt::sql_item_meta&
sql_item_visitor::operator()(const prompt::sql_function_t&) const
{
    static constexpr auto retval = prompt::sql_item_meta{
        "\U0001D453",
        "()",
        "(",
        role_t::VCR_FUNCTION,
    };

    return retval;
}

template<>
const prompt::sql_item_meta&
sql_item_visitor::operator()(const prompt::sql_column_t&) const
{
    static constexpr auto retval = prompt::sql_item_meta{
        "\U0001F132",
        "",
        "",
        role_t::VCR_IDENTIFIER,
    };

    return retval;
}

template<>
const prompt::sql_item_meta&
sql_item_visitor::operator()(const prompt::sql_number_t&) const
{
    static constexpr auto retval = prompt::sql_item_meta{
        "\U0001F13D",
        "",
        " ",
        role_t::VCR_NUMBER,
    };

    return retval;
}

template<>
const prompt::sql_item_meta&
sql_item_visitor::operator()(const prompt::sql_string_t&) const
{
    static constexpr auto retval = prompt::sql_item_meta{
        "\U0001f142",
        "",
        " ",
        role_t::VCR_STRING,
    };

    return retval;
}

template<>
const prompt::sql_item_meta&
sql_item_visitor::operator()(const prompt::sql_var_t&) const
{
    static constexpr auto retval = prompt::sql_item_meta{
        "\U0001f145",
        "",
        " ",
        role_t::VCR_VARIABLE,
    };

    return retval;
}

const prompt::sql_item_meta&
prompt::sql_item_hint(const sql_item_t& item) const
{
    return item.match(sql_item_visitor{});
}

attr_line_t
prompt::get_sql_completion_text(const std::pair<std::string, sql_item_t>& p)
{
    auto item_meta = this->sql_item_hint(p.second);
    return attr_line_t()
        .append(item_meta.sim_type_hint)
        .append(" ")
        .append(p.first, VC_ROLE.value(item_meta.sim_role))
        .append(item_meta.sim_display_suffix)
        .with_attr_for_all(
            SUBST_TEXT.value(p.first + item_meta.sim_replace_suffix));
}

std::vector<attr_line_t>
prompt::get_cmd_parameter_completion(textview_curses& tc,
                                     const help_text* ht,
                                     const std::string& str)
{
    log_debug("cmd comp %s", str.c_str());
    if (ht->ht_enum_values.empty()) {
        switch (ht->ht_format) {
            case help_parameter_format_t::HPF_REGEX: {
                std::vector<attr_line_t> retval;
                auto poss_str = view_text_possibilities(tc)
                    | lnav::itertools::similar_to(str, 10);

                for (const auto& str : poss_str) {
                    retval.emplace_back(
                        attr_line_t().append(str).with_attr_for_all(
                            SUBST_TEXT.value(lnav::pcre2pp::quote(str))));
                }
                return retval;
            }
            case help_parameter_format_t::HPF_FILENAME: {
                auto str_as_path = std::filesystem::path{str};
                auto parent = str_as_path.parent_path();
                std::vector<std::string> poss_paths;
                std::error_code ec;

                if (parent.empty()) {
                    parent = ".";
                }
                for (const auto& entry :
                     std::filesystem::directory_iterator(parent, ec))
                {
                    auto path_str = entry.path().string();
                    if (entry.is_directory()) {
                        path_str.push_back('/');
                    }
                    poss_paths.emplace_back(path_str);
                }

                auto retval = poss_paths | lnav::itertools::similar_to(str, 10)
                    | lnav::itertools::map([](const auto& path_str) {
                                  auto escaped_path = shlex::escape(path_str);
                                  if (!endswith(path_str, "/")) {
                                      escaped_path.push_back(' ');
                                  }
                                  return attr_line_t()
                                      .append(path_str)
                                      .with_attr_for_all(
                                          SUBST_TEXT.value(escaped_path));
                              });
                if (ec) {
                    log_error("dir iter failed!");
                }

                return retval;
            }
            case help_parameter_format_t::HPF_LOADED_FILE: {
                std::vector<std::string> files;
                for (const auto& lf : lnav_data.ld_active_files.fc_files) {
                    if (lf == nullptr) {
                        continue;
                    }

                    auto escaped_fn = fmt::to_string(lf->get_filename());
                    files.emplace_back(escaped_fn);
                }

                return files | lnav::itertools::similar_to(str, 10)
                    | lnav::itertools::map([](const auto& x) {
                           return attr_line_t().append(x).with_attr_for_all(
                               SUBST_TEXT.value(x + " "));
                       });
            }
            case help_parameter_format_t::HPF_FORMAT_FIELD: {
                std::unordered_set<std::string> field_names;

                for (const auto& format : log_format::get_root_formats()) {
                    for (const auto& lvm : format->get_value_metadata()) {
                        field_names.emplace(lvm.lvm_name.to_string());
                    }
                }

                return field_names | lnav::itertools::similar_to(str, 10)
                    | lnav::itertools::map([](const auto& x) {
                           return attr_line_t().append(x).with_attr_for_all(
                               SUBST_TEXT.value(x + " "));
                       });
            }
            case help_parameter_format_t::HPF_TIME_FILTER_POINT: {
                static const auto symbolic_times = std::vector<std::string>{
                    "here",
                    "now",
                    "today",
                    "yesterday",
                };

                auto* tss = tc.get_sub_source();
                auto* ttt = dynamic_cast<text_time_translator*>(tss);
                if (ttt == nullptr || !tss->tss_supports_filtering) {
                    return {};
                }

                auto ri_opt = ttt->time_for_row(tc.get_selection());
                if (!ri_opt) {
                    return {};
                }
                auto ri = ri_opt.value();

                auto all_times = symbolic_times;
                all_times.emplace_back(
                    lnav::to_rfc3339_string(ri.ri_time, 'T'));

                return all_times | lnav::itertools::similar_to(str, 10)
                    | lnav::itertools::map([](const auto& x) {
                           return attr_line_t().append(x).with_attr_for_all(
                               SUBST_TEXT.value(x + " "));
                       });
            }
            case help_parameter_format_t::HPF_TIMEZONE: {
                std::vector<std::string> tz_strs;
                try {
                    for (const auto& tz : date::get_tzdb().zones) {
                        tz_strs.emplace_back(tz.name());
                    }
                } catch (const std::runtime_error& e) {
                    log_error("unable to get tzdb -- %s", e.what());
                }

                return tz_strs | lnav::itertools::similar_to(str, 10)
                    | lnav::itertools::map([](const auto& x) {
                           return attr_line_t().append(x).with_attr_for_all(
                               SUBST_TEXT.value(x + " "));
                       });
            }
        }
    } else {
        return ht->ht_enum_values | lnav::itertools::similar_to(str, 10)
            | lnav::itertools::map([](const auto& x) {
                   return attr_line_t(x).with_attr_for_all(
                       SUBST_TEXT.value(std::string(x) + " "));
               });
    }

    return {};
}

}  // namespace lnav
