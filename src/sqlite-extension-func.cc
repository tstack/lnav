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
 * @file sqlite-extension-func.c
 */

#include "sqlite-extension-func.hh"

#include "base/auto_mem.hh"
#include "base/itertools.hh"
#include "base/lnav_log.hh"
#include "base/string_util.hh"
#include "config.h"
#include "sql_help.hh"

extern "C"
{
struct sqlite3_api_routines;

int sqlite3_series_init(sqlite3* db,
                        char** pzErrMsg,
                        const sqlite3_api_routines* pApi);
}

#ifdef HAVE_RUST_DEPS
rust::Vec<prqlc::SourceTreeElement> sqlite_extension_prql;
#endif

namespace lnav {
namespace sql {
std::multimap<std::string, const help_text*> prql_functions;

}
}  // namespace lnav

sqlite_registration_func_t sqlite_registration_funcs[] = {
    common_extension_functions,
    state_extension_functions,
    string_extension_functions,
    network_extension_functions,
    fs_extension_functions,
    json_extension_functions,
    yaml_extension_functions,
    time_extension_functions,

    nullptr,
};

struct prql_hier {
    std::map<std::string, prql_hier> ph_modules;
    std::map<std::string, std::string> ph_declarations;

    void to_string(std::string& accum) const
    {
        for (const auto& mod_pair : this->ph_modules) {
            accum.append("module ");
            accum.append(mod_pair.first);
            accum.append(" {\n");
            mod_pair.second.to_string(accum);
            accum.append("}\n");
        }
        for (const auto& decl_pair : this->ph_declarations) {
            accum.append(decl_pair.second);
            accum.append("\n");
        }
    }
};

static void
register_help(prql_hier& phier, const help_text& ht)
{
    auto prql_fqid
        = fmt::format(FMT_STRING("{}"), fmt::join(ht.ht_prql_path, "."));
    lnav::sql::prql_functions.emplace(prql_fqid, &ht);

    auto* curr_hier = &phier;
    for (size_t name_index = 0; name_index < ht.ht_prql_path.size();
         name_index++)
    {
        const auto& prql_name = ht.ht_prql_path[name_index];
        if (name_index == ht.ht_prql_path.size() - 1) {
            auto param_names
                = ht.ht_parameters | lnav::itertools::map([](const auto& elem) {
                      if (elem.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                          return fmt::format(FMT_STRING("{}:null"),
                                             elem.ht_name);
                      }
                      return fmt::format(FMT_STRING("p_{}"), elem.ht_name);
                  });
            auto func_args
                = ht.ht_parameters | lnav::itertools::map([](const auto& elem) {
                      if (elem.ht_nargs == help_nargs_t::HN_OPTIONAL) {
                          return fmt::format(FMT_STRING("{{{}:0}}"),
                                             elem.ht_name);
                      }
                      return fmt::format(FMT_STRING("{{p_{}:0}}"),
                                         elem.ht_name);
                  });
            curr_hier->ph_declarations[prql_name]
                = fmt::format(FMT_STRING("let {} = func {} -> s\"{}({})\""),
                              prql_name,
                              fmt::join(param_names, " "),
                              ht.ht_name,
                              fmt::join(func_args, ", "));
        } else {
            curr_hier = &curr_hier->ph_modules[prql_name];
        }
    }
}

