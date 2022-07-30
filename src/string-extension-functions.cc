/*
 * Written by Alexey Tourbin <at@altlinux.org>.
 *
 * The author has dedicated the code to the public domain.  Anyone is free
 * to copy, modify, publish, use, compile, sell, or distribute the original
 * code, either in source code form or as a compiled binary, for any purpose,
 * commercial or non-commercial, and by any means.
 */

#ifdef __CYGWIN__
#    include <alloca.h>
#endif

#include <unordered_map>

#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

#include "base/humanize.hh"
#include "base/lnav.gzip.hh"
#include "base/string_util.hh"
#include "column_namer.hh"
#include "config.h"
#include "data_parser.hh"
#include "data_scanner.hh"
#include "elem_to_json.hh"
#include "formats/logfmt/logfmt.parser.hh"
#include "libbase64.h"
#include "mapbox/variant.hpp"
#include "optional.hpp"
#include "pcrepp/pcrepp.hh"
#include "safe/safe.h"
#include "spookyhash/SpookyV2.h"
#include "sqlite-extension-func.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"
#include "yajl/api/yajl_gen.h"
#include "yajlpp/json_op.hh"
#include "yajlpp/yajlpp.hh"

using namespace mapbox;

struct cache_entry {
    std::shared_ptr<pcrepp> re2;
    std::shared_ptr<column_namer> cn{
        std::make_shared<column_namer>(column_namer::language::JSON)};
};

static cache_entry*
find_re(string_fragment re)
{
    using re_cache_t
        = std::unordered_map<string_fragment, cache_entry, frag_hasher>;
    static thread_local re_cache_t cache;

    auto iter = cache.find(re);
    if (iter == cache.end()) {
        cache_entry c;

        c.re2 = std::make_shared<pcrepp>(re.to_string());
        auto pair = cache.insert(
            std::make_pair(string_fragment::from_str(c.re2->get_pattern()), c));

        for (int lpc = 0; lpc < c.re2->get_capture_count(); lpc++) {
            c.cn->add_column(
                string_fragment::from_c_str(c.re2->name_for_capture(lpc)));
        }

        iter = pair.first;
    }

    return &iter->second;
}

static bool
regexp(string_fragment re, string_fragment str)
{
    cache_entry* reobj = find_re(re);
    pcre_context_static<30> pc;
    pcre_input pi(str);

    return reobj->re2->match(pc, pi);
}

static util::variant<int64_t, double, const char*, string_fragment, json_string>
regexp_match(string_fragment re, const char* str)
{
    cache_entry* reobj = find_re(re);
    pcre_context_static<30> pc;
    pcre_input pi(str);
    pcrepp& extractor = *reobj->re2;

    if (extractor.get_capture_count() == 0) {
        throw pcrepp::error("regular expression does not have any captures");
    }

    if (!extractor.match(pc, pi, PCRE_NO_UTF8_CHECK)) {
        return static_cast<const char*>(nullptr);
    }

    yajlpp_gen gen;
    yajl_gen_config(gen, yajl_gen_beautify, false);

    if (extractor.get_capture_count() == 1) {
        pcre_context::capture_t* cap = pc[0];
        const char* cap_start = pi.get_substr_start(cap);

        if (!cap->is_valid()) {
            return static_cast<const char*>(nullptr);
        }

        char* cap_copy = (char*) alloca(cap->length() + 1);
        long long int i_value;
        double d_value;
        int end_index;

        memcpy(cap_copy, cap_start, cap->length());
        cap_copy[cap->length()] = '\0';

        if (sscanf(cap_copy, "%lld%n", &i_value, &end_index) == 1
            && (end_index == cap->length()))
        {
            return (int64_t) i_value;
        }
        if (sscanf(cap_copy, "%lf%n", &d_value, &end_index) == 1
            && (end_index == cap->length()))
        {
            return d_value;
        }
        return string_fragment(str, cap->c_begin, cap->c_end);
    } else {
        yajlpp_map root_map(gen);

        for (int lpc = 0; lpc < extractor.get_capture_count(); lpc++) {
            const auto& colname = reobj->cn->cn_names[lpc];
            const auto* cap = pc[lpc];

            yajl_gen_pstring(gen, colname.data(), colname.length());

            if (!cap->is_valid()) {
                yajl_gen_null(gen);
            } else {
                const char* cap_start = pi.get_substr_start(cap);
                char* cap_copy = (char*) alloca(cap->length() + 1);
                long long int i_value;
                double d_value;
                int end_index;

                memcpy(cap_copy, cap_start, cap->length());
                cap_copy[cap->length()] = '\0';

                if (sscanf(cap_copy, "%lld%n", &i_value, &end_index) == 1
                    && (end_index == cap->length()))
                {
                    yajl_gen_integer(gen, i_value);
                } else if (sscanf(cap_copy, "%lf%n", &d_value, &end_index) == 1
                           && (end_index == cap->length()))
                {
                    yajl_gen_number(gen, cap_start, cap->length());
                } else {
                    yajl_gen_pstring(gen, cap_start, cap->length());
                }
            }
        }
    }

    return json_string(gen);
#if 0
    sqlite3_result_text(ctx, (const char *) buf, len, SQLITE_TRANSIENT);
#    ifdef HAVE_SQLITE3_VALUE_SUBTYPE
    sqlite3_result_subtype(ctx, JSON_SUBTYPE);
#    endif
#endif
}

