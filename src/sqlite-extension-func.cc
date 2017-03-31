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
 * @file sqlite-extension-func.c
 */

#include "config.h"

#include <stdio.h>
#include <assert.h>

#include "lnav_util.hh"
#include "lnav_log.hh"

#include "sqlite-extension-func.hh"

using namespace std;

sqlite_registration_func_t sqlite_registration_funcs[] = {
    common_extension_functions,
    state_extension_functions,
    string_extension_functions,
    network_extension_functions,
    fs_extension_functions,
    json_extension_functions,
    time_extension_functions,

    NULL
};

std::unordered_map<std::string, help_text *> sqlite_function_help;

int register_sqlite_funcs(sqlite3 *db, sqlite_registration_func_t *reg_funcs)
{
    int lpc;

    assert(db != NULL);
    assert(reg_funcs != NULL);
    
    for (lpc = 0; reg_funcs[lpc]; lpc++) {
        struct FuncDef *basic_funcs = NULL;
        struct FuncDefAgg *agg_funcs = NULL;
        int i;

        reg_funcs[lpc](&basic_funcs, &agg_funcs);

        for (i = 0; basic_funcs && basic_funcs[i].zName; i++) {
            struct FuncDef &fd = basic_funcs[i];

            //sqlite3CreateFunc
            /* LMH no error checking */
            sqlite3_create_function(db,
                                    basic_funcs[i].zName,
                                    basic_funcs[i].nArg,
                                    basic_funcs[i].eTextRep,
                                    (void *) &fd,
                                    basic_funcs[i].xFunc,
                                    0,
                                    0);

            if (fd.fd_help.ht_context != HC_NONE) {
                help_text &ht = fd.fd_help;

                sqlite_function_help[ht.ht_name] = &ht;
            }
        }

        for (i = 0; agg_funcs && agg_funcs[i].zName; i++) {
            //sqlite3CreateFunc
            sqlite3_create_function(db,
                                    agg_funcs[i].zName,
                                    agg_funcs[i].nArg,
                                    SQLITE_UTF8,
                                    (void *) &agg_funcs[i],
                                    0,
                                    agg_funcs[i].xStep,
                                    agg_funcs[i].xFinalize);
        }
    }

    static help_text builtin_funcs[] = {
        help_text("abs",
                  "Return the absolute value of the argument")
            .sql_function()
            .with_parameter({"x", "The number to convert"})
            .with_example({"SELECT abs(-1)"}),

        help_text("changes",
                  "The number of database rows that were changed, inserted, or deleted by the most recent statement.")
            .sql_function(),

        help_text("char",
                  "Returns a string composed of characters having the given unicode code point values")
            .sql_function()
            .with_parameter(help_text("X", "The unicode code point values")
                                .zero_or_more())
            .with_example({"SELECT char(0x48, 0x49)"}),

        help_text("coalesce",
                  "Returns a copy of its first non-NULL argument, or NULL if all arguments are NULL")
            .sql_function()
            .with_parameter({"X", "A value to check for NULL-ness"})
            .with_parameter(help_text("Y", "A value to check for NULL-ness")
                                .one_or_more())
            .with_example({"SELECT coalesce(null, 0, null)"}),

        help_text("glob",
                  "Match a string against Unix glob pattern")
            .sql_function()
            .with_parameter({"pattern", "The glob pattern"})
            .with_parameter({"str", "The string to match"})
            .with_example({"SELECT glob('a*', 'abc')"}),

        help_text("hex",
                  "Returns a string which is the upper-case hexadecimal rendering of the content of its argument.")
            .sql_function()
            .with_parameter({"X", "The blob to convert to hexadecimal"})
            .with_example({"SELECT hex('abc')"}),

        help_text("ifnull",
                  "Returns a copy of its first non-NULL argument, or NULL if both arguments are NULL")
            .sql_function()
            .with_parameter({"X", "A value to check for NULL-ness"})
            .with_parameter({"Y", "A value to check for NULL-ness"})
            .with_example({"SELECT ifnull(null, 0)"}),

        help_text("instr",
                  "Finds the first occurrence of the needle within the haystack and returns the number of prior characters plus 1, or 0 if Y is nowhere found within X")
            .sql_function()
            .with_parameter({"haystack", "The string to search within"})
            .with_parameter({"needle", "The string to look for in the haystack"})
            .with_example({"SELECT instr('abc', 'b')"}),

        help_text("last_insert_rowid",
                  "Returns the ROWID of the last row insert from the database connection which invoked the function")
            .sql_function(),

        help_text("length",
                  "Returns the number of characters (not bytes) in the given string prior to the first NUL character")
            .sql_function()
            .with_parameter({"str", "The string to determine the length of"})
            .with_example({"SELECT length('abc')"}),

        help_text("like",
                  "Match a string against a pattern")
            .sql_function()
            .with_parameter({"pattern",
                             "The pattern to match.  "
                                 "A percent symbol (%) will match zero or more characters "
                                 "and an underscore (_) will match a single character."})
            .with_parameter({"str", "The string to match"})
            .with_parameter(help_text("escape",
                                      "The escape character that can be used to prefix a literal percent or underscore in the pattern.")
                                .optional())
            .with_example({"SELECT like('%b%', 'aabcc')"})
            .with_example({"SELECT like('%b:%', 'aab%', ':')"}),

        help_text("likelihood",
                  "Provides a hint to the query planner that the first argument is a boolean that is true with the given probability")
            .sql_function()
            .with_parameter({"value", "The boolean value to return"})
            .with_parameter({"probability", "A floating point constant between 0.0 and 1.0"}),

        help_text("likely",
                  "Short-hand for likelihood(X,0.9375)")
            .sql_function()
            .with_parameter({"value", "The boolean value to return"}),

        help_text("load_extension",
                  "Loads SQLite extensions out of the given shared library file using the given entry point.")
            .sql_function()
            .with_parameter({"path", "The path to the shared library containing the extension."})
            .with_parameter(help_text("entry-point", "")
                                .optional()),

        help_text("lower",
                  "Returns a copy of the given string with all ASCII characters converted to lower case.")
            .sql_function()
            .with_parameter({"str", "The string to convert."})
            .with_example({"SELECT lower('AbC')"}),

        help_text("ltrim",
                  "Returns a string formed by removing any and all characters that appear in the second argument from the left side of the first.")
            .sql_function()
            .with_parameter({"str", "The string to trim characters from the left side"})
            .with_parameter(help_text("chars", "The characters to trim.  Defaults to spaces.")
                                .optional())
            .with_example({"SELECT ltrim('   abc')"})
            .with_example({"SELECT ltrim('aaaabbbc', 'ab')"}),

        help_text("max",
                  "Returns the argument with the maximum value, or return NULL if any argument is NULL.")
            .sql_function()
            .with_parameter(help_text("X", "The numbers to find the maximum of.  "
                "If only one argument is given, this function operates as an aggregate.")
                                .one_or_more())
            .with_example({"SELECT max(2, 1, 3)"})
            .with_example({"SELECT max(status) FROM http_status_codes"}),

        help_text("min",
                  "Returns the argument with the minimum value, or return NULL if any argument is NULL.")
            .sql_function()
            .with_parameter(help_text("X", "The numbers to find the minimum of.  "
                "If only one argument is given, this function operates as an aggregate.")
                                .one_or_more())
            .with_example({"SELECT min(2, 1, 3)"})
            .with_example({"SELECT min(status) FROM http_status_codes"}),

        help_text("nullif",
                  "Returns its first argument if the arguments are different and NULL if the arguments are the same.")
            .sql_function()
            .with_parameter({"X", "The first argument to compare."})
            .with_parameter({"Y", "The argument to compare against the first."})
            .with_example({"SELECT nullif(1, 1)"})
            .with_example({"SELECT nullif(1, 2)"}),

        help_text("printf",
                  "Returns a string with this functions arguments substituted into the given format.  "
                      "Substitution points are specified using percent (%) options, much like the standard C printf() function.")
            .sql_function()
            .with_parameter({"format", "The format of the string to return."})
            .with_parameter(help_text("X", "The argument to substitute at a given position in the format."))
            .with_example({"SELECT printf('Hello, %s!', 'World')"})
            .with_example({"SELECT printf('align: % 10s', 'small')"})
            .with_example({"SELECT printf('value: %05d', 11)"}),

        help_text("quote",
                  "Returns the text of an SQL literal which is the value of its argument suitable for inclusion into an SQL statement.")
            .sql_function()
            .with_parameter({"X", "The string to quote."})
            .with_example({"SELECT quote('abc')"})
            .with_example({"SELECT quote('abc''123')"}),

        help_text("random",
                  "Returns a pseudo-random integer between -9223372036854775808 and +9223372036854775807.")
            .sql_function(),

        help_text("randomblob",
                  "Return an N-byte blob containing pseudo-random bytes.")
            .sql_function()
            .with_parameter({"N", "The size of the blob in bytes."}),

        help_text("replace",
                  "Returns a string formed by substituting the replacement string for every occurrence of the old string in the given string.")
            .sql_function()
            .with_parameter({"str", "The string to perform substitutions on."})
            .with_parameter({"old", "The string to be replaced."})
            .with_parameter({"replacement", "The string to replace any occurrences of the old string with."})
            .with_example({"SELECT replace('abc', 'x', 'z')"})
            .with_example({"SELECT replace('abc', 'a', 'z')"}),

        help_text("round",
                  "Returns a floating-point value rounded to the given number of digits to the right of the decimal point.")
            .sql_function()
            .with_parameter({"num", "The value to round."})
            .with_parameter(help_text("digits", "The number of digits to the right of the decimal to round to.")
                                .optional())
            .with_example({"SELECT round(123.456)"})
            .with_example({"SELECT round(123.456, 1)"})
            .with_example({"SELECT round(123.456, 5)"}),

        help_text("rtrim",
                  "Returns a string formed by removing any and all characters that appear in the second argument from the right side of the first.")
            .sql_function()
            .with_parameter({"str", "The string to trim characters from the right side"})
            .with_parameter(help_text("chars", "The characters to trim.  Defaults to spaces.")
                                .optional())
            .with_example({"SELECT ltrim('abc   ')"})
            .with_example({"SELECT ltrim('abbbbcccc', 'bc')"}),

        help_text("sqlite_compileoption_get",
                  "Returns the N-th compile-time option used to build SQLite or NULL if N is out of range.")
            .sql_function()
            .with_parameter({"N", "The option number to get"}),

        help_text("sqlite_compileoption_used",
                  "Returns true (1) or false (0) depending on whether or not that compile-time option was used during the build.")
            .sql_function()
            .with_parameter({"option", "The name of the compile-time option."})
            .with_example({"SELECT sqlite_compileoption_used('ENABLE_FTS3')"}),

        help_text("sqlite_source_id",
                  "Returns a string that identifies the specific version of the source code that was used to build the SQLite library.")
            .sql_function(),

        help_text("sqlite_version",
                  "Returns the version string for the SQLite library that is running.")
            .sql_function(),

        help_text("substr",
                  "Returns a substring of input string X that begins with the Y-th character and which is Z characters long.")
            .sql_function()
            .with_parameter({"str", "The string to extract a substring from."})
            .with_parameter({"start", "The index within 'str' that is the start of the substring.  "
                "Indexes begin at 1.  "
                "A negative value means that the substring is found by counting from the right rather than the left.  "})
            .with_parameter(help_text("size", "The size of the substring.  "
                "If not given, then all characters through the end of the string are returned.  "
                "If the value is negative, then the characters before the start are returned.")
                                .optional())
            .with_example({"SELECT substr('abc', 2)"})
            .with_example({"SELECT substr('abc', 2, 1)"})
            .with_example({"SELECT substr('abc', -1)"})
            .with_example({"SELECT substr('abc', -1, -1)"}),

        help_text("total_changes",
                  "Returns the number of row changes caused by INSERT, UPDATE or DELETE statements since the current database connection was opened.")
            .sql_function(),

        help_text("trim",
                  "Returns a string formed by removing any and all characters that appear in the second argument from the left and right sides of the first.")
            .sql_function()
            .with_parameter({"str", "The string to trim characters from the left and right sides."})
            .with_parameter(help_text("chars", "The characters to trim.  Defaults to spaces.")
                                .optional())
            .with_example({"SELECT trim('    abc   ')"})
            .with_example({"SELECT trim('-+abc+-', '-+')"}),

        help_text("typeof",
                  "Returns a string that indicates the datatype of the expression X: \"null\", \"integer\", \"real\", \"text\", or \"blob\".")
            .sql_function()
            .with_parameter({"X", "The expression to check."})
            .with_example({"SELECT typeof(1)"})
            .with_example({"SELECT typeof('abc')"}),

        help_text("unicode",
                  "Returns the numeric unicode code point corresponding to the first character of the string X.")
            .sql_function()
            .with_parameter({"X", "The string to examine."})
            .with_example({"SELECT unicode('abc')"}),

        help_text("unlikely",
                  "Short-hand for likelihood(X, 0.0625)")
            .sql_function()
            .with_parameter({"value", "The boolean value to return"}),

        help_text("upper",
                  "Returns a copy of the given string with all ASCII characters converted to upper case.")
            .sql_function()
            .with_parameter({"str", "The string to convert."})
            .with_example({"SELECT upper('aBc')"}),

        help_text("zeroblob",
                  "Returns a BLOB consisting of N bytes of 0x00.")
            .sql_function()
            .with_parameter({"N", "The size of the BLOB."}),

    };

    for (auto &ht : builtin_funcs) {
        sqlite_function_help[ht.ht_name] = &ht;
    }

    static help_text idents[] = {
        help_text("SELECT",
                  "Query the database and return zero or more rows of data.")
            .sql_keyword()
            .with_parameter(help_text("result-column", "")
                                .one_or_more())
            .with_parameter(help_text("table", "The table(s) to query for data")
                                .with_flag_name("FROM")
                                .zero_or_more())
            .with_parameter(help_text("cond", "The conditions used to select the rows to return.")
                                .with_flag_name("WHERE")
                                .optional())
            .with_parameter(help_text("grouping-expr", "The expression to use when grouping rows.")
                                .with_flag_name("GROUP BY")
                                .zero_or_more())
            .with_parameter(help_text("ordering-term", "The values to use when ordering the result set.")
                                .with_flag_name("ORDER BY")
                                .zero_or_more())
            .with_parameter(help_text("limit-expr", "The maximum number of rows to return")
                                .with_flag_name("LIMIT")
                                .zero_or_more())
            .with_example({"SELECT * FROM syslog_log"}),

        help_text("WITH",
                  "Create a temporary view that exists only for the duration of a SQL statement.")
            .sql_keyword()
            .with_parameter(help_text("", "")
                                .with_flag_name("RECURSIVE")
                                .optional())
            .with_parameter({"cte-table-name", "The name for the temporary table."})
            .with_parameter(help_text("select-stmt", "The SELECT statement used to populate the temporary table.")
                                .with_flag_name("AS")),

        help_text("UPDATE",
                  "Modify a subset of values in zero or more rows of the given table")
            .sql_keyword()
            .with_parameter(help_text("table", "The table to update"))
            .with_parameter(help_text("")
                                .with_flag_name("SET"))
            .with_parameter(help_text("column-name", "The columns in the table to update.")
                                .with_parameter(help_text("expr", "The values to place into the column.")
                                                    .with_flag_name("="))
                                .one_or_more())
            .with_parameter(help_text("cond", "The condition used to determine whether a row should be updated.")
                                .with_flag_name("WHERE")
                                .optional()),

        help_text("CASE",
                  "Evaluate a series of expressions in order until one evaluates to true and then return it's result.  "
                      "Similar to an IF-THEN-ELSE construct in other languages.")
            .sql_keyword()
            .with_parameter(help_text("base-expr", "The base expression that is used for comparison in the branches")
                                .optional())
            .with_parameter(help_text("cmp-expr", "The expression to test if this branch should be taken")
                                .with_flag_name("WHEN")
                                .one_or_more()
                                .with_parameter(help_text("then-expr", "The result for this branch.")
                                                    .with_flag_name("THEN")))
            .with_parameter(help_text("else-expr", "The result of this CASE if no branches matched.")
                                .with_flag_name("ELSE")
                                .optional())
            .with_parameter(help_text("")
                                .with_flag_name("END"))
            .with_example({"SELECT CASE 1 WHEN 0 THEN 'zero' WHEN 1 THEN 'one' END"}),

    };

    for (auto &ht : idents) {
        sqlite_function_help[tolower(ht.ht_name)] = &ht;
        for (const auto &param : ht.ht_parameters) {
            if (!param.ht_flag_name) {
                continue;
            }
            sqlite_function_help[tolower(param.ht_flag_name)] = &ht;
        }
    }

    for (const auto &iter : sqlite_function_help) {
        log_debug("help %s", iter.first.c_str());
    }

    return 0;
}