int
register_sqlite_funcs(sqlite3* db, sqlite_registration_func_t* reg_funcs)
{
    static bool help_registration_done = false;
    prql_hier phier;
    int lpc;

    require(db != nullptr);
    require(reg_funcs != nullptr);

    {
        auto_mem<char> errmsg(sqlite3_free);

        sqlite3_series_init(db, errmsg.out(), nullptr);
    }

    for (lpc = 0; reg_funcs[lpc]; lpc++) {
        struct FuncDef* basic_funcs = nullptr;
        struct FuncDefAgg* agg_funcs = nullptr;
        int i;

        reg_funcs[lpc](&basic_funcs, &agg_funcs);

        for (i = 0; basic_funcs && basic_funcs[i].zName; i++) {
            struct FuncDef& fd = basic_funcs[i];

            // sqlite3CreateFunc
            /* LMH no error checking */
            sqlite3_create_function(db,
                                    basic_funcs[i].zName,
                                    basic_funcs[i].nArg,
                                    basic_funcs[i].eTextRep,
                                    (void*) &fd,
                                    basic_funcs[i].xFunc,
                                    nullptr,
                                    nullptr);

            if (!help_registration_done
                && fd.fd_help.ht_context != help_context_t::HC_NONE)
            {
                auto& ht = fd.fd_help;

                sqlite_function_help.insert(std::make_pair(ht.ht_name, &ht));
                ht.index_tags();
                if (!ht.ht_prql_path.empty()) {
                    register_help(phier, ht);
                }
            }
        }

        for (i = 0; agg_funcs && agg_funcs[i].zName; i++) {
            struct FuncDefAgg& fda = agg_funcs[i];

            // sqlite3CreateFunc
            sqlite3_create_function(db,
                                    agg_funcs[i].zName,
                                    agg_funcs[i].nArg,
                                    SQLITE_UTF8,
                                    (void*) &agg_funcs[i],
                                    nullptr,
                                    agg_funcs[i].xStep,
                                    agg_funcs[i].xFinalize);

            if (!help_registration_done
                && fda.fda_help.ht_context != help_context_t::HC_NONE)
            {
                auto& ht = fda.fda_help;

                sqlite_function_help.insert(std::make_pair(ht.ht_name, &ht));
                ht.index_tags();
                if (!ht.ht_prql_path.empty()) {
                    register_help(phier, ht);
                }
            }
        }
    }

#ifdef HAVE_RUST_DEPS
    if (sqlite_extension_prql.empty()) {
        require(phier.ph_declarations.empty());
        for (const auto& mod_pair : phier.ph_modules) {
            std::string content;

            mod_pair.second.to_string(content);
            sqlite_extension_prql.emplace_back(prqlc::SourceTreeElement{
                fmt::format(FMT_STRING("{}.prql"), mod_pair.first),
                content,
            });
        }
    }
#endif

    static help_text builtin_funcs[] = {
        help_text("abs", "Return the absolute value of the argument")
            .sql_function()
            .with_parameter({"x", "The number to convert"})
            .with_tags({"math"})
            .with_example(
                {"To get the absolute value of -1", "SELECT abs(-1)"}),

        help_text("changes",
                  "The number of database rows that were changed, inserted, or "
                  "deleted by the most recent statement.")
            .sql_function(),

        help_text("char",
                  "Returns a string composed of characters having the given "
                  "unicode code point values")
            .sql_function()
            .with_parameter(
                help_text("X", "The unicode code point values").zero_or_more())
            .with_tags({"string"})
            .with_example({"To get a string with the code points 0x48 and 0x49",
                           "SELECT char(0x48, 0x49)"}),

        help_text("coalesce",
                  "Returns a copy of its first non-NULL argument, or NULL if "
                  "all arguments are NULL")
            .sql_function()
            .with_parameter({"X", "A value to check for NULL-ness"})
            .with_parameter(
                help_text("Y", "A value to check for NULL-ness").one_or_more())
            .with_example(
                {"To get the first non-null value from three parameters",
                 "SELECT coalesce(null, 0, null)"}),

        help_text("glob", "Match a string against Unix glob pattern")
            .sql_function()
            .with_parameter({"pattern", "The glob pattern"})
            .with_parameter({"str", "The string to match"})
            .with_example({"To test if the string 'abc' matches the glob 'a*'",
                           "SELECT glob('a*', 'abc')"}),

        help_text("hex",
                  "Returns a string which is the upper-case hexadecimal "
                  "rendering of the content of its argument.")
            .sql_function()
            .with_parameter({"X", "The blob to convert to hexadecimal"})
            .with_example(
                {"To get the hexadecimal rendering of the string 'abc'",
                 "SELECT hex('abc')"}),

        help_text("ifnull",
                  "Returns a copy of its first non-NULL argument, or NULL if "
                  "both arguments are NULL")
            .sql_function()
            .with_parameter({"X", "A value to check for NULL-ness"})
            .with_parameter({"Y", "A value to check for NULL-ness"})
            .with_example(
                {"To get the first non-null value between null and zero",
                 "SELECT ifnull(null, 0)"}),

        help_text("instr",
                  "Finds the first occurrence of the needle within the "
                  "haystack and returns the number of prior characters plus 1, "
                  "or 0 if the needle was not found")
            .sql_function()
            .with_parameter({"haystack", "The string to search within"})
            .with_parameter(
                {"needle", "The string to look for in the haystack"})
            .with_tags({"string"})
            .with_example(
                {"To test get the position of 'b' in the string 'abc'",
                 "SELECT instr('abc', 'b')"}),

        help_text("last_insert_rowid",
                  "Returns the ROWID of the last row insert from the database "
                  "connection which invoked the function")
            .sql_function(),

        help_text("length",
                  "Returns the number of characters (not bytes) in the given "
                  "string prior to the first NUL character")
            .sql_function()
            .with_parameter({"str", "The string to determine the length of"})
            .with_tags({"string"})
            .with_example({"To get the length of the string 'abc'",
                           "SELECT length('abc')"}),

        help_text("like", "Match a string against a pattern")
            .sql_function()
            .with_parameter(
                {"pattern",
                 "The pattern to match.  "
                 "A percent symbol (%) will match zero or more characters "
                 "and an underscore (_) will match a single character."})
            .with_parameter({"str", "The string to match"})
            .with_parameter(
                help_text("escape",
                          "The escape character that can be used to prefix a "
                          "literal percent or underscore in the pattern.")
                    .optional())
            .with_example(
                {"To test if the string 'aabcc' contains the letter 'b'",
                 "SELECT like('%b%', 'aabcc')"})
            .with_example({"To test if the string 'aab%' ends with 'b%'",
                           "SELECT like('%b:%', 'aab%', ':')"}),

        help_text(
            "likelihood",
            "Provides a hint to the query planner that the first argument is a "
            "boolean that is true with the given probability")
            .sql_function()
            .with_parameter({"value", "The boolean value to return"})
            .with_parameter({"probability",
                             "A floating point constant between 0.0 and 1.0"}),

        help_text("likely", "Short-hand for likelihood(X,0.9375)")
            .sql_function()
            .with_parameter({"value", "The boolean value to return"}),

        help_text("load_extension",
                  "Loads SQLite extensions out of the given shared library "
                  "file using the given entry point.")
            .sql_function()
            .with_parameter(
                {"path",
                 "The path to the shared library containing the extension."})
            .with_parameter(help_text("entry-point", "").optional()),

        help_text("lower",
                  "Returns a copy of the given string with all ASCII "
                  "characters converted to lower case.")
            .sql_function()
            .with_parameter({"str", "The string to convert."})
            .with_tags({"string"})
            .with_example(
                {"To lowercase the string 'AbC'", "SELECT lower('AbC')"}),

        help_text(
            "ltrim",
            "Returns a string formed by removing any and all characters that "
            "appear in the second argument from the left side of the first.")
            .sql_function()
            .with_parameter(
                {"str", "The string to trim characters from the left side"})
            .with_parameter(
                help_text("chars",
                          "The characters to trim.  Defaults to spaces.")
                    .optional())
            .with_tags({"string"})
            .with_example({
                "To trim the leading space characters from the string '   abc'",
                "SELECT ltrim('   abc')",
            })
            .with_example({
                "To trim the characters 'a' or 'b' from the left side of the "
                "string 'aaaabbbc'",
                "SELECT ltrim('aaaabbbc', 'ab')",
            }),

        help_text("max",
                  "Returns the argument with the maximum value, or return NULL "
                  "if any argument is NULL.")
            .sql_function()
            .with_parameter(help_text("X",
                                      "The numbers to find the maximum of.  "
                                      "If only one argument is given, this "
                                      "function operates as an aggregate.")
                                .one_or_more())
            .with_tags({"math"})
            .with_example({"To get the largest value from the parameters",
                           "SELECT max(2, 1, 3)"})
            .with_example({"To get the largest value from an aggregate",
                           "SELECT max(status) FROM http_status_codes"}),

        help_text("min",
                  "Returns the argument with the minimum value, or return NULL "
                  "if any argument is NULL.")
            .sql_function()
            .with_parameter(help_text("X",
                                      "The numbers to find the minimum of.  "
                                      "If only one argument is given, this "
                                      "function operates as an aggregate.")
                                .one_or_more())
            .with_tags({"math"})
            .with_example({"To get the smallest value from the parameters",
                           "SELECT min(2, 1, 3)"})
            .with_example({"To get the smallest value from an aggregate",
                           "SELECT min(status) FROM http_status_codes"}),

        help_text("nullif",
                  "Returns its first argument if the arguments are different "
                  "and NULL if the arguments are the same.")
            .sql_function()
            .with_parameter({"X", "The first argument to compare."})
            .with_parameter({"Y", "The argument to compare against the first."})
            .with_example(
                {"To test if 1 is different from 1", "SELECT nullif(1, 1)"})
            .with_example(
                {"To test if 1 is different from 2", "SELECT nullif(1, 2)"}),

        help_text("printf",
                  "Returns a string with this functions arguments substituted "
                  "into the given format.  "
                  "Substitution points are specified using percent (%) "
                  "options, much like the standard C printf() function.")
            .sql_function()
            .with_parameter({"format", "The format of the string to return."})
            .with_parameter(help_text("X",
                                      "The argument to substitute at a given "
                                      "position in the format."))
            .with_tags({"string"})
            .with_example({"To substitute 'World' into the string 'Hello, %s!'",
                           "SELECT printf('Hello, %s!', 'World')"})
            .with_example({"To right-align 'small' in the string 'align:' with "
                           "a column width of 10",
                           "SELECT printf('align: % 10s', 'small')"})
            .with_example({"To format 11 with a width of five characters and "
                           "leading zeroes",
                           "SELECT printf('value: %05d', 11)"}),

        help_text("quote",
                  "Returns the text of an SQL literal which is the value of "
                  "its argument suitable for inclusion into an SQL statement.")
            .sql_function()
            .with_parameter({"X", "The string to quote."})
            .with_example({"To quote the string 'abc'", "SELECT quote('abc')"})
            .with_example(
                {"To quote the string 'abc'123'", "SELECT quote('abc''123')"}),

        help_text("random",
                  "Returns a pseudo-random integer between "
                  "-9223372036854775808 and +9223372036854775807.")
            .sql_function(),

        help_text("randomblob",
                  "Return an N-byte blob containing pseudo-random bytes.")
            .sql_function()
            .with_parameter({"N", "The size of the blob in bytes."}),

        help_text(
            "replace",
            "Returns a string formed by substituting the replacement string "
            "for every occurrence of the old string in the given string.")
            .sql_function()
            .with_parameter({"str", "The string to perform substitutions on."})
            .with_parameter({"old", "The string to be replaced."})
            .with_parameter({"replacement",
                             "The string to replace any occurrences of the old "
                             "string with."})
            .with_tags({"string"})
            .with_example({"To replace the string 'x' with 'z' in 'abc'",
                           "SELECT replace('abc', 'x', 'z')"})
            .with_example({"To replace the string 'a' with 'z' in 'abc'",
                           "SELECT replace('abc', 'a', 'z')"}),

        help_text("round",
                  "Returns a floating-point value rounded to the given number "
                  "of digits to the right of the decimal point.")
            .sql_function()
            .with_parameter({"num", "The value to round."})
            .with_parameter(help_text("digits",
                                      "The number of digits to the right of "
                                      "the decimal to round to.")
                                .optional())
            .with_tags({"math"})
            .with_example({"To round the number 123.456 to an integer",
                           "SELECT round(123.456)"})
            .with_example({"To round the number 123.456 to a precision of 1",
                           "SELECT round(123.456, 1)"})
            .with_example({"To round the number 123.456 to a precision of 5",
                           "SELECT round(123.456, 5)"}),

        help_text(
            "rtrim",
            "Returns a string formed by removing any and all characters that "
            "appear in the second argument from the right side of the first.")
            .sql_function()
            .with_parameter(
                {"str", "The string to trim characters from the right side"})
            .with_parameter(
                help_text("chars",
                          "The characters to trim.  Defaults to spaces.")
                    .optional())
            .with_tags({"string"})
            .with_example({
                "To trim the space characters from the end of the string 'abc  "
                " '",
                "SELECT rtrim('abc   ')",
            })
            .with_example({
                "To trim the characters 'b' and 'c' from the string "
                "'abbbbcccc'",
                "SELECT rtrim('abbbbcccc', 'bc')",
            }),

        help_text("sqlite_compileoption_get",
                  "Returns the N-th compile-time option used to build SQLite "
                  "or NULL if N is out of range.")
            .sql_function()
            .with_parameter({"N", "The option number to get"}),

        help_text("sqlite_compileoption_used",
                  "Returns true (1) or false (0) depending on whether or not "
                  "that compile-time option was used during the build.")
            .sql_function()
            .with_parameter({"option", "The name of the compile-time option."})
            .with_example(
                {"To check if the SQLite library was compiled with ENABLE_FTS3",
                 "SELECT sqlite_compileoption_used('ENABLE_FTS3')"}),

        help_text("sqlite_source_id",
                  "Returns a string that identifies the specific version of "
                  "the source code that was used to build the SQLite library.")
            .sql_function(),

        help_text("sqlite_version",
                  "Returns the version string for the SQLite library that is "
                  "running.")
            .sql_function(),

        help_text("substr",
                  "Returns a substring of input string X that begins with the "
                  "Y-th character and which is Z characters long.")
            .sql_function()
            .with_parameter({"str", "The string to extract a substring from."})
            .with_parameter(
                {"start",
                 "The index within 'str' that is the start of the substring.  "
                 "Indexes begin at 1.  "
                 "A negative value means that the substring is found by "
                 "counting from the right rather than the left.  "})
            .with_parameter(
                help_text("size",
                          "The size of the substring.  "
                          "If not given, then all characters through the end "
                          "of the string are returned.  "
                          "If the value is negative, then the characters "
                          "before the start are returned.")
                    .optional())
            .with_tags({"string"})
            .with_example({"To get the substring starting at the second "
                           "character until the end of the string 'abc'",
                           "SELECT substr('abc', 2)"})
            .with_example({"To get the substring of size one starting at the "
                           "second character of the string 'abc'",
                           "SELECT substr('abc', 2, 1)"})
            .with_example({"To get the substring starting at the last "
                           "character until the end of the string 'abc'",
                           "SELECT substr('abc', -1)"})
            .with_example(
                {"To get the substring starting at the last character and "
                 "going backwards one step of the string 'abc'",
                 "SELECT substr('abc', -1, -1)"}),

        help_text("total_changes",
                  "Returns the number of row changes caused by INSERT, UPDATE "
                  "or DELETE statements since the current database connection "
                  "was opened.")
            .sql_function(),

        help_text("trim",
                  "Returns a string formed by removing any and all characters "
                  "that appear in the second argument from the left and right "
                  "sides of the first.")
            .sql_function()
            .with_parameter({"str",
                             "The string to trim characters from the left and "
                             "right sides."})
            .with_parameter(
                help_text("chars",
                          "The characters to trim.  Defaults to spaces.")
                    .optional())
            .with_tags({"string"})
            .with_example({
                "To trim spaces from the start and end of the string '    abc  "
                " '",
                "SELECT trim('    abc   ')",
            })
            .with_example({
                "To trim the characters '-' and '+' from the string '-+abc+-'",
                "SELECT trim('-+abc+-', '-+')",
            }),

        help_text(
            "typeof",
            "Returns a string that indicates the datatype of the expression X: "
            "\"null\", \"integer\", \"real\", \"text\", or \"blob\".")
            .sql_function()
            .with_parameter({"X", "The expression to check."})
            .with_example(
                {"To get the type of the number 1", "SELECT typeof(1)"})
            .with_example({"To get the type of the string 'abc'",
                           "SELECT typeof('abc')"}),

        help_text("unicode",
                  "Returns the numeric unicode code point corresponding to the "
                  "first character of the string X.")
            .sql_function()
            .with_parameter({"X", "The string to examine."})
            .with_tags({"string"})
            .with_example({"To get the unicode code point for the first "
                           "character of 'abc'",
                           "SELECT unicode('abc')"}),

        help_text("unlikely", "Short-hand for likelihood(X, 0.0625)")
            .sql_function()
            .with_parameter({"value", "The boolean value to return"}),

        help_text("upper",
                  "Returns a copy of the given string with all ASCII "
                  "characters converted to upper case.")
            .sql_function()
            .with_parameter({"str", "The string to convert."})
            .with_tags({"string"})
            .with_example(
                {"To uppercase the string 'aBc'", "SELECT upper('aBc')"}),

        help_text("zeroblob", "Returns a BLOB consisting of N bytes of 0x00.")
            .sql_function()
            .with_parameter({"N", "The size of the BLOB."}),

        help_text("date", "Returns the date in this format: YYYY-MM-DD.")
            .sql_function()
            .with_parameter({"timestring", "The string to convert to a date."})
            .with_parameter(help_text("modifier",
                                      "A transformation that is applied to the "
                                      "value to the left.")
                                .zero_or_more())
            .with_tags({"datetime"})
            .with_example({"To get the date portion of the timestamp "
                           "'2017-01-02T03:04:05'",
                           "SELECT date('2017-01-02T03:04:05')"})
            .with_example({"To get the date portion of the timestamp "
                           "'2017-01-02T03:04:05' plus one day",
                           "SELECT date('2017-01-02T03:04:05', '+1 day')"})
            .with_example(
                {"To get the date portion of the epoch timestamp 1491341842",
                 "SELECT date(1491341842, 'unixepoch')"}),

        help_text("time", "Returns the time in this format: HH:MM:SS.")
            .sql_function()
            .with_parameter({"timestring", "The string to convert to a time."})
            .with_parameter(help_text("modifier",
                                      "A transformation that is applied to the "
                                      "value to the left.")
                                .zero_or_more())
            .with_tags({"datetime"})
            .with_example({"To get the time portion of the timestamp "
                           "'2017-01-02T03:04:05'",
                           "SELECT time('2017-01-02T03:04:05')"})
            .with_example({"To get the time portion of the timestamp "
                           "'2017-01-02T03:04:05' plus one minute",
                           "SELECT time('2017-01-02T03:04:05', '+1 minute')"})
            .with_example(
                {"To get the time portion of the epoch timestamp 1491341842",
                 "SELECT time(1491341842, 'unixepoch')"}),

        help_text(
            "datetime",
            "Returns the date and time in this format: YYYY-MM-DD HH:MM:SS.")
            .sql_function()
            .with_parameter(
                {"timestring", "The string to convert to a date with time."})
            .with_parameter(help_text("modifier",
                                      "A transformation that is applied to the "
                                      "value to the left.")
                                .zero_or_more())
            .with_tags({"datetime"})
            .with_example({"To get the date and time portion of the timestamp "
                           "'2017-01-02T03:04:05'",
                           "SELECT datetime('2017-01-02T03:04:05')"})
            .with_example(
                {"To get the date and time portion of the timestamp "
                 "'2017-01-02T03:04:05' plus one minute",
                 "SELECT datetime('2017-01-02T03:04:05', '+1 minute')"})
            .with_example({"To get the date and time portion of the epoch "
                           "timestamp 1491341842",
                           "SELECT datetime(1491341842, 'unixepoch')"}),

        help_text("julianday",
                  "Returns the number of days since noon in Greenwich on "
                  "November 24, 4714 B.C.")
            .sql_function()
            .with_parameter(
                {"timestring", "The string to convert to a date with time."})
            .with_parameter(help_text("modifier",
                                      "A transformation that is applied to the "
                                      "value to the left.")
                                .zero_or_more())
            .with_tags({"datetime"})
            .with_example({"To get the julian day from the timestamp "
                           "'2017-01-02T03:04:05'",
                           "SELECT julianday('2017-01-02T03:04:05')"})
            .with_example(
                {"To get the julian day from the timestamp "
                 "'2017-01-02T03:04:05' plus one minute",
                 "SELECT julianday('2017-01-02T03:04:05', '+1 minute')"})
            .with_example(
                {"To get the julian day from the timestamp 1491341842",
                 "SELECT julianday(1491341842, 'unixepoch')"}),

        help_text("strftime",
                  "Returns the date formatted according to the format string "
                  "specified as the first argument.")
            .sql_function()
            .with_parameter(
                {"format",
                 "A format string with substitutions similar to those found in "
                 "the strftime() standard C library."})
            .with_parameter(
                {"timestring", "The string to convert to a date with time."})
            .with_parameter(help_text("modifier",
                                      "A transformation that is applied to the "
                                      "value to the left.")
                                .zero_or_more())
            .with_tags({"datetime"})
            .with_example(
                {"To get the year from the timestamp '2017-01-02T03:04:05'",
                 "SELECT strftime('%Y', '2017-01-02T03:04:05')"})
            .with_example({"To create a string with the time from the "
                           "timestamp '2017-01-02T03:04:05' plus one minute",
                           "SELECT strftime('The time is: %H:%M:%S', "
                           "'2017-01-02T03:04:05', '+1 minute')"})
            .with_example(
                {"To create a string with the Julian day from the epoch "
                 "timestamp 1491341842",
                 "SELECT strftime('Julian day: %J', 1491341842, 'unixepoch')"}),

        help_text(
            "avg",
            "Returns the average value of all non-NULL numbers within a group.")
            .sql_function()
            .with_parameter({"X", "The value to compute the average of."})
            .with_tags({"math"})
            .with_example({"To get the average of the column 'ex_duration' "
                           "from the table 'lnav_example_log'",
                           "SELECT avg(ex_duration) FROM lnav_example_log"})
            .with_example(
                {"To get the average of the column 'ex_duration' from the "
                 "table 'lnav_example_log' when grouped by 'ex_procname'",
                 "SELECT ex_procname, avg(ex_duration) FROM lnav_example_log "
                 "GROUP BY ex_procname"}),

        help_text("count",
                  "If the argument is '*', the total number of rows in the "
                  "group is returned.  "
                  "Otherwise, the number of times the argument is non-NULL.")
            .sql_function()
            .with_parameter({"X", "The value to count."})
            .with_example(
                {"To get the count of the non-NULL rows of 'lnav_example_log'",
                 "SELECT count(*) FROM lnav_example_log"})
            .with_example({"To get the count of the non-NULL values of "
                           "'log_part' from 'lnav_example_log'",
                           "SELECT count(log_part) FROM lnav_example_log"}),

        help_text("group_concat",
                  "Returns a string which is the concatenation of all non-NULL "
                  "values of X separated by a comma or the given separator.")
            .sql_function()
            .with_parameter({"X", "The value to concatenate."})
            .with_parameter(
                help_text("sep", "The separator to place between the values.")
                    .optional())
            .with_tags({"string"})
            .with_example(
                {"To concatenate the values of the column 'ex_procname' from "
                 "the table 'lnav_example_log'",
                 "SELECT group_concat(ex_procname) FROM lnav_example_log"})
            .with_example({"To join the values of the column 'ex_procname' "
                           "using the string ', '",
                           "SELECT group_concat(ex_procname, ', ') FROM "
                           "lnav_example_log"})
            .with_example({"To concatenate the distinct values of the column "
                           "'ex_procname' from the table 'lnav_example_log'",
                           "SELECT group_concat(DISTINCT ex_procname) FROM "
                           "lnav_example_log"}),

        help_text("sum",
                  "Returns the sum of the values in the group as an integer.")
            .sql_function()
            .with_parameter({"X", "The values to add."})
            .with_tags({"math"})
            .with_example({
                "To sum all of the values in the column "
                "'ex_duration' from the table 'lnav_example_log'",
                "SELECT sum(ex_duration) FROM lnav_example_log",
            }),

        help_text(
            "total",
            "Returns the sum of the values in the group as a floating-point.")
            .sql_function()
            .with_parameter({"X", "The values to add."})
            .with_tags({"math"})
            .with_example({
                "To total all of the values in the column "
                "'ex_duration' from the table 'lnav_example_log'",
                "SELECT total(ex_duration) FROM lnav_example_log",
            }),

        help_text("generate_series",
                  "A table-valued-function that returns the whole numbers "
                  "between a lower and upper bound, inclusive")
            .sql_table_valued_function()
            .with_parameter({"start", "The starting point of the series"})
            .with_parameter({"stop", "The stopping point of the series"})
            .with_parameter(
                help_text("step", "The increment between each value")
                    .optional())
            .with_result({"value", "The number in the series"})
            .with_example({
                "To generate the numbers in the range [10, 14]",
                "SELECT value FROM generate_series(10, 14)",
            })
            .with_example({
                "To generate every other number in the range [10, 14]",
                "SELECT value FROM generate_series(10, 14, 2)",
            })
            .with_example({"To count down from five to 1",
                           "SELECT value FROM generate_series(1, 5, -1)"}),

        help_text("json",
                  "Verifies that its argument is valid JSON and returns a "
                  "minified version or throws an error.")
            .sql_function()
            .with_parameter({"X", "The string to interpret as JSON."})
            .with_tags({"json"}),

        help_text("json_array", "Constructs a JSON array from its arguments.")
            .sql_function()
            .with_parameter(
                help_text{"X", "The values of the JSON array"}.zero_or_more())
            .with_tags({"json"})
            .with_example({"To create an array of all types",
                           "SELECT json_array(NULL, 1, 2.1, 'three', "
                           "json_array(4), json_object('five', 'six'))"})
            .with_example({"To create an empty array", "SELECT json_array()"}),

        help_text("json_array_length", "Returns the length of a JSON array.")
            .sql_function()
            .with_parameter({"X", "The JSON object."})
            .with_parameter(
                help_text{"P", "The path to the array in 'X'."}.optional())
            .with_tags({"json"})
            .with_example({"To get the length of an array",
                           "SELECT json_array_length('[1, 2, 3]')"})
            .with_example(
                {"To get the length of a nested array",
                 "SELECT json_array_length('{\"arr\": [1, 2, 3]}', '$.arr')"}),

        help_text(
            "json_extract",
            "Returns the value(s) from the given JSON at the given path(s).")
            .sql_function()
            .with_parameter({"X", "The JSON value."})
            .with_parameter(
                help_text{"P", "The path to extract."}.one_or_more())
            .with_tags({"json"})
            .with_example({"To get a number",
                           R"(SELECT json_extract('{"num": 1}', '$.num'))"})
            .with_example(
                {"To get two numbers",
                 R"(SELECT json_extract('{"num": 1, "val": 2}', '$.num', '$.val'))"})
            .with_example(
                {"To get an object",
                 R"(SELECT json_extract('{"obj": {"sub": 1}}', '$.obj'))"})
#if 0 && SQLITE_VERSION_NUMBER >= 3038000
            .with_example({"To get a JSON value using the short-hand",
                           R"(SELECT '{"a":"b"}' -> '$.a')"})
            .with_example({"To get a SQL value using the short-hand",
                           R"(SELECT '{"a":"b"}' ->> '$.a')"})
#endif
            ,

        help_text("json_insert",
                  "Inserts values into a JSON object/array at the given "
                  "locations, if it does not already exist")
            .sql_function()
            .with_parameter({"X", "The JSON value to update"})
            .with_parameter({"P",
                             "The path to the insertion point.  A '#' array "
                             "index means append the value"})
            .with_parameter({"Y", "The value to insert"})
            .with_tags({"json"})
            .with_example({"To append to an array",
                           R"(SELECT json_insert('[1, 2]', '$[#]', 3))"})
            .with_example({"To update an object",
                           R"(SELECT json_insert('{"a": 1}', '$.b', 2))"})
            .with_example({"To ensure a value is set",
                           R"(SELECT json_insert('{"a": 1}', '$.a', 2))"})
            .with_example(
                {"To update multiple values",
                 R"(SELECT json_insert('{"a": 1}', '$.b', 2, '$.c', 3))"}),

        help_text("json_replace",
                  "Replaces existing values in a JSON object/array at the "
                  "given locations")
            .sql_function()
            .with_parameter({"X", "The JSON value to update"})
            .with_parameter({"P", "The path to replace"})
            .with_parameter({"Y", "The new value for the property"})
            .with_tags({"json"})
            .with_example({"To replace an existing value",
                           R"(SELECT json_replace('{"a": 1}', '$.a', 2))"})
            .with_example(
                {"To replace a value without creating a new property",
                 R"(SELECT json_replace('{"a": 1}', '$.a', 2, '$.b', 3))"}),

        help_text("json_set",
                  "Inserts or replaces existing values in a JSON object/array "
                  "at the given locations")
            .sql_function()
            .with_parameter({"X", "The JSON value to update"})
            .with_parameter({"P",
                             "The path to the insertion point.  A '#' array "
                             "index means append the value"})
            .with_parameter({"Y", "The value to set"})
            .with_tags({"json"})
            .with_example({"To replace an existing array element",
                           R"(SELECT json_set('[1, 2]', '$[1]', 3))"})
            .with_example(
                {"To replace a value and create a new property",
                 R"(SELECT json_set('{"a": 1}', '$.a', 2, '$.b', 3))"}),

        help_text("json_object",
                  "Create a JSON object from the given arguments")
            .sql_function()
            .with_parameter({"N", "The property name"})
            .with_parameter({"V", "The property value"})
            .with_tags({"json"})
            .with_example(
                {"To create an object", "SELECT json_object('a', 1, 'b', 'c')"})
            .with_example(
                {"To create an empty object", "SELECT json_object()"}),

        help_text("json_remove", "Removes paths from a JSON value")
            .sql_function()
            .with_parameter({"X", "The JSON value to update"})
            .with_parameter(help_text{"P", "The paths to remove"}.one_or_more())
            .with_tags({"json"})
            .with_example({"To remove elements of an array",
                           "SELECT json_remove('[1,2,3]', '$[1]', '$[1]')"})
            .with_example({"To remove object properties",
                           R"(SELECT json_remove('{"a":1,"b":2}', '$.b'))"}),

        help_text("json_type", "Returns the type of a JSON value")
            .sql_function()
            .with_parameter({"X", "The JSON value to query"})
            .with_parameter(help_text{"P", "The path to the value"}.optional())
            .with_tags({"json"})
            .with_example(
                {"To get the type of a value",
                 R"(SELECT json_type('[null,1,2.1,"three",{"four":5}]'))"})
            .with_example(
                {"To get the type of an array element",
                 R"(SELECT json_type('[null,1,2.1,"three",{"four":5}]', '$[0]'))"})
            .with_example(
                {"To get the type of a string",
                 R"(SELECT json_type('[null,1,2.1,"three",{"four":5}]', '$[3]'))"}),

        help_text("json_valid", "Tests if the given value is valid JSON")
            .sql_function()
            .with_parameter({"X", "The value to check"})
            .with_tags({"json"})
            .with_example({"To check an empty string", "SELECT json_valid('')"})
            .with_example({"To check a string", R"(SELECT json_valid('"a"'))"}),

        help_text("json_quote",
                  "Returns the JSON representation of the given value, if it "
                  "is not already JSON")
            .sql_function()
            .with_parameter({"X", "The value to convert"})
            .with_tags({"json"})
            .with_example(
                {"To convert a string", "SELECT json_quote('Hello, World!')"})
            .with_example({"To pass through an existing JSON value",
                           R"(SELECT json_quote(json('"Hello, World!"')))"}),

        help_text("json_each",
                  "A table-valued-function that returns the children of the "
                  "top-level JSON value")
            .sql_table_valued_function()
            .with_parameter({"X", "The JSON value to query"})
            .with_parameter(
                help_text{"P", "The path to the value to query"}.optional())
            .with_result({"key",
                          "The array index for elements of an array or "
                          "property names of the object"})
            .with_result({"value", "The value for the current element"})
            .with_result({"type", "The type of the current element"})
            .with_result(
                {"atom",
                 "The SQL value of the element, if it is a primitive type"})
            .with_result({"fullkey", "The path to the current element"})
            .with_tags({"json"})
            .with_example(
                {"To iterate over an array",
                 R"(SELECT * FROM json_each('[null,1,"two",{"three":4.5}]'))"}),

        help_text("json_tree",
                  "A table-valued-function that recursively descends through a "
                  "JSON value")
            .sql_table_valued_function()
            .with_parameter({"X", "The JSON value to query"})
            .with_parameter(
                help_text{"P", "The path to the value to query"}.optional())
            .with_result({"key",
                          "The array index for elements of an array or "
                          "property names of the object"})
            .with_result({"value", "The value for the current element"})
            .with_result({"type", "The type of the current element"})
            .with_result(
                {"atom",
                 "The SQL value of the element, if it is a primitive type"})
            .with_result({"fullkey", "The path to the current element"})
            .with_result({"path", "The path to the container of this element"})
            .with_tags({"json"})
            .with_example(
                {"To iterate over an array",
                 R"(SELECT key,value,type,atom,fullkey,path FROM json_tree('[null,1,"two",{"three":4.5}]'))"}),

        help_text("text.contains", "Returns true if col contains sub")
            .prql_function()
            .with_parameter(
                help_text{"sub", "The substring to look for in col"})
            .with_parameter(help_text{"col", "The string to examine"})
            .with_example({
                "To check if 'Hello' contains 'lo'",
                "from [{s='Hello'}] | select { s=text.contains 'lo' s }",
                help_example::language::prql,
            })
            .with_example({
                "To check if 'Goodbye' contains 'lo'",
                "from [{s='Goodbye'}] | select { s=text.contains 'lo' s }",
                help_example::language::prql,
            }),
        help_text("text.ends_with", "Returns true if col ends with suffix")
            .prql_function()
            .with_parameter(
                help_text{"suffix", "The string to look for at the end of col"})
            .with_parameter(help_text{"col", "The string to examine"})
            .with_example({
                "To check if 'Hello' ends with 'lo'",
                "from [{s='Hello'}] | select { s=text.ends_with 'lo' s }",
                help_example::language::prql,
            })
            .with_example({
                "To check if 'Goodbye' ends with 'lo'",
                "from [{s='Goodbye'}] | select { s=text.ends_with 'lo' s }",
                help_example::language::prql,
            }),
        help_text("text.extract", "Extract a slice of a string")
            .prql_function()
            .with_parameter(help_text{
                "idx",
                "The starting index where the first character is index 1"})
            .with_parameter(help_text{"len", "The length of the slice"})
            .with_parameter(help_text{"str", "The string to extract from"})
            .with_example({
                "To extract a substring from s",
                "from [{s='Hello, World!'}] | select { s=text.extract 1 5 s }",
                help_example::language::prql,
            }),
        help_text("text.length", "Returns the number of characters in col")
            .prql_function()
            .with_parameter(help_text{"col", "The string to examine"})
            .with_example({
                "To count the number of characters in s",
                "from [{s='Hello, World!'}] | select { s=text.length s }",
                help_example::language::prql,
            }),
        help_text("text.lower", "Converts col to lowercase")
            .prql_function()
            .with_parameter(help_text{"col", "The string to convert"})
            .with_example({
                "To convert s to lowercase",
                "from [{s='HELLO'}] | select { s=text.lower s }",
                help_example::language::prql,
            }),
        help_text("text.ltrim", "Remove whitespace from the left side of col")
            .prql_function()
            .with_parameter(help_text{"col", "The string to trim"})
            .with_example({
                "To trim the left side of s",
                "from [{s='  HELLO  '}] | select { s=text.ltrim s }",
                help_example::language::prql,
            }),
        help_text("text.replace",
                  "Replace all occurrences of before with after in col")
            .prql_function()
            .with_parameter(help_text{"before", "The string to find"})
            .with_parameter(help_text{"after", "The replacement"})
            .with_parameter(help_text{"col", "The string to trim"})
            .with_example({
                "To erase foo in s",
                "from [{s='foobar'}] | select { s=text.replace 'foo' '' s }",
                help_example::language::prql,
            }),
        help_text("text.rtrim", "Remove whitespace from the right side of col")
            .prql_function()
            .with_parameter(help_text{"col", "The string to trim"})
            .with_example({
                "To trim the right side of s",
                "from [{s='  HELLO  '}] | select { s=text.rtrim s }",
                help_example::language::prql,
            }),
        help_text("text.starts_with", "Returns true if col starts with suffix")
            .prql_function()
            .with_parameter(help_text{
                "suffix", "The string to look for at the start of col"})
            .with_parameter(help_text{"col", "The string to examine"})
            .with_example({
                "To check if 'Hello' starts with 'lo'",
                "from [{s='Hello'}] | select { s=text.starts_with 'He' s }",
                help_example::language::prql,
            })
            .with_example({
                "To check if 'Goodbye' starts with 'lo'",
                "from [{s='Goodbye'}] | select { s=text.starts_with 'He' s }",
                help_example::language::prql,
            }),
        help_text("text.trim", "Remove whitespace from the both sides of col")
            .prql_function()
            .with_parameter(help_text{"col", "The string to trim"})
            .with_example({
                "To trim s",
                "from [{s='  HELLO  '}] | select { s=text.trim s }",
                help_example::language::prql,
            }),
        help_text("text.upper", "Converts col to uppercase")
            .prql_function()
            .with_parameter(help_text{"col", "The string to convert"})
            .with_example({
                "To convert s to uppercase",
                "from [{s='hello'}] | select { s=text.upper s }",
                help_example::language::prql,
            }),
    };

    if (!help_registration_done) {
        for (auto& ht : builtin_funcs) {
            switch (ht.ht_context) {
                case help_context_t::HC_PRQL_FUNCTION:
                    lnav::sql::prql_functions.emplace(ht.ht_name, &ht);
                    break;
                default:
                    sqlite_function_help.emplace(ht.ht_name, &ht);
                    break;
            }
            ht.index_tags();
        }
    }

    static help_text builtin_win_funcs[] = {
        help_text("row_number",
                  "Returns the number of the row within the current partition, "
                  "starting from 1.")
            .sql_function()
            .with_tags({"window"})
            .with_example({"To number messages from a process",
                           "SELECT row_number() OVER (PARTITION BY ex_procname "
                           "ORDER BY log_line) AS msg_num, ex_procname, "
                           "log_body FROM lnav_example_log"}),

        help_text("rank",
                  "Returns the row_number() of the first peer in each group "
                  "with gaps")
            .sql_function()
            .with_tags({"window"}),

        help_text("dense_rank",
                  "Returns the row_number() of the first peer in each group "
                  "without gaps")
            .sql_function()
            .with_tags({"window"}),

        help_text("percent_rank", "Returns (rank - 1) / (partition-rows - 1)")
            .sql_function()
            .with_tags({"window"}),

        help_text("cume_dist", "Returns the cumulative distribution")
            .sql_function()
            .with_tags({"window"}),

        help_text(
            "ntile",
            "Returns the number of the group that the current row is a part of")
            .sql_function()
            .with_parameter({"groups", "The number of groups"})
            .with_tags({"window"}),

        help_text("lag",
                  "Returns the result of evaluating the expression against the "
                  "previous row in the partition.")
            .sql_function()
            .with_parameter(
                {"expr", "The expression to execute over the previous row"})
            .with_parameter(
                help_text("offset",
                          "The offset from the current row in the partition")
                    .optional())
            .with_parameter(help_text("default",
                                      "The default value if the previous row "
                                      "does not exist instead of NULL")
                                .optional())
            .with_tags({"window"}),

        help_text("lead",
                  "Returns the result of evaluating the expression against the "
                  "next row in the partition.")
            .sql_function()
            .with_parameter(
                {"expr", "The expression to execute over the next row"})
            .with_parameter(
                help_text("offset",
                          "The offset from the current row in the partition")
                    .optional())
            .with_parameter(help_text("default",
                                      "The default value if the next row does "
                                      "not exist instead of NULL")
                                .optional())
            .with_tags({"window"}),

        help_text("first_value",
                  "Returns the result of evaluating the expression against the "
                  "first row in the window frame.")
            .sql_function()
            .with_parameter(
                {"expr", "The expression to execute over the first row"})
            .with_tags({"window"}),

        help_text("last_value",
                  "Returns the result of evaluating the expression against the "
                  "last row in the window frame.")
            .sql_function()
            .with_parameter(
                {"expr", "The expression to execute over the last row"})
            .with_tags({"window"}),

        help_text("nth_value",
                  "Returns the result of evaluating the expression against the "
                  "nth row in the window frame.")
            .sql_function()
            .with_parameter(
                {"expr", "The expression to execute over the nth row"})
            .with_parameter({"N", "The row number"})
            .with_tags({"window"}),
    };

    if (!help_registration_done) {
        for (auto& ht : builtin_win_funcs) {
            sqlite_function_help.insert(std::make_pair(ht.ht_name, &ht));
            ht.index_tags();
        }
    }

    static help_text idents[] = {
        help_text("ATTACH", "Attach a database file to the current connection.")
            .sql_keyword()
            .with_parameter(
                help_text("filename", "The path to the database file.")
                    .with_flag_name("DATABASE"))
            .with_parameter(help_text("schema-name",
                                      "The prefix for tables in this database.")
                                .with_flag_name("AS"))
            .with_example({"To attach the database file '/tmp/customers.db' "
                           "with the name customers",
                           "ATTACH DATABASE '/tmp/customers.db' AS customers"}),

        help_text("DETACH", "Detach a database from the current connection.")
            .sql_keyword()
            .with_parameter(help_text("schema-name",
                                      "The prefix for tables in this database.")
                                .with_flag_name("DATABASE"))
            .with_example({"To detach the database named 'customers'",
                           "DETACH DATABASE customers"}),

        help_text("CREATE", "Assign a name to a SELECT statement")
            .sql_keyword()
            .with_parameter(help_text("TEMP").optional())
            .with_parameter(help_text("").with_flag_name("VIEW"))
            .with_parameter(
                help_text("IF NOT EXISTS",
                          "Do not create the view if it already exists")
                    .optional())
            .with_parameter(
                help_text("schema-name.", "The database to create the view in")
                    .optional())
            .with_parameter(help_text("view-name", "The name of the view"))
            .with_parameter(
                help_text("select-stmt",
                          "The SELECT statement the view represents")
                    .with_flag_name("AS")),

        help_text("CREATE", "Create a table")
            .sql_keyword()
            .with_parameter(help_text("TEMP").optional())
            .with_parameter(help_text("").with_flag_name("TABLE"))
            .with_parameter(help_text("IF NOT EXISTS").optional())
            .with_parameter(help_text("schema-name.").optional())
            .with_parameter(help_text("table-name"))
            .with_parameter(help_text("select-stmt").with_flag_name("AS")),

        help_text("DELETE", "Delete rows from a table")
            .sql_keyword()
            .with_parameter(help_text("table-name", "The name of the table")
                                .with_flag_name("FROM"))
            .with_parameter(
                help_text("cond", "The conditions used to delete the rows.")
                    .with_flag_name("WHERE")
                    .optional()),

        help_text("DROP", "Drop an index")
            .sql_keyword()
            .with_parameter(help_text("").with_flag_name("INDEX"))
            .with_parameter(help_text("IF EXISTS").optional())
            .with_parameter(help_text("schema-name.").optional())
            .with_parameter(help_text("index-name")),

        help_text("DROP", "Drop a table")
            .sql_keyword()
            .with_parameter(help_text("").with_flag_name("TABLE"))
            .with_parameter(help_text("IF EXISTS").optional())
            .with_parameter(help_text("schema-name.").optional())
            .with_parameter(help_text("table-name")),

        help_text("DROP", "Drop a view")
            .sql_keyword()
            .with_parameter(help_text("").with_flag_name("VIEW"))
            .with_parameter(help_text("IF EXISTS").optional())
            .with_parameter(help_text("schema-name.").optional())
            .with_parameter(help_text("view-name")),

        help_text("DROP", "Drop a trigger")
            .sql_keyword()
            .with_parameter(help_text("").with_flag_name("TRIGGER"))
            .with_parameter(help_text("IF EXISTS").optional())
            .with_parameter(help_text("schema-name.").optional())
            .with_parameter(help_text("trigger-name")),

        help_text("INSERT", "Insert rows into a table")
            .sql_keyword()
            .with_parameter(help_text("").with_flag_name("INTO"))
            .with_parameter(help_text("schema-name.").optional())
            .with_parameter(help_text("table-name"))
            .with_parameter(
                help_text("column-name").with_grouping("(", ")").zero_or_more())
            .with_parameter(help_text("expr")
                                .with_flag_name("VALUES")
                                .with_grouping("(", ")")
                                .one_or_more())
            .with_example(
                {"To insert the pair containing 'MSG' and 'HELLO, WORLD!' into "
                 "the 'environ' table",
                 "INSERT INTO environ VALUES ('MSG', 'HELLO, WORLD!')"}),

        help_text("SELECT",
                  "Query the database and return zero or more rows of data.")
            .sql_keyword()
            .with_parameter(
                help_text(
                    "result-column",
                    "The expression used to generate a result for this column.")
                    .one_or_more())
            .with_parameter(help_text("table", "The table(s) to query for data")
                                .with_flag_name("FROM")
                                .zero_or_more())
            .with_parameter(
                help_text("cond",
                          "The conditions used to select the rows to return.")
                    .with_flag_name("WHERE")
                    .optional())
            .with_parameter(
                help_text("grouping-expr",
                          "The expression to use when grouping rows.")
                    .with_flag_name("GROUP BY")
                    .zero_or_more())
            .with_parameter(
                help_text("ordering-term",
                          "The values to use when ordering the result set.")
                    .with_flag_name("ORDER BY")
                    .zero_or_more())
            .with_parameter(
                help_text("limit-expr", "The maximum number of rows to return.")
                    .with_flag_name("LIMIT")
                    .zero_or_more())
            .with_example(
                {"To select all of the columns from the table 'syslog_log'",
                 "SELECT * FROM syslog_log"}),

        help_text("WITH",
                  "Create a temporary view that exists only for the duration "
                  "of a SQL statement.")
            .sql_keyword()
            .with_parameter(
                help_text("").with_flag_name("RECURSIVE").optional())
            .with_parameter(
                {"cte-table-name", "The name for the temporary table."})
            .with_parameter(help_text("select-stmt",
                                      "The SELECT statement used to populate "
                                      "the temporary table.")
                                .with_flag_name("AS")),

        help_text(
            "UPDATE",
            "Modify a subset of values in zero or more rows of the given table")
            .sql_keyword()
            .with_parameter(help_text("table", "The table to update"))
            .with_parameter(help_text("").with_flag_name("SET"))
            .with_parameter(
                help_text("column-name", "The columns in the table to update.")
                    .with_parameter(
                        help_text("expr",
                                  "The values to place into the column.")
                            .with_flag_name("="))
                    .one_or_more())
            .with_parameter(help_text("cond",
                                      "The condition used to determine whether "
                                      "a row should be updated.")
                                .with_flag_name("WHERE")
                                .optional())
            .with_example({
                "To mark the syslog message at line 40",
                "UPDATE syslog_log SET log_mark = 1 WHERE log_line = 40",
            }),

        help_text("CASE",
                  "Evaluate a series of expressions in order until one "
                  "evaluates to true and then return it's result.  "
                  "Similar to an IF-THEN-ELSE construct in other languages.")
            .sql_keyword()
            .with_parameter(help_text("base-expr",
                                      "The base expression that is used for "
                                      "comparison in the branches")
                                .optional())
            .with_parameter(
                help_text(
                    "cmp-expr",
                    "The expression to test if this branch should be taken")
                    .with_flag_name("WHEN")
                    .one_or_more()
                    .with_parameter(
                        help_text("then-expr", "The result for this branch.")
                            .with_flag_name("THEN")))
            .with_parameter(
                help_text("else-expr",
                          "The result of this CASE if no branches matched.")
                    .with_flag_name("ELSE")
                    .optional())
            .with_parameter(help_text("").with_flag_name("END"))
            .with_example({
                "To evaluate the number one and return the string 'one'",
                "SELECT CASE 1 WHEN 0 THEN 'zero' WHEN 1 THEN 'one' END",
            }),

        help_text("CAST",
                  "Convert the value of the given expression to a different "
                  "storage class specified by type-name.")
            .sql_function()
            .with_parameter({"expr", "The value to convert."})
            .with_parameter(
                help_text("type-name", "The name of the type to convert to.")
                    .with_flag_name("AS"))
            .with_example({
                "To cast the value 1.23 as an integer",
                "SELECT CAST(1.23 AS INTEGER)",
            }),

        help_text("expr", "Match an expression against a glob pattern.")
            .sql_infix()
            .with_parameter(help_text("NOT").optional())
            .with_parameter(
                help_text("pattern", "The glob pattern to match against.")
                    .with_flag_name("GLOB"))
            .with_example({
                "To check if a value matches the pattern '*.log'",
                "SELECT 'foobar.log' GLOB '*.log'",
            }),

        help_text("expr", "Match an expression against a text pattern.")
            .sql_infix()
            .with_parameter(help_text("NOT").optional())
            .with_parameter(
                help_text("pattern", "The pattern to match against.")
                    .with_flag_name("LIKE"))
            .with_example({
                "To check if a value matches the pattern 'Hello, %!'",
                "SELECT 'Hello, World!' LIKE 'Hello, %!'",
            }),

        help_text("expr", "Match an expression against a regular expression.")
            .sql_infix()
            .with_parameter(help_text("NOT").optional())
            .with_parameter(
                help_text("pattern", "The regular expression to match against.")
                    .with_flag_name("REGEXP"))
            .with_example({
                "To check if a value matches the pattern 'file-\\d+'",
                "SELECT 'file-23' REGEXP 'file-\\d+'",
            }),

        help_text("expr", "Assign a collating sequence to the expression.")
            .sql_infix()
            .with_parameter(
                help_text("collation-name", "The name of the collator.")
                    .with_flag_name("COLLATE"))
            .with_example({
                "To change the collation method for string comparisons",
                "SELECT ('a2' < 'a10'), ('a2' < 'a10' COLLATE "
                "naturalnocase)",
            }),

        help_text("expr", "Test if an expression is between two values.")
            .sql_infix()
            .with_parameter(help_text("NOT").optional())
            .with_parameter(
                help_text("low", "The low point").with_flag_name("BETWEEN"))
            .with_parameter(
                help_text("hi", "The high point").with_flag_name("AND"))
            .with_example({
                "To check if 3 is between 5 and 10",
                "SELECT 3 BETWEEN 5 AND 10",
            })
            .with_example({
                "To check if 10 is between 5 and 10",
                "SELECT 10 BETWEEN 5 AND 10",
            }),

        help_text("OVER", "Executes the preceding function over a window")
            .sql_keyword()
            .with_parameter(
                {"window-name", "The name of the window definition"}),

        help_text("OVER", "Executes the preceding function over a window")
            .sql_function()
            .with_parameter(help_text{
                "base-window-name",
                "The name of the window definition",
            }
                                .optional())
            .with_parameter(
                help_text{"expr", "The values to use for partitioning"}
                    .with_flag_name("PARTITION BY")
                    .zero_or_more())
            .with_parameter(help_text{
                "expr", "The values used to order the rows in the window"}
                                .with_flag_name("ORDER BY")
                                .zero_or_more())
            .with_parameter(help_text{
                "frame-spec",
                "Determines which output rows are read "
                "by an aggregate window function",
            }
                                .optional()),
    };

    if (!help_registration_done) {
        for (auto& ht : idents) {
            sqlite_function_help.insert(make_pair(toupper(ht.ht_name), &ht));
            for (const auto& param : ht.ht_parameters) {
                if (!param.ht_flag_name) {
                    continue;
                }
                sqlite_function_help.insert(
                    make_pair(toupper(param.ht_flag_name), &ht));
            }
        }
    }

    help_registration_done = true;

    return 0;
}
