/*
 * Written by Alexey Tourbin <at@altlinux.org>.
 *
 * The author has dedicated the code to the public domain.  Anyone is free
 * to copy, modify, publish, use, compile, sell, or distribute the original
 * code, either in source code form or as a compiled binary, for any purpose,
 * commercial or non-commercial, and by any means.
 */

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <pcrecpp.h>

#include <unordered_map>

#include "pcrepp.hh"

#include "yajlpp.hh"
#include "column_namer.hh"
#include "yajl/api/yajl_gen.h"
#include "sqlite-extension-func.hh"
#include "data_scanner.hh"
#include "data_parser.hh"
#include "elem_to_json.hh"
#include "vtab_module.hh"

#include "optional.hpp"
#include "mapbox/variant.hpp"

using namespace std;
using namespace mapbox;

typedef struct {
    shared_ptr<pcrecpp::RE> re;
    shared_ptr<pcrepp> re2;
} cache_entry;

static cache_entry *find_re(const char *re)
{
    static unordered_map<string, cache_entry> CACHE;

    string re_str = re;
    auto iter = CACHE.find(re_str);

    if (iter == CACHE.end()) {
        cache_entry c;

        c.re2 = make_shared<pcrepp>(re_str);
        c.re = make_shared<pcrecpp::RE>(re);
        if (!c.re->error().empty()) {
            auto_mem<char> e2(sqlite3_free);

            e2 = sqlite3_mprintf("%s: %s", re, c.re->error().c_str());
            throw new pcrepp::error(e2.in(), 0);
        }
        CACHE[re_str] = c;

        iter = CACHE.find(re_str);
    }

    return &iter->second;
}

static bool regexp(const char *re, const char *str)
{
    cache_entry *reobj = find_re(re);

    return reobj->re->PartialMatch(str);
}

static
util::variant<int64_t, double, const char*, string_fragment, json_string>
regexp_match(const char *re, const char *str)
{
    cache_entry *reobj = find_re(re);
    pcre_context_static<30> pc;
    pcre_input pi(str);
    pcrepp &extractor = *reobj->re2;

    if (extractor.get_capture_count() == 0) {
        throw pcrepp::error("regular expression does not have any captures");
    }

    if (!extractor.match(pc, pi)) {
        return static_cast<const char *>(nullptr);
    }

    auto_mem<yajl_gen_t> gen(yajl_gen_free);

    gen = yajl_gen_alloc(NULL);
    yajl_gen_config(gen.in(), yajl_gen_beautify, false);

    if (extractor.get_capture_count() == 1) {
        pcre_context::capture_t *cap = pc[0];
        const char *cap_start = pi.get_substr_start(cap);

        if (!cap->is_valid()) {
            return static_cast<const char *>(nullptr);
        }
        else {
            char *cap_copy = (char *)alloca(cap->length() + 1);
            long long int i_value;
            double d_value;
            int end_index;

            memcpy(cap_copy, cap_start, cap->length());
            cap_copy[cap->length()] = '\0';

            if (sscanf(cap_copy, "%lld%n", &i_value, &end_index) == 1 &&
                (end_index == cap->length())) {
                return (int64_t)i_value;
            }
            else if (sscanf(cap_copy, "%lf%n", &d_value, &end_index) == 1 &&
                     (end_index == cap->length())) {
                return d_value;
            }
            else {
                return string_fragment(str, cap->c_begin, cap->c_end);
            }
        }
    }
    else {
        yajlpp_map root_map(gen);
        column_namer cn;

        for (int lpc = 0; lpc < extractor.get_capture_count(); lpc++) {
            string colname = cn.add_column(extractor.name_for_capture(lpc));
            pcre_context::capture_t *cap = pc[lpc];

            yajl_gen_string(gen, colname);

            if (!cap->is_valid()) {
                yajl_gen_null(gen);
            }
            else {
                const char *cap_start = pi.get_substr_start(cap);
                char *cap_copy = (char *) alloca(cap->length() + 1);
                long long int i_value;
                double d_value;
                int end_index;

                memcpy(cap_copy, cap_start, cap->length());
                cap_copy[cap->length()] = '\0';

                if (sscanf(cap_copy, "%lld%n", &i_value, &end_index) == 1 &&
                    (end_index == cap->length())) {
                    yajl_gen_integer(gen, i_value);
                }
                else if (sscanf(cap_copy, "%lf%n", &d_value, &end_index) == 1 &&
                         (end_index == cap->length())) {
                    yajl_gen_number(gen, cap_start, cap->length());
                }
                else {
                    yajl_gen_pstring(gen, cap_start, cap->length());
                }
            }
        }
    }

    return json_string(gen);
#if 0
    sqlite3_result_text(ctx, (const char *) buf, len, SQLITE_TRANSIENT);
#ifdef HAVE_SQLITE3_VALUE_SUBTYPE
    sqlite3_result_subtype(ctx, JSON_SUBTYPE);
#endif
#endif
}

