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
#include <unordered_set>

#include "readline_possibilities.hh"

#include "base/fs_util.hh"
#include "base/isc.hh"
#include "base/opt_util.hh"
#include "config.h"
#include "data_parser.hh"
#include "date/tz.h"
#include "lnav.hh"
#include "lnav.prompt.hh"
#include "lnav_config.hh"
#include "log_data_helper.hh"
#include "log_format_ext.hh"
#include "session_data.hh"
#include "tailer/tailer.looper.hh"

static void
tokenize_view_text(std::unordered_set<std::string>& accum, string_fragment text)
{
    data_scanner ds(text);

    while (true) {
        auto tok_res = ds.tokenize2();

        if (!tok_res) {
            break;
        }
        if (tok_res->tr_capture.length() < 3) {
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

        accum.emplace(tok_res->to_string());
        switch (tok_res->tr_token) {
            case DT_QUOTED_STRING:
                tokenize_view_text(
                    accum, ds.to_string_fragment(tok_res->tr_inner_capture));
                break;
            default:
                break;
        }
    }
}

std::unordered_set<std::string>
view_text_possibilities(textview_curses& tc)
{
    std::unordered_set<std::string> retval;
    auto* tss = tc.get_sub_source();
    std::string accum;

    if (tc.get_inner_height() > 0_vl) {
        for (auto curr_line = tc.get_top(); curr_line <= tc.get_bottom();
             ++curr_line)
        {
            std::string line;

            tss->text_value_for_line(
                tc, curr_line, line, text_sub_source::RF_RAW);
            if (curr_line > tc.get_top()) {
                accum.push_back('\n');
            }
            accum.append(line);
        }

        tokenize_view_text(retval, accum);
    }

    return retval;
}

void
add_env_possibilities(ln_mode_t context)
{
#if 0
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
#endif
}

void
add_recent_netlocs_possibilities()
{
#if 0
    readline_curses* rc = lnav_data.ld_rl_view;

    rc->clear_possibilities(ln_mode_t::COMMAND, "recent-netlocs");
    std::set<std::string> netlocs;

    isc::to<tailer::looper&, services::remote_tailer_t>().send_and_wait(
        [&netlocs](auto& tlooper) { netlocs = tlooper.active_netlocs(); });
    netlocs.insert(recent_refs.rr_netlocs.begin(),
                   recent_refs.rr_netlocs.end());
    rc->add_possibility(ln_mode_t::COMMAND, "recent-netlocs", netlocs);
#endif
}
