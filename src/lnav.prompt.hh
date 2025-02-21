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

#ifndef lnav_prompt_hh
#define lnav_prompt_hh

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/attr_line.hh"
#include "base/string_attr_type.hh"
#include "format.scripts.hh"
#include "help_text.hh"
#include "mapbox/variant.hpp"
#include "textinput.history.hh"
#include "textinput_curses.hh"

namespace lnav {

struct prompt {
    static const string_attr_type<std::string> SUBST_TEXT;

    static prompt& get();

    struct sql_keyword_t {};
    struct sql_db_t {};
    struct sql_table_t {};
    struct sql_table_valued_function_t {};
    struct sql_function_t {};
    struct sql_column_t {};
    struct sql_number_t {};
    struct sql_string_t {};
    struct sql_collation_t {};
    struct sql_var_t {};

    using sql_item_t = mapbox::util::variant<sql_keyword_t,
                                             sql_collation_t,
                                             sql_db_t,
                                             sql_table_t,
                                             sql_table_valued_function_t,
                                             sql_function_t,
                                             sql_column_t,
                                             sql_number_t,
                                             sql_string_t,
                                             sql_var_t>;

    struct sql_item_meta {
        const char* sim_type_hint;
        const char* sim_display_suffix;
        const char* sim_replace_suffix;
        role_t sim_role;
    };

    lnav::textinput::history p_sql_history;
    lnav::textinput::history p_cmd_history;
    lnav::textinput::history p_search_history;
    lnav::textinput::history p_script_history;

    lnav::textinput::history& get_history_for(char sigil)
    {
        switch (sigil) {
            case ':':
                return this->p_cmd_history;
            case ';':
                return this->p_sql_history;
            case '/':
                return this->p_search_history;
            case '|':
                return this->p_script_history;
            default:
                ensure(false);
        }
    }

    std::map<std::string, std::string> p_env_vars;
    std::multimap<std::string, sql_item_t> p_sql_completions;
    std::map<std::string, sql_item_t> p_prql_completions;
    std::map<std::string, const json_path_handler_base*> p_config_paths;
    std::map<std::string, std::vector<std::string>> p_config_values;
    available_scripts p_scripts;
    textinput_curses p_editor;

    void focus_for(char sigil, const std::vector<std::string>& args);

    void refresh_sql_completions(textview_curses& tc);
    void insert_sql_completion(const std::string& name, const sql_item_t& item);
    const sql_item_meta& sql_item_hint(const sql_item_t& item) const;
    attr_line_t get_db_completion_text(const std::string& str, int width) const;
    attr_line_t get_sql_completion_text(
        const std::pair<std::string, sql_item_t>& p) const;

    void refresh_config_completions();
    std::vector<attr_line_t> get_cmd_parameter_completion(
        textview_curses& tc, const help_text* ht, const std::string& str);
    std::vector<attr_line_t> get_env_completion(const std::string& str);
    std::vector<attr_line_t> get_config_value_completion(
        const std::string& path, const std::string& str) const;

    void rl_help(textinput_curses& tc);
    void rl_reformat(textinput_curses& tc);
    void rl_history(textinput_curses& tc);
    void rl_completion(textinput_curses& tc);
};

}  // namespace lnav

#endif