json_string extract(const char *str)
{
    data_scanner ds(str);
    data_parser dp(&ds);

    dp.parse();
    // dp.print(stderr, dp.dp_pairs);

    auto_mem<yajl_gen_t> gen(yajl_gen_free);

    gen = yajl_gen_alloc(NULL);
    yajl_gen_config(gen.in(), yajl_gen_beautify, false);

    elements_to_json(gen, dp, &dp.dp_pairs);

    return json_string(gen);
}

static
string regexp_replace(const char *str, const char *re, const char *repl)
{
    cache_entry *reobj = find_re(re);
    string dest(str);

    reobj->re->GlobalReplace(repl, &dest);
    return dest;
}

int string_extension_functions(struct FuncDef **basic_funcs,
                               struct FuncDefAgg **agg_funcs)
{
    static struct FuncDef string_funcs[] = {
        sqlite_func_adapter<decltype(&regexp), regexp>::builder(
            help_text("regexp",
                      "Test if a string matches a regular expression")
                .sql_function()
                .with_parameter({"re", "The regular expression to use"})
                .with_parameter({"str", "The string to test against the regular expression"})
        ),

        sqlite_func_adapter<decltype(&regexp_match), regexp_match>::builder(
            help_text("regexp_match",
                      "Match a string against a regular expression and return the capture groups as JSON.")
                .sql_function()
                .with_parameter({"re", "The regular expression to use"})
                .with_parameter({"str", "The string to test against the regular expression"})
                .with_example({"SELECT regexp_match('(\\d+)', '123')"})
                .with_example({"SELECT regexp_match('(\\d+) (\\w+)', '123 four')"})
                .with_example({"SELECT regexp_match('(?<num>\\d+) (?<str>\\w+)', '123 four')"})
        ),

        sqlite_func_adapter<decltype(&regexp_replace), regexp_replace>::builder(
            help_text("regexp_replace",
                      "Replace the parts of a string that match a regular expression.")
                .sql_function()
                .with_parameter({"str", "The string to perform replacements on"})
                .with_parameter({"re", "The regular expression to match"})
                .with_parameter({"repl", "The replacement string.  "
                    "You can reference capture groups with a backslash followed by the number of the "
                    "group, starting with 1."})
                .with_example({"SELECT regexp_replace('Hello, World!', '^(\\w+)', 'Goodbye')"})
                .with_example({"SELECT regexp_replace('123 abc', '(\\w+)', '<\\1>')"})
        ),

        sqlite_func_adapter<decltype(&extract), extract>::builder(
            help_text("extract",
                      "Automatically Parse and extract data from a string")
                .sql_function()
                .with_parameter({"str", "The string to parse"})
                .with_example({"SELECT extract('foo=1 bar=2 name=\"Rolo Tomassi\"')"})
                .with_example({"SELECT extract('1.0 abc 2.0')"})
        ),

        sqlite_func_adapter<decltype(
             static_cast<bool (*)(const char *, const char *)>(&startswith)),
            startswith>::builder(
            help_text("startswith",
                      "Test if a string begins with the given prefix")
                .sql_function()
                .with_parameter({"str", "The string to test"})
                .with_parameter({"prefix", "The prefix to check in the string"})
                .with_example({"SELECT startswith('foobar', 'foo')"})
                .with_example({"SELECT startswith('foobar', 'bar')"})
        ),

        sqlite_func_adapter<decltype(&endswith), endswith>::builder(
            help_text("endswith",
                      "Test if a string ends with the given suffix")
                .sql_function()
                .with_parameter({"str", "The string to test"})
                .with_parameter({"suffix", "The suffix to check in the string"})
                .with_example({"SELECT endswith('notbad.jpg', '.jpg')"})
                .with_example({"SELECT endswith('notbad.png', '.jpg')"})
        ),

        { NULL }
    };

    *basic_funcs = string_funcs;

    return SQLITE_OK;
}
