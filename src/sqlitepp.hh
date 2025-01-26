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
 *
 * @file sqlitepp.hh
 */

#ifndef lnav_sqlitepp_hh
#define lnav_sqlitepp_hh

#include <string>

#include <sqlite3.h>

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/types.hh"

/* XXX figure out how to do this with the template */
void sqlite_close_wrapper(void* mem);

using auto_sqlite3 = auto_mem<sqlite3, sqlite_close_wrapper>;

namespace sqlitepp {

inline auto_mem<char>
quote(const std::optional<std::string>& str)
{
    auto_mem<char> retval(sqlite3_free);

    if (str) {
        retval = sqlite3_mprintf("%Q", str.value().c_str());
    } else {
        retval = sqlite3_mprintf("NULL");
    }

    return retval;
}

extern const char* ERROR_PREFIX;

struct bind_visitor {
    bind_visitor(sqlite3_stmt* stmt, int index) : bv_stmt(stmt), bv_index(index)
    {
    }

    void operator()(const std::string& str) const
    {
        sqlite3_bind_text(this->bv_stmt,
                          this->bv_index,
                          str.c_str(),
                          str.size(),
                          SQLITE_TRANSIENT);
    }

    void operator()(const string_fragment& str) const
    {
        sqlite3_bind_text(this->bv_stmt,
                          this->bv_index,
                          str.data(),
                          str.length(),
                          SQLITE_TRANSIENT);
    }

    void operator()(null_value_t) const
    {
        sqlite3_bind_null(this->bv_stmt, this->bv_index);
    }

    void operator()(int64_t value) const
    {
        sqlite3_bind_int64(this->bv_stmt, this->bv_index, value);
    }

    void operator()(double value) const
    {
        sqlite3_bind_double(this->bv_stmt, this->bv_index, value);
    }

    void operator()(bool value) const
    {
        sqlite3_bind_int(this->bv_stmt, this->bv_index, value);
    }

    sqlite3_stmt* bv_stmt;
    int bv_index;
};

}  // namespace sqlitepp

#endif