json_string
extract(const char* str)
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

json_string
logfmt2json(string_fragment line)
{
    logfmt::parser p(line);
    yajlpp_gen gen;
    yajl_gen_config(gen, yajl_gen_beautify, false);

    {
        yajlpp_map root(gen);
        bool done = false;

        while (!done) {
            auto pair = p.step();

            done = pair.match(
                [](const logfmt::parser::end_of_input& eoi) { return true; },
                [&root, &gen](const logfmt::parser::kvpair& kvp) {
                    root.gen(kvp.first);

                    kvp.second.match(
                        [&root](const logfmt::parser::bool_value& bv) {
                            root.gen(bv.bv_value);
                        },
                        [&root](const logfmt::parser::int_value& iv) {
                            root.gen(iv.iv_value);
                        },
                        [&root](const logfmt::parser::float_value& fv) {
                            root.gen(fv.fv_value);
                        },
                        [&root, &gen](const logfmt::parser::quoted_value& qv) {
                            auto_mem<yajl_handle_t> parse_handle(yajl_free);
                            json_ptr jp("");
                            json_op jo(jp);

                            jo.jo_ptr_callbacks = json_op::gen_callbacks;
                            jo.jo_ptr_data = gen;
                            parse_handle.reset(yajl_alloc(
                                &json_op::ptr_callbacks, nullptr, &jo));

                            const auto* json_in
                                = (const unsigned char*) qv.qv_value.data();
                            auto json_len = qv.qv_value.length();

                            if (yajl_parse(parse_handle.in(), json_in, json_len)
                                    != yajl_status_ok
                                || yajl_complete_parse(parse_handle.in())
                                    != yajl_status_ok)
                            {
                                root.gen(qv.qv_value);
                            }
                        },
                        [&root](const logfmt::parser::unquoted_value& uv) {
                            root.gen(uv.uv_value);
                        });

                    return false;
                },
                [](const logfmt::parser::error& e) -> bool {
                    throw sqlite_func_error("Invalid logfmt: {}", e.e_msg);
                });
        }
    }

    return json_string(gen);
}

static std::string
regexp_replace(const char* str, string_fragment re, const char* repl)
{
    cache_entry* reobj = find_re(re);

    return reobj->re2->replace(str, repl);
}

