/**
 * Copyright (c) 2017, Timothy Stack
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

#include "vtab_module.hh"

#include "config.h"
#include "lnav_util.hh"
#include "sqlitepp.hh"

std::string vtab_module_schemas;

std::map<intern_string_t, std::string> vtab_module_ddls;

void
to_sqlite(sqlite3_context* ctx, const lnav::console::user_message& um)
{
    auto errmsg = fmt::format(
        FMT_STRING("{}{}"), sqlitepp::ERROR_PREFIX, lnav::to_json(um));

    sqlite3_result_error(ctx, errmsg.c_str(), errmsg.size());
}

void
set_vtable_errmsg(sqlite3_vtab* vtab, const lnav::console::user_message& um)
{
    vtab->zErrMsg = sqlite3_mprintf(
        "%s%s", sqlitepp::ERROR_PREFIX, lnav::to_json(um).c_str());
}

lnav::console::user_message
sqlite3_error_to_user_message(sqlite3* db)
{
    const auto* errmsg = sqlite3_errmsg(db);
    if (startswith(errmsg, sqlitepp::ERROR_PREFIX)) {
        auto from_res = lnav::from_json<lnav::console::user_message>(
            &errmsg[strlen(sqlitepp::ERROR_PREFIX)]);

        if (from_res.isOk()) {
            return from_res.unwrap();
        }

        log_error("unable to parse error message: %s", errmsg);
        return lnav::console::user_message::error("internal error")
            .with_reason(from_res.unwrapErr()[0].um_message.get_string());
    }

    return lnav::console::user_message::error("SQL statement failed")
        .with_reason(errmsg);
}

void
vtab_index_usage::column_used(
    const vtab_index_constraints::const_iterator& iter)
{
    this->viu_min_column = std::min(iter->iColumn, this->viu_min_column);
    this->viu_max_column = std::max(iter->iColumn, this->viu_max_column);
    this->viu_index_info.idxNum |= (1L << iter.i_index);
    this->viu_used_column_count += 1;
}

void
vtab_index_usage::allocate_args(int low, int high, int required)
{
    int n_arg = 0;

    if (this->viu_min_column != low || this->viu_max_column > high
        || this->viu_used_column_count < required)
    {
        this->viu_index_info.estimatedCost = 2147483647;
        this->viu_index_info.estimatedRows = 2147483647;
        return;
    }

    for (int lpc = 0; lpc <= this->viu_max_column; lpc++) {
        for (int cons_index = 0; cons_index < this->viu_index_info.nConstraint;
             cons_index++)
        {
            if (this->viu_index_info.aConstraint[cons_index].iColumn != lpc) {
                continue;
            }
            if (!(this->viu_index_info.idxNum & (1L << cons_index))) {
                continue;
            }

            this->viu_index_info.aConstraintUsage[cons_index].argvIndex
                = ++n_arg;
        }
    }
    this->viu_index_info.estimatedCost = 1.0;
    this->viu_index_info.estimatedRows = 1;
}
