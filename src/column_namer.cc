/**
 * Copyright (c) 2019, Timothy Stack
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
 *
 * @file column_namer.cc
 */

#include <algorithm>

#include "column_namer.hh"

#include "base/itertools.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "sql_util.hh"

const string_fragment column_namer::BUILTIN_COL = "col"_frag;

column_namer::column_namer(language lang) : cn_language(lang)
{
    this->cn_builtin_names.emplace_back(BUILTIN_COL);
    this->cn_builtin_names.emplace_back("log_time"_frag);
    this->cn_builtin_names.emplace_back("log_level"_frag);
    this->cn_builtin_names.emplace_back("log_opid"_frag);
}

bool
column_namer::existing_name(const string_fragment& in_name) const
{
    switch (this->cn_language) {
        case language::SQL: {
            auto upped = toupper(in_name.to_string());

            if (std::binary_search(
                    std::begin(sql_keywords), std::end(sql_keywords), upped))
            {
                return true;
            }
            break;
        }
        case language::JSON:
            break;
    }

    if (this->cn_builtin_names | lnav::itertools::find(in_name)) {
        return true;
    }

    if (this->cn_names | lnav::itertools::find(in_name)) {
        return true;
    }

    return false;
}

string_fragment
column_namer::add_column(const string_fragment& in_name)
{
    string_fragment base_name;
    string_fragment retval;
    fmt::memory_buffer buf;
    int num = 0;

    if (in_name.empty()) {
        base_name = BUILTIN_COL;
    } else {
        base_name = in_name;
    }

    retval = base_name;
    auto counter_iter = this->cn_name_counters.find(retval);
    if (counter_iter != this->cn_name_counters.end()) {
        num = ++counter_iter->second;
        fmt::format_to(
            std::back_inserter(buf), FMT_STRING("{}_{}"), base_name, num);
        retval = string_fragment::from_memory_buffer(buf);
    }

    while (this->existing_name(retval)) {
        if (num == 0) {
            auto counter_name = retval.to_owned(this->cn_alloc);
            this->cn_name_counters[counter_name] = num;
        }

        fmt::format_to(
            std::back_inserter(buf), FMT_STRING("{}_{}"), base_name, num);
        log_debug("column name already exists (%.*s), trying (%.*s)",
                  retval.length(),
                  retval.data(),
                  buf.size(),
                  buf.data());
        retval = string_fragment::from_memory_buffer(buf);
        num += 1;
    }

    retval = retval.to_owned(this->cn_alloc);
    this->cn_names.emplace_back(retval);

    return retval;
}
