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

#include "dump_internals.hh"

#include "base/injector.hh"
#include "bound_tags.hh"
#include "help_text_formatter.hh"
#include "lnav.events.hh"
#include "lnav.hh"
#include "lnav_config.hh"
#include "log_format_loader.hh"
#include "sql_help.hh"
#include "view_helpers.examples.hh"
#include "yajlpp/yajlpp.hh"

namespace lnav {

void
dump_internals(const char* internals_dir)
{
    static const auto* sql_cmd_map
        = injector::get<readline_context::command_map_t*, sql_cmd_map_tag>();

    for (const auto* handlers :
         std::initializer_list<const json_path_container*>{
             &lnav_config_handlers,
             &root_format_handler,
             &lnav::events::file::open::handlers,
             &lnav::events::file::format_detected::handlers,
             &lnav::events::log::msg_detected::handlers,
             &lnav::events::session::loaded::handlers,
         })
    {
        dump_schema_to(*handlers, internals_dir);
    }

    auto cmd_ref_path = std::filesystem::path(internals_dir) / "cmd-ref.rst";
    auto cmd_file = std::unique_ptr<FILE, decltype(&fclose)>(
        fopen(cmd_ref_path.c_str(), "w+"), fclose);

    if (cmd_file != nullptr) {
        std::set<readline_context::command_t*> unique_cmds;

        for (auto& cmd : lnav_commands) {
            if (unique_cmds.find(cmd.second) != unique_cmds.end()) {
                continue;
            }
            unique_cmds.insert(cmd.second);
            format_help_text_for_rst(
                cmd.second->c_help, eval_example, cmd_file.get());
        }
    }

    auto sql_ref_path = std::filesystem::path(internals_dir) / "sql-ref.rst";
    auto sql_file = std::unique_ptr<FILE, decltype(&fclose)>(
        fopen(sql_ref_path.c_str(), "w+"), fclose);
    std::set<const help_text*> unique_sql_help;

    if (sql_file != nullptr) {
        for (const auto& sql : sqlite_function_help) {
            if (unique_sql_help.find(sql.second) != unique_sql_help.end()) {
                continue;
            }
            unique_sql_help.insert(sql.second);
            format_help_text_for_rst(*sql.second, eval_example, sql_file.get());
        }
        for (const auto& cmd_pair : *sql_cmd_map) {
            if (cmd_pair.second->c_help.ht_name == nullptr) {
                continue;
            }
            format_help_text_for_rst(
                cmd_pair.second->c_help, eval_example, sql_file.get());
        }
    }
}

}  // namespace lnav
