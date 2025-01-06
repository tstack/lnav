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
 * @file sql_help.hh
 */

#ifndef sql_help_hh
#define sql_help_hh

#include <map>
#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "base/intern_string.hh"
#include "help_text.hh"

extern string_attr_type<void> SQL_COMMAND_ATTR;
extern string_attr_type<void> SQL_KEYWORD_ATTR;
extern string_attr_type<void> SQL_IDENTIFIER_ATTR;
extern string_attr_type<void> SQL_FUNCTION_ATTR;
extern string_attr_type<void> SQL_STRING_ATTR;
extern string_attr_type<void> SQL_NUMBER_ATTR;
extern string_attr_type<void> SQL_OPERATOR_ATTR;
extern string_attr_type<void> SQL_PAREN_ATTR;
extern string_attr_type<void> SQL_GARBAGE_ATTR;
extern string_attr_type<void> SQL_COMMENT_ATTR;

void annotate_sql_statement(attr_line_t& al_inout);

extern std::multimap<std::string, const help_text*> sqlite_function_help;

std::string sql_keyword_re();
std::vector<const help_text*> find_sql_help_for_line(const attr_line_t& al,
                                                     size_t x);

namespace lnav {
namespace sql {

extern string_attr_type<void> PRQL_STAGE_ATTR;
extern string_attr_type<void> PRQL_TRANSFORM_ATTR;
extern string_attr_type<void> PRQL_KEYWORD_ATTR;
extern string_attr_type<void> PRQL_IDENTIFIER_ATTR;
extern string_attr_type<void> PRQL_FQID_ATTR;
extern string_attr_type<void> PRQL_PIPE_ATTR;
extern string_attr_type<void> PRQL_DOT_ATTR;
extern string_attr_type<void> PRQL_STRING_ATTR;
extern string_attr_type<void> PRQL_NUMBER_ATTR;
extern string_attr_type<void> PRQL_OPERATOR_ATTR;
extern string_attr_type<void> PRQL_PAREN_ATTR;
extern string_attr_type<void> PRQL_UNTERMINATED_PAREN_ATTR;
extern string_attr_type<void> PRQL_GARBAGE_ATTR;
extern string_attr_type<void> PRQL_COMMENT_ATTR;

bool is_prql(const string_fragment& sf);

void annotate_prql_statement(attr_line_t& al);

extern const char* const prql_keywords[];
extern std::multimap<std::string, const help_text*> prql_functions;

}  // namespace sql

namespace prql {

std::string quote_ident(std::string id);

}  // namespace prql

}  // namespace lnav

#endif