static std::string
spooky_hash(const std::vector<const char*>& args)
{
    byte_array<2, uint64> hash;
    SpookyHash context;

    context.Init(0, 0);
    for (const auto* const arg : args) {
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

static void
sql_spooky_hash_step(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    auto* hasher
        = (SpookyHash*) sqlite3_aggregate_context(context, sizeof(SpookyHash));

    for (int lpc = 0; lpc < argc; lpc++) {
        const auto* value = sqlite3_value_text(argv[lpc]);
        int64_t len = value != nullptr ? strlen((const char*) value) : 0;

        hasher->Update(&len, sizeof(len));
        if (value == nullptr) {
            continue;
        }
        hasher->Update(value, len);
    }
}

static void
sql_spooky_hash_final(sqlite3_context* context)
{
    auto* hasher
        = (SpookyHash*) sqlite3_aggregate_context(context, sizeof(SpookyHash));

    if (hasher == nullptr) {
        sqlite3_result_null(context);
    } else {
        byte_array<2, uint64> hash;

        hasher->Final(hash.out(0), hash.out(1));

        auto hex = hash.to_string();
        sqlite3_result_text(
            context, hex.c_str(), hex.length(), SQLITE_TRANSIENT);
    }
}

struct sparkline_context {
    bool sc_initialized{true};
    double sc_max_value{0.0};
    std::vector<double> sc_values;
};

static void
sparkline_step(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    auto* sc = (sparkline_context*) sqlite3_aggregate_context(
        context, sizeof(sparkline_context));

    if (!sc->sc_initialized) {
        new (sc) sparkline_context;
    }

    if (argc == 0) {
        return;
    }

    sc->sc_values.push_back(sqlite3_value_double(argv[0]));
    sc->sc_max_value = std::max(sc->sc_max_value, sc->sc_values.back());

    if (argc >= 2) {
        sc->sc_max_value
            = std::max(sc->sc_max_value, sqlite3_value_double(argv[1]));
    }
}

static void
sparkline_final(sqlite3_context* context)
{
    auto* sc = (sparkline_context*) sqlite3_aggregate_context(
        context, sizeof(sparkline_context));

    if (!sc->sc_initialized) {
        sqlite3_result_text(context, "", 0, SQLITE_STATIC);
        return;
    }

    auto* retval = (char*) malloc(sc->sc_values.size() * 3 + 1);
    auto* start = retval;

    for (const auto& value : sc->sc_values) {
        auto bar = humanize::sparkline(value, sc->sc_max_value);

        strcpy(start, bar.c_str());
        start += bar.length();
    }
    *start = '\0';

    sqlite3_result_text(context, retval, -1, free);

    sc->~sparkline_context();
}

nonstd::optional<util::variant<blob_auto_buffer, sqlite3_int64, double>>
sql_gunzip(sqlite3_value* val)
{
    switch (sqlite3_value_type(val)) {
        case SQLITE3_TEXT:
        case SQLITE_BLOB: {
            const auto* buffer = sqlite3_value_blob(val);
            auto len = sqlite3_value_bytes(val);

            if (!lnav::gzip::is_gzipped((const char*) buffer, len)) {
                return blob_auto_buffer{
                    auto_buffer::from((const char*) buffer, len)};
            }

            auto res = lnav::gzip::uncompress("", buffer, len);

            if (res.isErr()) {
                throw sqlite_func_error("unable to uncompress -- {}",
                                        res.unwrapErr());
            }

            return blob_auto_buffer{res.unwrap()};
        }
        case SQLITE_INTEGER:
            return sqlite3_value_int64(val);
        case SQLITE_FLOAT:
            return sqlite3_value_double(val);
    }

    return nonstd::nullopt;
}

nonstd::optional<blob_auto_buffer>
sql_gzip(sqlite3_value* val)
{
    switch (sqlite3_value_type(val)) {
        case SQLITE3_TEXT:
        case SQLITE_BLOB: {
            const auto* buffer = sqlite3_value_blob(val);
            auto len = sqlite3_value_bytes(val);
            auto res = lnav::gzip::compress(buffer, len);

            if (res.isErr()) {
                throw sqlite_func_error("unable to compress -- {}",
                                        res.unwrapErr());
            }

            return blob_auto_buffer{res.unwrap()};
        }
        case SQLITE_INTEGER:
        case SQLITE_FLOAT: {
            const auto* buffer = sqlite3_value_text(val);
            auto res
                = lnav::gzip::compress(buffer, strlen((const char*) buffer));

            if (res.isErr()) {
                throw sqlite_func_error("unable to compress -- {}",
                                        res.unwrapErr());
            }

            return blob_auto_buffer{res.unwrap()};
        }
    }

    return nonstd::nullopt;
}

static nonstd::optional<text_auto_buffer>
sql_base64_encode(sqlite3_value* value)
{
    switch (sqlite3_value_type(value)) {
        case SQLITE_NULL: {
            return nonstd::nullopt;
        }
        case SQLITE_BLOB: {
            const auto* blob
                = static_cast<const char*>(sqlite3_value_blob(value));
            auto blob_len = sqlite3_value_bytes(value);
            auto buf = auto_buffer::alloc((blob_len * 5) / 3);
            size_t outlen = buf.capacity();

            base64_encode(blob, blob_len, buf.in(), &outlen, 0);
            buf.resize(outlen);
            return text_auto_buffer{std::move(buf)};
        }
        default: {
            const auto* text = (const char*) sqlite3_value_text(value);
            auto text_len = sqlite3_value_bytes(value);
            auto buf = auto_buffer::alloc((text_len * 5) / 3);
            size_t outlen = buf.capacity();

            base64_encode(text, text_len, buf.in(), &outlen, 0);
            buf.resize(outlen);
            return text_auto_buffer{std::move(buf)};
        }
    }
}

static blob_auto_buffer
sql_base64_decode(string_fragment str)
{
    auto buf = auto_buffer::alloc(str.length());
    auto outlen = buf.capacity();
    base64_decode(str.data(), str.length(), buf.in(), &outlen, 0);
    buf.resize(outlen);

    return blob_auto_buffer{std::move(buf)};
}

std::string
sql_humanize_file_size(file_ssize_t value)
{
    return humanize::file_size(value, humanize::alignment::columnar);
}

int
string_extension_functions(struct FuncDef** basic_funcs,
                           struct FuncDefAgg** agg_funcs)
{
    static struct FuncDef string_funcs[] = {
        sqlite_func_adapter<decltype(&regexp), regexp>::builder(
            help_text("regexp", "Test if a string matches a regular expression")
                .sql_function()
                .with_parameter({"re", "The regular expression to use"})
                .with_parameter({
                    "str",
                    "The string to test against the regular expression",
                })),

        sqlite_func_adapter<decltype(&regexp_match), regexp_match>::builder(
            help_text("regexp_match",
                      "Match a string against a regular expression and return "
                      "the capture groups as JSON.")
                .sql_function()
                .with_parameter({"re", "The regular expression to use"})
                .with_parameter({
                    "str",
                    "The string to test against the regular expression",
                })
                .with_tags({"string", "regex"})
                .with_example({
                    "To capture the digits from the string '123'",
                    "SELECT regexp_match('(\\d+)', '123')",
                })
                .with_example({
                    "To capture a number and word into a JSON object with the "
                    "properties 'col_0' and 'col_1'",
                    "SELECT regexp_match('(\\d+) (\\w+)', '123 four')",
                })
                .with_example({
                    "To capture a number and word into a JSON object with the "
                    "named properties 'num' and 'str'",
                    "SELECT regexp_match('(?<num>\\d+) (?<str>\\w+)', '123 "
                    "four')",
                })),

        sqlite_func_adapter<decltype(&regexp_replace), regexp_replace>::builder(
            help_text("regexp_replace",
                      "Replace the parts of a string that match a regular "
                      "expression.")
                .sql_function()
                .with_parameter(
                    {"str", "The string to perform replacements on"})
                .with_parameter({"re", "The regular expression to match"})
                .with_parameter({
                    "repl",
                    "The replacement string.  "
                    "You can reference capture groups with a "
                    "backslash followed by the number of the "
                    "group, starting with 1.",
                })
                .with_tags({"string", "regex"})
                .with_example({
                    "To replace the word at the start of the string "
                    "'Hello, World!' with 'Goodbye'",
                    "SELECT regexp_replace('Hello, World!', "
                    "'^(\\w+)', 'Goodbye')",
                })
                .with_example({
                    "To wrap alphanumeric words with angle brackets",
                    "SELECT regexp_replace('123 abc', '(\\w+)', '<\\1>')",
                })),

        sqlite_func_adapter<decltype(&sql_humanize_file_size),
                            sql_humanize_file_size>::
            builder(help_text(
                        "humanize_file_size",
                        "Format the given file size as a human-friendly string")
                        .sql_function()
                        .with_parameter({"value", "The file size to format"})
                        .with_tags({"string"})
                        .with_example({
                            "To format an amount",
                            "SELECT humanize_file_size(10 * 1024 * 1024)",
                        })),

        sqlite_func_adapter<decltype(&humanize::sparkline),
                            humanize::sparkline>::
            builder(
                help_text("sparkline",
                          "Function used to generate a sparkline bar chart.  "
                          "The non-aggregate version converts a single numeric "
                          "value on a range to a bar chart character.  The "
                          "aggregate version returns a string with a bar "
                          "character for every numeric input")
                    .sql_function()
                    .with_parameter({"value", "The numeric value to convert"})
                    .with_parameter(help_text("upper",
                                              "The upper bound of the numeric "
                                              "range.  The non-aggregate "
                                              "version defaults to 100.  The "
                                              "aggregate version uses the "
                                              "largest value in the inputs.")
                                        .optional())
                    .with_tags({"string"})
                    .with_example({
                        "To get the unicode block element for the "
                        "value 32 in the "
                        "range of 0-128",
                        "SELECT sparkline(32, 128)",
                    })
                    .with_example({
                        "To chart the values in a JSON array",
                        "SELECT sparkline(value) FROM json_each('[0, 1, 2, 3, "
                        "4, 5, 6, 7, 8]')",
                    })),

        sqlite_func_adapter<decltype(&extract), extract>::builder(
            help_text("extract",
                      "Automatically Parse and extract data from a string")
                .sql_function()
                .with_parameter({"str", "The string to parse"})
                .with_tags({"string"})
                .with_example({
                    "To extract key/value pairs from a string",
                    "SELECT extract('foo=1 bar=2 name=\"Rolo Tomassi\"')",
                })
                .with_example({
                    "To extract columnar data from a string",
                    "SELECT extract('1.0 abc 2.0')",
                })),

        sqlite_func_adapter<decltype(&logfmt2json), logfmt2json>::builder(
            help_text("logfmt2json",
                      "Convert a logfmt-encoded string into JSON")
                .sql_function()
                .with_parameter({"str", "The logfmt message to parse"})
                .with_tags({"string"})
                .with_example({
                    "To extract key/value pairs from a log message",
                    "SELECT logfmt2json('foo=1 bar=2 name=\"Rolo Tomassi\"')",
                })),

        sqlite_func_adapter<
            decltype(static_cast<bool (*)(const char*, const char*)>(
                &startswith)),
            startswith>::
            builder(help_text("startswith",
                              "Test if a string begins with the given prefix")
                        .sql_function()
                        .with_parameter({"str", "The string to test"})
                        .with_parameter(
                            {"prefix", "The prefix to check in the string"})
                        .with_tags({"string"})
                        .with_example({
                            "To test if the string 'foobar' starts with 'foo'",
                            "SELECT startswith('foobar', 'foo')",
                        })
                        .with_example({
                            "To test if the string 'foobar' starts with 'bar'",
                            "SELECT startswith('foobar', 'bar')",
                        })),

        sqlite_func_adapter<decltype(static_cast<bool (*)(
                                         const char*, const char*)>(&endswith)),
                            endswith>::
            builder(
                help_text("endswith",
                          "Test if a string ends with the given suffix")
                    .sql_function()
                    .with_parameter({"str", "The string to test"})
                    .with_parameter(
                        {"suffix", "The suffix to check in the string"})
                    .with_tags({"string"})
                    .with_example({
                        "To test if the string 'notbad.jpg' ends with '.jpg'",
                        "SELECT endswith('notbad.jpg', '.jpg')",
                    })
                    .with_example({
                        "To test if the string 'notbad.png' starts with '.jpg'",
                        "SELECT endswith('notbad.png', '.jpg')",
                    })),

        sqlite_func_adapter<decltype(&spooky_hash), spooky_hash>::builder(
            help_text("spooky_hash",
                      "Compute the hash value for the given arguments.")
                .sql_function()
                .with_parameter(
                    help_text("str", "The string to hash").one_or_more())
                .with_tags({"string"})
                .with_example({
                    "To produce a hash for the string 'Hello, World!'",
                    "SELECT spooky_hash('Hello, World!')",
                })
                .with_example({
                    "To produce a hash for the parameters where one is NULL",
                    "SELECT spooky_hash('Hello, World!', NULL)",
                })
                .with_example({
                    "To produce a hash for the parameters where one "
                    "is an empty string",
                    "SELECT spooky_hash('Hello, World!', '')",
                })
                .with_example({
                    "To produce a hash for the parameters where one "
                    "is a number",
                    "SELECT spooky_hash('Hello, World!', 123)",
                })),

        sqlite_func_adapter<decltype(&sql_gunzip), sql_gunzip>::builder(
            help_text("gunzip", "Decompress a gzip file")
                .sql_function()
                .with_parameter(
                    help_text("b", "The blob to decompress").one_or_more())
                .with_tags({"string"})),

        sqlite_func_adapter<decltype(&sql_gzip), sql_gzip>::builder(
            help_text("gzip", "Compress a string into a gzip file")
                .sql_function()
                .with_parameter(
                    help_text("value", "The value to compress").one_or_more())
                .with_tags({"string"})),

        sqlite_func_adapter<decltype(&sql_base64_encode), sql_base64_encode>::
            builder(
                help_text("base64_encode", "Base-64 encode the given value")
                    .sql_function()
                    .with_parameter(help_text("value", "The value to encode"))
                    .with_tags({"string"})
                    .with_example({
                        "To encode 'Hello, World!'",
                        "SELECT base64_encode('Hello, World!')",
                    })),

        sqlite_func_adapter<decltype(&sql_base64_decode), sql_base64_decode>::
            builder(
                help_text("base64_encode", "Base-64 decode the given value")
                    .sql_function()
                    .with_parameter(help_text("value", "The value to decode"))
                    .with_tags({"string"})),

        {nullptr},
    };

    static struct FuncDefAgg str_agg_funcs[] = {
        {
            "group_spooky_hash",
            -1,
            0,
            sql_spooky_hash_step,
            sql_spooky_hash_final,
            help_text("group_spooky_hash",
                      "Compute the hash value for the given arguments")
                .sql_agg_function()
                .with_parameter(
                    help_text("str", "The string to hash").one_or_more())
                .with_tags({"string"})
                .with_example({
                    "To produce a hash of all of the values of 'column1'",
                    "SELECT group_spooky_hash(column1) FROM (VALUES ('abc'), "
                    "('123'))",
                }),
        },

        {
            "sparkline",
            -1,
            0,
            sparkline_step,
            sparkline_final,
        },

        {nullptr},
    };

    *basic_funcs = string_funcs;
    *agg_funcs = str_agg_funcs;

    return SQLITE_OK;
}
