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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file sql_util.hh
 */

#ifndef _sql_util_hh
#define _sql_util_hh

#include <time.h>
#include <sys/time.h>

#include <sqlite3.h>

#include <map>
#include <string>
#include <vector>

#include "attr_line.hh"

extern const char *sql_keywords[122];
extern const char *sql_function_names[];

typedef int (*sqlite_exec_callback)(void *, int, char **, char **);
typedef std::vector<std::string>               db_table_list_t;
typedef std::map<std::string, db_table_list_t> db_table_map_t;

struct sqlite_metadata_callbacks {
    sqlite_exec_callback smc_collation_list;
    sqlite_exec_callback smc_database_list;
    sqlite_exec_callback smc_table_list;
    sqlite_exec_callback smc_table_info;
    sqlite_exec_callback smc_foreign_key_list;
    void *smc_userdata;
    db_table_map_t       smc_db_list;
};

int walk_sqlite_metadata(sqlite3 *db, struct sqlite_metadata_callbacks &smc);

void dump_sqlite_schema(sqlite3 *db, std::string &schema_out);

void attach_sqlite_db(sqlite3 *db, const std::string &filename);

ssize_t sql_strftime(char *buffer, size_t buffer_size, time_t time, int millis,
    char sep = ' ');

inline ssize_t sql_strftime(char *buffer, size_t buffer_size,
                            const struct timeval &tv, char sep = ' ')
{
    return sql_strftime(buffer, buffer_size, tv.tv_sec, tv.tv_usec / 1000, sep);
}

void sql_install_logger(void);

bool sql_ident_needs_quote(const char *ident);

char *sql_quote_ident(const char *ident);

std::string sql_safe_ident(const string_fragment &ident);

void sql_compile_script(sqlite3 *db,
                        const char *src_name,
                        const char *script,
                        std::vector<sqlite3_stmt *> &stmts,
                        std::vector<std::string> &errors);

void sql_execute_script(sqlite3 *db,
                        const std::vector<sqlite3_stmt *> &stmts,
                        std::vector<std::string> &errors);

void sql_execute_script(sqlite3 *db,
                        const char *src_name,
                        const char *script,
                        std::vector<std::string> &errors);

int guess_type_from_pcre(const std::string &pattern, const char **collator);

/* XXX figure out how to do this with the template */
void sqlite_close_wrapper(void *mem);

int sqlite_authorizer(void* pUserData, int action_code, const char *detail1,
                      const char *detail2, const char *detail3,
                      const char *detail4);

extern string_attr_type SQL_KEYWORD_ATTR;
extern string_attr_type SQL_IDENTIFIER_ATTR;
extern string_attr_type SQL_FUNCTION_ATTR;
extern string_attr_type SQL_STRING_ATTR;
extern string_attr_type SQL_OPERATOR_ATTR;
extern string_attr_type SQL_PAREN_ATTR;
extern string_attr_type SQL_GARBAGE_ATTR;

void annotate_sql_statement(attr_line_t &al_inout);

std::string sql_keyword_re(void);

#endif
