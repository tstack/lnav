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
#include "pcrepp/pcre2pp.hh"
#include "pretty_printer.hh"
#include "safe/safe.h"
#include "scn/scn.h"
#include "spookyhash/SpookyV2.h"
#include "sqlite-extension-func.hh"
#include "text_anonymizer.hh"
#include "view_curses.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"
#include "yajl/api/yajl_gen.h"
#include "yajlpp/json_op.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

#if defined(HAVE_LIBCURL)
#    include <curl/curl.h>
#endif

enum class encode_algo {
    base64,
    hex,
    uri,
};

template<>
struct from_sqlite<encode_algo> {
    inline encode_algo operator()(int argc, sqlite3_value** val, int argi)
    {
        const char* algo_name = (const char*) sqlite3_value_text(val[argi]);

        if (strcasecmp(algo_name, "base64") == 0) {
            return encode_algo::base64;
        }
        if (strcasecmp(algo_name, "hex") == 0) {
            return encode_algo::hex;
        }
        if (strcasecmp(algo_name, "uri") == 0) {
            return encode_algo::uri;
        }

        throw from_sqlite_conversion_error("value of 'base64', 'hex', or 'uri'",
                                           argi);
    }
};

namespace {

struct cache_entry {
    std::shared_ptr<lnav::pcre2pp::code> re2;
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
        auto compile_res = lnav::pcre2pp::code::from(re);
        if (compile_res.isErr()) {
            const static intern_string_t SRC = intern_string::lookup("arg");

            throw lnav::console::to_user_message(SRC, compile_res.unwrapErr());
        }

        cache_entry c;

        c.re2 = compile_res.unwrap().to_shared();
        auto pair = cache.insert(
            std::make_pair(string_fragment::from_str(c.re2->get_pattern()), c));

        for (size_t lpc = 0; lpc < c.re2->get_capture_count(); lpc++) {
            c.cn->add_column(string_fragment::from_c_str(
                c.re2->get_name_for_capture(lpc + 1)));
        }

        iter = pair.first;
    }

    return &iter->second;
}

bool
regexp(string_fragment re, string_fragment str)
{
    auto* reobj = find_re(re);

    return reobj->re2->find_in(str).ignore_error().has_value();
}

