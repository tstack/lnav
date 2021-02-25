/*
 * Written by Alexey Tourbin <at@altlinux.org>.
 *
 * The author has dedicated the code to the public domain.  Anyone is free
 * to copy, modify, publish, use, compile, sell, or distribute the original
 * code, either in source code form or as a compiled binary, for any purpose,
 * commercial or non-commercial, and by any means.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#include <unordered_map>

#include "pcrepp/pcrepp.hh"

#include "base/humanize.hh"
#include "base/string_util.hh"
#include "yajlpp/yajlpp.hh"
#include "column_namer.hh"
#include "yajl/api/yajl_gen.h"
#include "sqlite-extension-func.hh"
#include "data_scanner.hh"
#include "data_parser.hh"
#include "elem_to_json.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"
#include "safe/safe.h"
#include "spookyhash/SpookyV2.h"

#include "optional.hpp"
#include "mapbox/variant.hpp"

using namespace std;
using namespace mapbox;

typedef struct {
    shared_ptr<pcrepp> re2;
} cache_entry;

static cache_entry *find_re(const char *re)
{
    using safe_cache = safe::Safe<unordered_map<string, cache_entry>>;
    static safe_cache CACHE;

    safe::WriteAccess<safe_cache> wcache(CACHE);
    string re_str = re;
    auto iter = wcache->find(re_str);

    if (iter == wcache->end()) {
        cache_entry c;

        c.re2 = make_shared<pcrepp>(re_str);
        auto pair = wcache->insert(std::make_pair(re_str, c));

        iter = pair.first;
    }

    return &iter->second;
}

static bool regexp(const char *re, const char *str)
{
    cache_entry *reobj = find_re(re);
    pcre_context_static<30> pc;
    pcre_input pi(str);

    return reobj->re2->match(pc, pi);
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

    yajlpp_gen gen;
    yajl_gen_config(gen, yajl_gen_beautify, false);

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

    yajlpp_gen gen;
    yajl_gen_config(gen, yajl_gen_beautify, false);

    elements_to_json(gen, dp, &dp.dp_pairs);

    return json_string(gen);
}

static
string regexp_replace(const char *str, const char *re, const char *repl)
{
    cache_entry *reobj = find_re(re);

    return reobj->re2->replace(str, repl);
}

static
string spooky_hash(const vector<const char *> &args)
{
    byte_array<2, uint64> hash;
    SpookyHash context;

    context.Init(0, 0);
    for (const auto arg : args) {
        int64_t len = arg != nullptr ? strlen(arg) : 0;

        context.Update(&len, sizeof(len));
        if (arg == nullptr) {
            continue;
        }
        context.Update(arg, len);
    }
    context.Final(hash.out(0), hash.out(1));

    return hash.to_string();
}

static void sql_spooky_hash_step(sqlite3_context *context,
                                 int argc,
                                 sqlite3_value **argv)
{
    auto *hasher = (SpookyHash *)sqlite3_aggregate_context(context,
        sizeof(SpookyHash));

    for (int lpc = 0; lpc < argc; lpc++) {
        auto value = sqlite3_value_text(argv[lpc]);
        int64_t len = value != nullptr ? strlen((const char *) value) : 0;

        hasher->Update(&len, sizeof(len));
        if (value == nullptr) {
            continue;
        }
        hasher->Update(value, len);
    }
}

static void sql_spooky_hash_final(sqlite3_context *context)
{
    auto *hasher = (SpookyHash *)sqlite3_aggregate_context(
        context, sizeof(SpookyHash));

    if (hasher == nullptr) {
        sqlite3_result_null(context);
    } else {
        byte_array<2, uint64> hash;

        hasher->Final(hash.out(0), hash.out(1));

        string hex = hash.to_string();
        sqlite3_result_text(context, hex.c_str(), hex.length(),
                            SQLITE_TRANSIENT);
    }
}

struct sparkline_context {
    bool sc_initialized{true};
    double sc_max_value{0.0};
    std::vector<double> sc_values;
};

static void sparkline_step(sqlite3_context *context,
                           int argc,
                           sqlite3_value **argv)
{
    auto *sc = (sparkline_context *)
        sqlite3_aggregate_context(context, sizeof(sparkline_context));

    if (!sc->sc_initialized) {
        new (sc) sparkline_context;
    }

    if (argc == 0) {
        return;
    }

    sc->sc_values.push_back(sqlite3_value_double(argv[0]));
    sc->sc_max_value = std::max(sc->sc_max_value, sc->sc_values.back());

    if (argc >= 2) {
        sc->sc_max_value = std::max(sc->sc_max_value,
                                    sqlite3_value_double(argv[1]));
    }
}

static void sparkline_final(sqlite3_context *context)
{
    auto *sc = (sparkline_context *)
        sqlite3_aggregate_context(context, sizeof(sparkline_context));

    if (!sc->sc_initialized) {
        sqlite3_result_text(context, "", 0, SQLITE_STATIC);
        return;
    }

    auto retval = (char *) sqlite3_malloc(sc->sc_values.size() * 3 + 1);
    auto start = retval;

    for (const auto& value : sc->sc_values) {
        auto bar = humanize::sparkline(value, sc->sc_max_value);

        strcpy(start, bar.c_str());
        start += bar.length();
    }
    *start = '\0';

    sqlite3_result_text(context, retval, -1, sqlite3_free);
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
                .with_tags({"string", "regex"})
                .with_example({
                    "To capture the digits from the string '123'",
                    "SELECT regexp_match('(\\d+)', '123')"
                })
                .with_example({
                    "To capture a number and word into a JSON object with the properties 'col_0' and 'col_1'",
                    "SELECT regexp_match('(\\d+) (\\w+)', '123 four')"
                })
                .with_example({
                    "To capture a number and word into a JSON object with the named properties 'num' and 'str'",
                    "SELECT regexp_match('(?<num>\\d+) (?<str>\\w+)', '123 four')"
                })
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
                .with_tags({"string", "regex"})
                .with_example({
                    "To replace the word at the start of the string 'Hello, World!' with 'Goodbye'",
                    "SELECT regexp_replace('Hello, World!', '^(\\w+)', 'Goodbye')"
                })
                .with_example({
                    "To wrap alphanumeric words with angle brackets",
                    "SELECT regexp_replace('123 abc', '(\\w+)', '<\\1>')"
                })
        ),

        sqlite_func_adapter<decltype(&humanize::file_size), humanize::file_size>::builder(
            help_text("humanize_file_size",
                      "Format the given file size as a human-friendly string")
                .sql_function()
                .with_parameter({"value", "The file size to format"})
                .with_tags({"string"})
                .with_example({
                    "To format an amount",
                    "SELECT humanize_file_size(10 * 1024 * 1024)"
                })
        ),

        sqlite_func_adapter<decltype(&humanize::sparkline), humanize::sparkline>::builder(
            help_text("sparkline",
                      "Converts a numeric value on a range to a bar chart character")
                .sql_function()
                .with_parameter({"value", "The numeric value to convert"})
                .with_parameter(help_text(
                    "upper", "The upper bound of the numeric range (default: 100)")
                                    .optional())
                .with_tags({"string"})
                .with_example({
                    "To get the unicode block element for the value 32 in the "
                    "range of 0-128",
                    "SELECT sparkline(32, 128)"
                })
        ),

        sqlite_func_adapter<decltype(&extract), extract>::builder(
            help_text("extract",
                      "Automatically Parse and extract data from a string")
                .sql_function()
                .with_parameter({"str", "The string to parse"})
                .with_tags({"string"})
                .with_example({
                    "To extract key/value pairs from a string",
                    "SELECT extract('foo=1 bar=2 name=\"Rolo Tomassi\"')"
                })
                .with_example({
                    "To extract columnar data from a string",
                    "SELECT extract('1.0 abc 2.0')"
                })
        ),

        sqlite_func_adapter<decltype(
             static_cast<bool (*)(const char *, const char *)>(&startswith)),
            startswith>::builder(
            help_text("startswith",
                      "Test if a string begins with the given prefix")
                .sql_function()
                .with_parameter({"str", "The string to test"})
                .with_parameter({"prefix", "The prefix to check in the string"})
                .with_tags({"string"})
                .with_example({
                    "To test if the string 'foobar' starts with 'foo'",
                    "SELECT startswith('foobar', 'foo')"
                })
                .with_example({
                    "To test if the string 'foobar' starts with 'bar'",
                    "SELECT startswith('foobar', 'bar')"
                })
        ),

        sqlite_func_adapter<decltype(
            static_cast<bool (*)(const char *, const char *)>(&endswith)),
            endswith>::builder(
            help_text("endswith",
                      "Test if a string ends with the given suffix")
                .sql_function()
                .with_parameter({"str", "The string to test"})
                .with_parameter({"suffix", "The suffix to check in the string"})
                .with_tags({"string"})
                .with_example({
                    "To test if the string 'notbad.jpg' ends with '.jpg'",
                    "SELECT endswith('notbad.jpg', '.jpg')"
                })
                .with_example({
                    "To test if the string 'notbad.png' starts with '.jpg'",
                    "SELECT endswith('notbad.png', '.jpg')"
                })
        ),

        sqlite_func_adapter<decltype(&spooky_hash), spooky_hash>::builder(
            help_text("spooky_hash",
                      "Compute the hash value for the given arguments.")
                .sql_function()
                .with_parameter(help_text("str", "The string to hash")
                                    .one_or_more())
                .with_tags({"string"})
                .with_example({
                    "To produce a hash for the string 'Hello, World!'",
                    "SELECT spooky_hash('Hello, World!')"
                })
                .with_example({
                    "To produce a hash for the parameters where one is NULL",
                    "SELECT spooky_hash('Hello, World!', NULL)"
                })
                .with_example({
                    "To produce a hash for the parameters where one is an empty string",
                    "SELECT spooky_hash('Hello, World!', '')"
                })
                .with_example({
                    "To produce a hash for the parameters where one is a number",
                    "SELECT spooky_hash('Hello, World!', 123)"
                })
        ),

        {nullptr}
    };

    static struct FuncDefAgg str_agg_funcs[] = {
        {"group_spooky_hash", -1, 0,
            sql_spooky_hash_step, sql_spooky_hash_final,
            help_text("group_spooky_hash",
                      "Compute the hash value for the given arguments")
                .sql_agg_function()
                .with_parameter(help_text("str", "The string to hash")
                                    .one_or_more())
                .with_tags({"string"})
                .with_example({
                    "To produce a hash of all of the values of 'column1'",
                    "SELECT group_spooky_hash(column1) FROM (VALUES ('abc'), ('123'))"
                })
        },

        {
            "sparkline", -1, 0,
            sparkline_step, sparkline_final,
            help_text("sparkline",
                      "An aggregate function to convert numeric values to a "
                      "sparkline bar chart")
                .sql_agg_function()
                .with_parameter({"value", "The numeric values to chart"})
                .with_parameter(help_text(
                    "upper", "The upper bound of the numeric range.  "
                             "If not provided, the default is derived from "
                             "all of the provided values")
                                    .optional())
                .with_tags({"string"})
                .with_example({
                    "To chart the values in a JSON array",
                    "SELECT sparkline(value) FROM json_each('[0, 1, 2, 3, 4, 5, 6, 7, 8]')"
                })
        },

        {nullptr}
    };

    *basic_funcs = string_funcs;
    *agg_funcs = str_agg_funcs;

    return SQLITE_OK;
}
