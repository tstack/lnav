/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file sql_util.hh
 */

#ifndef lnav_sql_util_hh
#define lnav_sql_util_hh

#include <array>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <sqlite3.h>
#include <sys/time.h>
#include <time.h>

#include "base/attr_line.hh"
#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/time_util.hh"

extern const std::array<const char*, 145> sql_keywords;
extern const char* sql_function_names[];
extern const std::unordered_map<unsigned char, const char*>
    sql_constraint_names;

inline const char*
sql_constraint_op_name(unsigned char op)
{
    const auto iter = sql_constraint_names.find(op);
    if (iter == sql_constraint_names.end()) {
        return "??";
    }

    return iter->second;
}

using sqlite_exec_callback = int (*)(void*, int, char**, char**);
using db_table_list_t = std::vector<std::string>;
using db_table_map_t = std::map<std::string, db_table_list_t>;

struct sqlite_metadata_callbacks {
    sqlite_exec_callback smc_collation_list;
    sqlite_exec_callback smc_database_list;
    sqlite_exec_callback smc_table_list;
    sqlite_exec_callback smc_table_info;
    sqlite_exec_callback smc_foreign_key_list;
    void* smc_userdata{nullptr};
    std::string smc_table_name;
    db_table_map_t smc_db_list{};
};

int walk_sqlite_metadata(sqlite3* db, struct sqlite_metadata_callbacks& smc);

void dump_sqlite_schema(sqlite3* db, std::string& schema_out);

void attach_sqlite_db(sqlite3* db, const std::string& filename);

inline ssize_t
sql_strftime(char* buffer,
             size_t buffer_size,
             lnav::time64_t tim,
             int millis,
             char sep = ' ')
{
    return lnav::strftime_rfc3339(buffer, buffer_size, tim, millis, sep);
}

inline ssize_t
sql_strftime(char* buffer,
             size_t buffer_size,
             const struct timeval& tv,
             char sep = ' ')
{
    return sql_strftime(buffer, buffer_size, tv.tv_sec, tv.tv_usec / 1000, sep);
}

void sql_install_logger();

bool sql_ident_needs_quote(const char* ident);

auto_mem<char, sqlite3_free> sql_quote_ident(const char* ident);

std::string sql_safe_ident(const string_fragment& ident);

std::string sql_quote_text(const std::string& str);

int guess_type_from_pcre(const std::string& pattern, std::string& collator);

const char* sqlite3_type_to_string(int type);

attr_line_t sqlite3_errmsg_to_attr_line(sqlite3* db);

attr_line_t annotate_sql_with_error(sqlite3* db,
                                    const char* sql,
                                    const char* tail);

int sqlite_authorizer(void* pUserData,
                      int action_code,
                      const char* detail1,
                      const char* detail2,
                      const char* detail3,
                      const char* detail4);

namespace lnav::sql {

auto_mem<char, sqlite3_free> mprintf(const char* fmt, ...);

}

#endif