mapbox::util::
    variant<int64_t, double, const char*, string_fragment, json_string>
    regexp_match(string_fragment re, string_fragment str)
{
    auto* reobj = find_re(re);
    auto& extractor = *reobj->re2;

    if (extractor.get_capture_count() == 0) {
        throw std::runtime_error(
            "regular expression does not have any captures");
    }

    auto md = extractor.create_match_data();
    auto match_res = extractor.capture_from(str).into(md).matches();
    if (match_res.is<lnav::pcre2pp::matcher::not_found>()) {
        return static_cast<const char*>(nullptr);
    }
    if (match_res.is<lnav::pcre2pp::matcher::error>()) {
        auto err = match_res.get<lnav::pcre2pp::matcher::error>();

        throw std::runtime_error(err.get_message());
    }

    yajlpp_gen gen;
    yajl_gen_config(gen, yajl_gen_beautify, false);

    if (extractor.get_capture_count() == 1) {
        auto cap = md[1];

        if (!cap) {
            return static_cast<const char*>(nullptr);
        }

        auto scan_int_res = scn::scan_value<int64_t>(cap->to_string_view());
        if (scan_int_res && scan_int_res.empty()) {
            return scan_int_res.value();
        }

        auto scan_float_res = scn::scan_value<double>(cap->to_string_view());
        if (scan_float_res && scan_float_res.empty()) {
            return scan_float_res.value();
        }

        return cap.value();
    } else {
        yajlpp_map root_map(gen);

        for (size_t lpc = 0; lpc < extractor.get_capture_count(); lpc++) {
            const auto& colname = reobj->cn->cn_names[lpc];
            const auto cap = md[lpc + 1];

            yajl_gen_pstring(gen, colname.data(), colname.length());

            if (!cap) {
                yajl_gen_null(gen);
            } else {
                auto scan_int_res
                    = scn::scan_value<int64_t>(cap->to_string_view());
                if (scan_int_res && scan_int_res.empty()) {
                    yajl_gen_integer(gen, scan_int_res.value());
                } else {
                    auto scan_float_res
                        = scn::scan_value<double>(cap->to_string_view());
                    if (scan_float_res && scan_float_res.empty()) {
                        yajl_gen_number(gen, cap->data(), cap->length());
                    } else {
                        yajl_gen_pstring(gen, cap->data(), cap->length());
                    }
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

static json_string
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

std::string
regexp_replace(string_fragment str, string_fragment re, const char* repl)
{
    auto* reobj = find_re(re);

    return reobj->re2->replace(str, repl);
}

std::string
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

void
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

    auto retval = auto_mem<char>::malloc(sc->sc_values.size() * 3 + 1);
    auto* start = retval.in();

    for (const auto& value : sc->sc_values) {
        auto bar = humanize::sparkline(value, sc->sc_max_value);

        strcpy(start, bar.c_str());
        start += bar.length();
    }
    *start = '\0';

    to_sqlite(context, std::move(retval));

    sc->~sparkline_context();
}

std::optional<mapbox::util::variant<blob_auto_buffer, sqlite3_int64, double>>
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

    return std::nullopt;
}

std::optional<blob_auto_buffer>
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

    return std::nullopt;
}

#if defined(HAVE_LIBCURL)
static CURL*
get_curl_easy()
{
    static struct curl_wrapper {
        curl_wrapper() { this->cw_value = curl_easy_init(); }

        auto_mem<CURL> cw_value{curl_easy_cleanup};
    } retval;

    return retval.cw_value.in();
}
#endif

static mapbox::util::variant<text_auto_buffer, auto_mem<char>, null_value_t>
sql_encode(sqlite3_value* value, encode_algo algo)
{
    switch (sqlite3_value_type(value)) {
        case SQLITE_NULL: {
            return null_value_t{};
        }
        case SQLITE_BLOB: {
            const auto* blob
                = static_cast<const char*>(sqlite3_value_blob(value));
            auto blob_len = sqlite3_value_bytes(value);

            switch (algo) {
                case encode_algo::base64: {
                    auto buf = auto_buffer::alloc((blob_len * 5) / 3);
                    auto outlen = buf.capacity();

                    base64_encode(blob, blob_len, buf.in(), &outlen, 0);
                    buf.resize(outlen);
                    return text_auto_buffer{std::move(buf)};
                }
                case encode_algo::hex: {
                    auto buf = auto_buffer::alloc(blob_len * 2 + 1);

                    for (int lpc = 0; lpc < blob_len; lpc++) {
                        fmt::format_to(std::back_inserter(buf),
                                       FMT_STRING("{:x}"),
                                       blob[lpc]);
                    }

                    return text_auto_buffer{std::move(buf)};
                }
#if defined(HAVE_LIBCURL)
                case encode_algo::uri: {
                    auto_mem<char> retval(curl_free);

                    retval = curl_easy_escape(get_curl_easy(), blob, blob_len);
                    return std::move(retval);
                }
#endif
            }
        }
        default: {
            const auto* text = (const char*) sqlite3_value_text(value);
            auto text_len = sqlite3_value_bytes(value);

            switch (algo) {
                case encode_algo::base64: {
                    auto buf = auto_buffer::alloc((text_len * 5) / 3);
                    size_t outlen = buf.capacity();

                    base64_encode(text, text_len, buf.in(), &outlen, 0);
                    buf.resize(outlen);
                    return text_auto_buffer{std::move(buf)};
                }
                case encode_algo::hex: {
                    auto buf = auto_buffer::alloc(text_len * 2 + 1);

                    for (int lpc = 0; lpc < text_len; lpc++) {
                        fmt::format_to(std::back_inserter(buf),
                                       FMT_STRING("{:02x}"),
                                       text[lpc]);
                    }

                    return text_auto_buffer{std::move(buf)};
                }
#if defined(HAVE_LIBCURL)
                case encode_algo::uri: {
                    auto_mem<char> retval(curl_free);

                    retval = curl_easy_escape(get_curl_easy(), text, text_len);
                    return std::move(retval);
                }
#endif
            }
        }
    }
    ensure(false);
}

static mapbox::util::variant<blob_auto_buffer, auto_mem<char>>
sql_decode(string_fragment str, encode_algo algo)
{
    switch (algo) {
        case encode_algo::base64: {
            auto buf = auto_buffer::alloc(str.length());
            auto outlen = buf.capacity();
            base64_decode(str.data(), str.length(), buf.in(), &outlen, 0);
            buf.resize(outlen);

            return blob_auto_buffer{std::move(buf)};
        }
        case encode_algo::hex: {
            auto buf = auto_buffer::alloc(str.length() / 2);
            auto sv = str.to_string_view();

            while (!sv.empty()) {
                int32_t value;
                auto scan_res = scn::scan(sv, "{:2x}", value);
                if (!scan_res) {
                    throw sqlite_func_error(
                        "invalid hex input at: {}",
                        std::distance(str.begin(), sv.begin()));
                }
                buf.push_back((char) (value & 0xff));
                sv = scan_res.range_as_string_view();
            }

            return blob_auto_buffer{std::move(buf)};
        }
#if defined(HAVE_LIBCURL)
        case encode_algo::uri: {
            auto_mem<char> retval(curl_free);

            retval = curl_easy_unescape(
                get_curl_easy(), str.data(), str.length(), nullptr);

            return std::move(retval);
        }
#endif
    }
    ensure(false);
}

std::string
sql_humanize_file_size(file_ssize_t value)
{
    return humanize::file_size(value, humanize::alignment::columnar);
}

static std::string
sql_anonymize(string_fragment frag)
{
    static safe::Safe<lnav::text_anonymizer> ta;

    return ta.writeAccess()->next(frag);
}

#if !CURL_AT_LEAST_VERSION(7, 80, 0)
extern "C"
{
const char* curl_url_strerror(CURLUcode error);
}
#endif

json_string
sql_parse_url(std::string url)
{
    static auto* CURL_HANDLE = get_curl_easy();

    auto_mem<CURLU> cu(curl_url_cleanup);
    cu = curl_url();

    auto rc = curl_url_set(
        cu, CURLUPART_URL, url.c_str(), CURLU_NON_SUPPORT_SCHEME);
    if (rc != CURLUE_OK) {
        throw lnav::console::user_message::error(
            attr_line_t("invalid URL: ").append(lnav::roles::file(url)))
            .with_reason(curl_url_strerror(rc));
    }

    auto_mem<char> url_part(curl_free);
    yajlpp_gen gen;
    yajl_gen_config(gen, yajl_gen_beautify, false);

    {
        yajlpp_map root(gen);

        root.gen("scheme");
        rc = curl_url_get(cu, CURLUPART_SCHEME, url_part.out(), 0);
        if (rc == CURLUE_OK) {
            root.gen(string_fragment::from_c_str(url_part.in()));
        } else {
            root.gen();
        }
        root.gen("username");
        rc = curl_url_get(cu, CURLUPART_USER, url_part.out(), CURLU_URLDECODE);
        if (rc == CURLUE_OK) {
            root.gen(string_fragment::from_c_str(url_part.in()));
        } else {
            root.gen();
        }
        root.gen("password");
        rc = curl_url_get(
            cu, CURLUPART_PASSWORD, url_part.out(), CURLU_URLDECODE);
        if (rc == CURLUE_OK) {
            root.gen(string_fragment::from_c_str(url_part.in()));
        } else {
            root.gen();
        }
        root.gen("host");
        rc = curl_url_get(cu, CURLUPART_HOST, url_part.out(), CURLU_URLDECODE);
        if (rc == CURLUE_OK) {
            root.gen(string_fragment::from_c_str(url_part.in()));
        } else {
            root.gen();
        }
        root.gen("port");
        rc = curl_url_get(cu, CURLUPART_PORT, url_part.out(), 0);
        if (rc == CURLUE_OK) {
            root.gen(string_fragment::from_c_str(url_part.in()));
        } else {
            root.gen();
        }
        root.gen("path");
        rc = curl_url_get(cu, CURLUPART_PATH, url_part.out(), CURLU_URLDECODE);
        if (rc == CURLUE_OK) {
            root.gen(string_fragment::from_c_str(url_part.in()));
        } else {
            root.gen();
        }
        rc = curl_url_get(cu, CURLUPART_QUERY, url_part.out(), 0);
        if (rc == CURLUE_OK) {
            root.gen("query");
            root.gen(string_fragment::from_c_str(url_part.in()));

            root.gen("parameters");
            robin_hood::unordered_set<std::string> seen_keys;
            yajlpp_map query_map(gen);

            for (size_t lpc = 0; url_part.in()[lpc]; lpc++) {
                if (url_part.in()[lpc] == '+') {
                    url_part.in()[lpc] = ' ';
                }
            }
            auto query_frag = string_fragment::from_c_str(url_part.in());
            auto remaining = query_frag;

            while (true) {
                auto split_res
                    = remaining.split_when(string_fragment::tag1{'&'});
                auto_mem<char> kv_pair(curl_free);
                auto kv_pair_encoded = split_res.first;
                int out_len = 0;

                kv_pair = curl_easy_unescape(CURL_HANDLE,
                                             kv_pair_encoded.data(),
                                             kv_pair_encoded.length(),
                                             &out_len);

                auto kv_pair_frag
                    = string_fragment::from_bytes(kv_pair.in(), out_len);
                auto eq_index_opt = kv_pair_frag.find('=');
                if (eq_index_opt) {
                    auto key = kv_pair_frag.sub_range(0, eq_index_opt.value());
                    auto val = kv_pair_frag.substr(eq_index_opt.value() + 1);
                    auto key_str = key.to_string();

                    if (seen_keys.count(key_str) == 0) {
                        seen_keys.emplace(key_str);
                        query_map.gen(key);
                        query_map.gen(val);
                    }
                } else {
                    auto val_str = split_res.first.to_string();

                    if (seen_keys.count(val_str) == 0) {
                        seen_keys.insert(val_str);
                        query_map.gen(split_res.first);
                        query_map.gen();
                    }
                }

                if (split_res.second.empty()) {
                    break;
                }

                remaining = split_res.second;
            }
        } else {
            root.gen("query");
            root.gen();
            root.gen("parameters");
            root.gen();
        }
        root.gen("fragment");
        rc = curl_url_get(
            cu, CURLUPART_FRAGMENT, url_part.out(), CURLU_URLDECODE);
        if (rc == CURLUE_OK) {
            root.gen(string_fragment::from_c_str(url_part.in()));
        } else {
            root.gen();
        }
    }

    return json_string(gen);
}

struct url_parts {
    std::optional<std::string> up_scheme;
    std::optional<std::string> up_username;
    std::optional<std::string> up_password;
    std::optional<std::string> up_host;
    std::optional<std::string> up_port;
    std::optional<std::string> up_path;
    std::optional<std::string> up_query;
    std::map<std::string, std::optional<std::string>> up_parameters;
    std::optional<std::string> up_fragment;
};

static const json_path_container url_params_handlers = {
    yajlpp::pattern_property_handler("(?<param>.*)")
        .for_field(&url_parts::up_parameters),
};

static const typed_json_path_container<url_parts> url_parts_handlers = {
    yajlpp::property_handler("scheme").for_field(&url_parts::up_scheme),
    yajlpp::property_handler("username").for_field(&url_parts::up_username),
    yajlpp::property_handler("password").for_field(&url_parts::up_password),
    yajlpp::property_handler("host").for_field(&url_parts::up_host),
    yajlpp::property_handler("port").for_field(&url_parts::up_port),
    yajlpp::property_handler("path").for_field(&url_parts::up_path),
    yajlpp::property_handler("query").for_field(&url_parts::up_query),
    yajlpp::property_handler("parameters").with_children(url_params_handlers),
    yajlpp::property_handler("fragment").for_field(&url_parts::up_fragment),
};

static auto_mem<char>
sql_unparse_url(string_fragment in)
{
    static auto* CURL_HANDLE = get_curl_easy();
    static intern_string_t SRC = intern_string::lookup("arg");

    auto parse_res = url_parts_handlers.parser_for(SRC).of(in);
    if (parse_res.isErr()) {
        throw parse_res.unwrapErr()[0];
    }

    auto up = parse_res.unwrap();
    auto_mem<CURLU> cu(curl_url_cleanup);
    cu = curl_url();

    if (up.up_scheme) {
        curl_url_set(
            cu, CURLUPART_SCHEME, up.up_scheme->c_str(), CURLU_URLENCODE);
    }
    if (up.up_username) {
        curl_url_set(
            cu, CURLUPART_USER, up.up_username->c_str(), CURLU_URLENCODE);
    }
    if (up.up_password) {
        curl_url_set(
            cu, CURLUPART_PASSWORD, up.up_password->c_str(), CURLU_URLENCODE);
    }
    if (up.up_host) {
        curl_url_set(cu, CURLUPART_HOST, up.up_host->c_str(), CURLU_URLENCODE);
    }
    if (up.up_port) {
        curl_url_set(cu, CURLUPART_PORT, up.up_port->c_str(), 0);
    }
    if (up.up_path) {
        curl_url_set(cu, CURLUPART_PATH, up.up_path->c_str(), CURLU_URLENCODE);
    }
    if (up.up_query) {
        curl_url_set(cu, CURLUPART_QUERY, up.up_query->c_str(), 0);
    } else if (!up.up_parameters.empty()) {
        for (const auto& pair : up.up_parameters) {
            auto_mem<char> key(curl_free);
            auto_mem<char> value(curl_free);
            std::string qparam;

            key = curl_easy_escape(
                CURL_HANDLE, pair.first.c_str(), pair.first.length());
            if (pair.second) {
                value = curl_easy_escape(
                    CURL_HANDLE, pair.second->c_str(), pair.second->length());
                qparam = fmt::format(FMT_STRING("{}={}"), key.in(), value.in());
            } else {
                qparam = key.in();
            }

            curl_url_set(
                cu, CURLUPART_QUERY, qparam.c_str(), CURLU_APPENDQUERY);
        }
    }
    if (up.up_fragment) {
        curl_url_set(
            cu, CURLUPART_FRAGMENT, up.up_fragment->c_str(), CURLU_URLENCODE);
    }

    auto_mem<char> retval(curl_free);

    curl_url_get(cu, CURLUPART_URL, retval.out(), 0);
    return retval;
}

}  // namespace

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

static std::string
sql_humanize_id(string_fragment id)
{
    auto& vc = view_colors::singleton();
    auto attrs = vc.attrs_for_ident(id.data(), id.length());

    return fmt::format(FMT_STRING("\x1b[38;5;{}m{}\x1b[0m"),
                       attrs.ta_fg_color.value_or(COLOR_CYAN),
                       id);
}

static std::string
sql_pretty_print(string_fragment in)
{
    data_scanner ds(in);
    pretty_printer pp(&ds, {});
    attr_line_t retval;

    pp.append_to(retval);

    return std::move(retval.get_string());
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
                .with_prql_path({"text", "regexp_match"})
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
                }))
            .with_result_subtype(),

        sqlite_func_adapter<decltype(&regexp_replace), regexp_replace>::builder(
            help_text("regexp_replace",
                      "Replace the parts of a string that match a regular "
                      "expression.")
                .sql_function()
                .with_prql_path({"text", "regexp_replace"})
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
                        .with_prql_path({"humanize", "file_size"})
                        .with_parameter({"value", "The file size to format"})
                        .with_tags({"string"})
                        .with_example({
                            "To format an amount",
                            "SELECT humanize_file_size(10 * 1024 * 1024)",
                        })),

        sqlite_func_adapter<decltype(&sql_humanize_id), sql_humanize_id>::
            builder(help_text("humanize_id",
                              "Colorize the given ID using ANSI escape codes.")
                        .sql_function()
                        .with_prql_path({"humanize", "id"})
                        .with_parameter({"id", "The identifier to color"})
                        .with_tags({"string"})
                        .with_example({
                            "To colorize the ID 'cluster1'",
                            "SELECT humanize_id('cluster1')",
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
                    .with_prql_path({"text", "sparkline"})
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

        sqlite_func_adapter<decltype(&sql_anonymize), sql_anonymize>::builder(
            help_text("anonymize",
                      "Replace identifying information with random values.")
                .sql_function()
                .with_prql_path({"text", "anonymize"})
                .with_parameter({"value", "The text to anonymize"})
                .with_tags({"string"})
                .with_example({
                    "To anonymize an IP address",
                    "SELECT anonymize('Hello, 192.168.1.2')",
                })),

        sqlite_func_adapter<decltype(&extract), extract>::builder(
            help_text("extract",
                      "Automatically Parse and extract data from a string")
                .sql_function()
                .with_prql_path({"text", "discover"})
                .with_parameter({"str", "The string to parse"})
                .with_tags({"string"})
                .with_example({
                    "To extract key/value pairs from a string",
                    "SELECT extract('foo=1 bar=2 name=\"Rolo Tomassi\"')",
                })
                .with_example({
                    "To extract columnar data from a string",
                    "SELECT extract('1.0 abc 2.0')",
                }))
            .with_result_subtype(),

        sqlite_func_adapter<decltype(&logfmt2json), logfmt2json>::builder(
            help_text("logfmt2json",
                      "Convert a logfmt-encoded string into JSON")
                .sql_function()
                .with_prql_path({"logfmt", "to_json"})
                .with_parameter({"str", "The logfmt message to parse"})
                .with_tags({"string"})
                .with_example({
                    "To extract key/value pairs from a log message",
                    "SELECT logfmt2json('foo=1 bar=2 name=\"Rolo Tomassi\"')",
                }))
            .with_result_subtype(),

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

        sqlite_func_adapter<decltype(&sql_encode), sql_encode>::builder(
            help_text("encode", "Encode the value using the given algorithm")
                .sql_function()
                .with_parameter(help_text("value", "The value to encode"))
                .with_parameter(help_text("algorithm",
                                          "One of the following encoding "
                                          "algorithms: base64, hex, uri"))
                .with_tags({"string"})
                .with_example({
                    "To base64-encode 'Hello, World!'",
                    "SELECT encode('Hello, World!', 'base64')",
                })
                .with_example({
                    "To hex-encode 'Hello, World!'",
                    "SELECT encode('Hello, World!', 'hex')",
                })
                .with_example({
                    "To URI-encode 'Hello, World!'",
                    "SELECT encode('Hello, World!', 'uri')",
                })),

        sqlite_func_adapter<decltype(&sql_decode), sql_decode>::builder(
            help_text("decode", "Decode the value using the given algorithm")
                .sql_function()
                .with_parameter(help_text("value", "The value to decode"))
                .with_parameter(help_text("algorithm",
                                          "One of the following encoding "
                                          "algorithms: base64, hex, uri"))
                .with_tags({"string"})
                .with_example({
                    "To decode the URI-encoded string '%63%75%72%6c'",
                    "SELECT decode('%63%75%72%6c', 'uri')",
                })),

        sqlite_func_adapter<decltype(&sql_parse_url), sql_parse_url>::builder(
            help_text("parse_url",
                      "Parse a URL and return the components in a JSON object. "
                      "Limitations: not all URL schemes are supported and "
                      "repeated query parameters are not captured.")
                .sql_function()
                .with_parameter(help_text("url", "The URL to parse"))
                .with_result({
                    "scheme",
                    "The URL's scheme",
                })
                .with_result({
                    "username",
                    "The name of the user specified in the URL",
                })
                .with_result({
                    "password",
                    "The password specified in the URL",
                })
                .with_result({
                    "host",
                    "The host name / IP specified in the URL",
                })
                .with_result({
                    "port",
                    "The port specified in the URL",
                })
                .with_result({
                    "path",
                    "The path specified in the URL",
                })
                .with_result({
                    "query",
                    "The query string in the URL",
                })
                .with_result({
                    "parameters",
                    "An object containing the query parameters",
                })
                .with_result({
                    "fragment",
                    "The fragment specified in the URL",
                })
                .with_tags({"string", "url"})
                .with_example({
                    "To parse the URL "
                    "'https://example.com/search?q=hello%20world'",
                    "SELECT "
                    "parse_url('https://example.com/search?q=hello%20world')",
                })
                .with_example({
                    "To parse the URL "
                    "'https://alice@[fe80::14ff:4ee5:1215:2fb2]'",
                    "SELECT "
                    "parse_url('https://alice@[fe80::14ff:4ee5:1215:2fb2]')",
                }))
            .with_result_subtype(),

        sqlite_func_adapter<decltype(&sql_unparse_url), sql_unparse_url>::
            builder(
                help_text("unparse_url",
                          "Convert a JSON object containing the parts of a "
                          "URL into a URL string")
                    .sql_function()
                    .with_parameter(help_text(
                        "obj", "The JSON object containing the URL parts"))
                    .with_tags({"string", "url"})
                    .with_example({
                        "To unparse the object "
                        "'{\"scheme\": \"https\", \"host\": \"example.com\"}'",
                        "SELECT "
                        "unparse_url('{\"scheme\": \"https\", \"host\": "
                        "\"example.com\"}')",
                    })),

        sqlite_func_adapter<decltype(&sql_pretty_print), sql_pretty_print>::
            builder(
                help_text("pretty_print", "Pretty-print the given string")
                    .sql_function()
                    .with_prql_path({"text", "pretty"})
                    .with_parameter(help_text("str", "The string to format"))
                    .with_tags({"string"})
                    .with_example({
                        "To pretty-print the string "
                        "'{\"scheme\": \"https\", \"host\": \"example.com\"}'",
                        "SELECT "
                        "pretty_print('{\"scheme\": \"https\", \"host\": "
                        "\"example.com\"}')",
                    })),

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
