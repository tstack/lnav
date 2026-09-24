/**
 * Copyright (c) 2017, Timothy Stack
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
 */

#include "config.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <data_parser.hh>

#include "base/relative_time.hh"
#include "base/from_trait.hh"
#include "base/fts_fuzzy_match.hh"
#include "base/itertools.similar.hh"
#include "breadcrumb.hh"
#include "byte_array.hh"
#include "cmd.parser.hh"
#include "data_scanner.hh"
#include "digestible/digestible.h"
#include "doctest/doctest.h"
#include "file_split.hh"
#include "hasher.hh"
#include "lnav_config.hh"
#include "lnav_util.hh"
#include "logfile.hh"
#include "md2attr_line.hh"
#include "md4cpp.hh"
#include "ptimec.hh"
#include "shlex.hh"
#include "sqlite-extension-func.hh"
#include "sqlitepp.hh"
#include "terminfo/terminfo.h"
#include "unique_path.hh"
#include "vtab_module.hh"

#include <condition_variable>
#include <random>
#include <set>
#include <mutex>
#include <thread>

using namespace std;

TEST_CASE("lnav::logfile_indexing_width")
{
    auto& threads = lnav_config.lc_logfile.lc_indexing_threads;
    const auto saved = threads;

    // One file at a time, the way lnav always has.
    threads = 1;
    CHECK(1 == lnav::logfile_indexing_width(0));
    CHECK(1 == lnav::logfile_indexing_width(1));
    CHECK(1 == lnav::logfile_indexing_width(100));

    // A fixed width, never more workers than there are files.
    threads = 4;
    CHECK(1 == lnav::logfile_indexing_width(1));
    CHECK(2 == lnav::logfile_indexing_width(2));
    CHECK(4 == lnav::logfile_indexing_width(4));
    CHECK(4 == lnav::logfile_indexing_width(100));

    // 0 asks the machine; the answer is bounded the same way.
    threads = 0;
    CHECK(1 == lnav::logfile_indexing_width(1));
    CHECK(lnav::logfile_indexing_width(100) >= 1);
    CHECK(lnav::logfile_indexing_width(100) <= 8);

    threads = saved;
}

#if 0
TEST_CASE("overwritten-logfile") {
    string fname = "reload_test.0";

    ofstream(fname) << "test 1\n";

    logfile_open_options loo;
    logfile lf(fname, loo);
    auto build_result = lf.rebuild_index();
    CHECK(build_result == logfile::RR_NEW_LINES);
    CHECK(lf.size() == 1);

    sleep(1);
    ofstream(fname) << "test 2\n";
    auto rebuild_result = lf.rebuild_index();
    CHECK(rebuild_result == logfile::RR_NO_NEW_LINES);
    CHECK(lf.is_closed());
}
#endif

TEST_CASE("shlex::eval")
{
    std::string cmdline1 = "${semantic_highlight_color}";

    shlex lexer(cmdline1);

    std::map<std::string, scoped_value_t> vars = {
        {"semantic_highlight_color", string_fragment::from_const("foo")},
    };

    std::string out;
    auto rc = lexer.eval(out, scoped_resolver{&vars});
    CHECK(rc);
    CHECK(out == "foo");
}
#if 0
TEST_CASE("tiparm_s")
{
    const auto* path = terminfo_find_path_for_term("xterm-256color");
    auto* ti = terminfo_load(path);

    TiparmValue argv[] = {tiparm_int(5)};
    const auto* fmt = terminfo_get_string_by_name(ti, "setab");
    auto_mem<char> ab1;
    ab1 = tiparm_s(fmt, 1, argv);
    fprintf(stderr, "ab = %s", &(ab1.in()[1]));
    terminfo_free(ti);
}
#endif

TEST_CASE("lnav::command::parse_for_prompt")
{
    static const auto SEARCH_HELP
        = help_text("search", "search the view for a pattern")
              .with_parameter(
                  help_text("pattern", "The pattern to search for")
                      .with_format(help_parameter_format_t::HPF_REGEX));

    exec_context ec;
    {
        auto sf = "Word "_frag;
        auto parse_res = lnav::command::parse_for_prompt(ec, sf, SEARCH_HELP);
        auto arg = parse_res.arg_at(5);
        CHECK(arg.has_value());
        CHECK(arg->aar_help == &SEARCH_HELP.ht_parameters[0]);
        CHECK(arg->aar_element.se_origin.empty());
    }
    {
        auto sf = "abc\\"_frag;
        auto parse_res = lnav::command::parse_for_prompt(ec, sf, SEARCH_HELP);
        auto arg = parse_res.arg_at(4);
        CHECK(arg.has_value());
        CHECK(arg->aar_element.se_value == "abc\\");
    }
}

TEST_CASE("shlex::split")
{
    {
        std::string cmdline1 = "abc\\";
        shlex lexer(cmdline1);
        auto split_res = lexer.split(scoped_resolver{});
        CHECK(split_res.isErr());
        auto se = split_res.unwrapErr();
        CHECK(se.se_elements.size() == 1);
        CHECK(se.se_elements[0].se_value == cmdline1);
    }
    {
        std::string cmdline1 = "";

        std::map<std::string, scoped_value_t> vars;
        shlex lexer(cmdline1);
        auto split_res = lexer.split(scoped_resolver{&vars});
        CHECK(split_res.isOk());
        auto args = split_res.unwrap();
        CHECK(args.empty());
    }
    {
        std::string cmdline1 = ":sh --name=\"foo $BAR\" echo Hello!";

        std::map<std::string, scoped_value_t> vars;
        shlex lexer(cmdline1);
        auto split_res = lexer.split(scoped_resolver{&vars});
        CHECK(split_res.isOk());
        auto args = split_res.unwrap();
        for (const auto& se : args) {
            printf(" range %d:%d -- %s\n",
                   se.se_origin.sf_begin,
                   se.se_origin.sf_end,
                   se.se_value.c_str());
        }
    }
    {
        std::string cmdline1 = "abc def $FOO ghi";

        std::map<std::string, scoped_value_t> vars;
        shlex lexer(cmdline1);
        auto split_res = lexer.split(scoped_resolver{&vars});
        CHECK(split_res.isOk());
        auto args = split_res.unwrap();
        for (const auto& se : args) {
            printf(" range %d:%d -- %s\n",
                   se.se_origin.sf_begin,
                   se.se_origin.sf_end,
                   se.se_value.c_str());
        }
    }
}

TEST_CASE("byte_array")
{
    using my_array_t = byte_array<8>;

    my_array_t ba1;

    memcpy(ba1.out(), "abcd1234", my_array_t::BYTE_COUNT);
    CHECK(ba1.to_string() == "6162636431323334");
    auto ba2 = ba1;
    CHECK(ba1 == ba2);
    CHECK_FALSE(ba1 != ba2);
    CHECK_FALSE(ba1 < ba2);

    my_array_t ba3;

    memcpy(ba3.out(), "abcd1235", my_array_t::BYTE_COUNT);
    CHECK(ba1 < ba3);
    CHECK_FALSE(ba3 < ba1);

    ba1.clear();
    CHECK(ba1.to_string() == "0000000000000000");
    CHECK(ba2.to_string() == "6162636431323334");

    auto outbuf = auto_buffer::alloc(my_array_t::STRING_SIZE);
    ba2.to_string(std::back_inserter(outbuf));
    CHECK(std::string(outbuf.in(), outbuf.size()) == "6162636431323334");
}

TEST_CASE("ptime_fmt")
{
    const char* date_str = "2018-05-16 18:16:42";
    struct exttm tm;
    off_t off = 0;

    bool rc
        = ptime_fmt("%Y-%d-%m\t%H:%M:%S", &tm, date_str, off, strlen(date_str));
    CHECK(!rc);
    CHECK(off == 8);
}

TEST_CASE("rgb_color from string")
{
    const auto name = string_fragment::from_const("#87d7ff");
    auto color = from<rgb_color>(name).unwrap();
    CHECK(color.rc_r == 135);
    CHECK(color.rc_g == 215);
    CHECK(color.rc_b == 255);
}

TEST_CASE("ptime_roundtrip")
{
    const char* fmts[] = {
        "%Y-%m-%d %l:%M:%S %p",
        "%Y-%m-%d %I:%M:%S %p",
    };
    time_t now = time(nullptr);

    for (const auto* fmt : fmts) {
        for (time_t sec = now; sec < (now + (24 * 60 * 60)); sec++) {
            char ftime_result[128];
            char strftime_result[128];
            struct exttm etm;

            memset(&etm, 0, sizeof(etm));
            gmtime_r(&sec, &etm.et_tm);
            etm.et_flags = ETF_YEAR_SET | ETF_MONTH_SET | ETF_DAY_SET;
            size_t ftime_size
                = ftime_fmt(ftime_result, sizeof(ftime_result), fmt, etm);
            size_t strftime_size = strftime(
                strftime_result, sizeof(strftime_result), fmt, &etm.et_tm);

            CHECK(string(ftime_result, ftime_size)
                  == string(strftime_result, strftime_size));

            struct exttm etm2;
            off_t off = 0;

            memset(&etm2, 0, sizeof(etm2));
            bool rc = ptime_fmt(fmt, &etm2, ftime_result, off, ftime_size);
            CHECK(rc);
            CHECK(sec == tm2sec(&etm2.et_tm));
        }
    }
}

class my_path_source : public unique_path_source {
public:
    explicit my_path_source(std::filesystem::path p) : mps_path(std::move(p)) {}

    std::filesystem::path get_path() const override { return this->mps_path; }

    std::filesystem::path mps_path;
};

TEST_CASE("unique_path")
{
    unique_path_generator upg;

    auto bar = make_shared<my_path_source>("/foo/bar");
    auto bar_dupe = make_shared<my_path_source>("/foo/bar");
    auto baz = make_shared<my_path_source>("/foo/baz");
    auto baz2 = make_shared<my_path_source>("/foo2/bar");
    auto log1 = make_shared<my_path_source>(
        "/home/bob/downloads/machine1/var/log/syslog.log");
    auto log2 = make_shared<my_path_source>(
        "/home/bob/downloads/machine2/var/log/syslog.log");

    upg.add_source(bar);
    upg.add_source(bar_dupe);
    upg.add_source(baz);
    upg.add_source(baz2);
    upg.add_source(log1);
    upg.add_source(log2);

    upg.generate();

    CHECK(bar->get_unique_path() == "[foo]/bar");
    CHECK(bar_dupe->get_unique_path() == "[foo]/bar");
    CHECK(baz->get_unique_path() == "baz");
    CHECK(baz2->get_unique_path() == "[foo2]/bar");
    CHECK(log1->get_unique_path() == "[machine1]/syslog.log");
    CHECK(log2->get_unique_path() == "[machine2]/syslog.log");
}

TEST_CASE("attr_line to json")
{
    attr_line_t al;

    al.append("Hello, ").append(lnav::roles::symbol("World")).append("!");

    auto json = lnav::to_json(al);
    auto al2 = lnav::from_json<attr_line_t>(json).unwrap();
    auto json2 = lnav::to_json(al2);

    CHECK(json == json2);
}

TEST_CASE("user_message to json")
{
    auto um = lnav::console::user_message::error("testing")
                  .with_reason("because")
                  .with_snippet(lnav::console::snippet::from(
                                    intern_string::lookup("hello.c"), "printf(")
                                    .with_line(1))
                  .with_help("close it");

    auto json = lnav::to_json(um);
    auto um2 = lnav::from_json<lnav::console::user_message>(json).unwrap();
    auto json2 = lnav::to_json(um2);

    CHECK(json == json2);
}

TEST_CASE("data_scanner CSI")
{
    static const char INPUT[] = "\x1b[32mHello\x1b[0m";

    data_scanner ds(string_fragment::from_const(INPUT));

    auto tok_res = ds.tokenize2();
    CHECK(tok_res->tr_token == DT_CSI);
    CHECK(tok_res->to_string() == "\x1b[32m");
    tok_res = ds.tokenize2();
    CHECK(tok_res->tr_token == DT_WORD);
    CHECK(tok_res->to_string() == "Hello");
    tok_res = ds.tokenize2();
    CHECK(tok_res->tr_token == DT_CSI);
    CHECK(tok_res->to_string() == "\x1b[0m");
}

TEST_CASE("data_scanner quote")
{
    static const char INPUT[] = "abc \"\"\"\n";

    {
        data_scanner ds(string_fragment::from_const(INPUT));

        auto tok_res = ds.tokenize2();
        CHECK(tok_res->tr_token == DT_WORD);
        CHECK(tok_res->to_string() == "abc");
        tok_res = ds.tokenize2();
        CHECK(tok_res->tr_token == DT_WHITE);
        tok_res = ds.tokenize2();
        CHECK(tok_res->tr_token == DT_QUOTED_STRING);
        tok_res = ds.tokenize2();
        CHECK_FALSE(tok_res.has_value());
    }

    {
        data_scanner ds(string_fragment::from_const(INPUT));
        data_parser dp(&ds);

        dp.parse();
    }
}

TEST_CASE("data_scanner quote3")
{
    static const char INPUT[] = "\nC0\n\n\"000\"00";

    {
        data_scanner ds(string_fragment::from_const(INPUT));

        auto tok_res = ds.tokenize2();
        CHECK(tok_res->tr_token == DT_LINE);
        tok_res = ds.tokenize2();
        CHECK(tok_res->tr_token == DT_SYMBOL);
        printf(
            " %d:%d\n", tok_res->tr_capture.c_begin, tok_res->tr_capture.c_end);
        tok_res = ds.tokenize2();
        CHECK(tok_res->tr_token == DT_LINE);
        tok_res = ds.tokenize2();
        CHECK(tok_res->tr_token == DT_LINE);
        tok_res = ds.tokenize2();
        CHECK(tok_res->tr_token == DT_QUOTED_STRING);
    }
}

TEST_CASE("possibility_collector")
{
    auto keys_of = [](const std::vector<breadcrumb::possibility>& poss) {
        std::vector<std::string> retval;
        for (const auto& p : poss) {
            retval.emplace_back(p.p_key);
        }
        return retval;
    };

    SUBCASE("empty search keeps the first keys")
    {
        breadcrumb::possibility_collector pc(string_fragment{}, 3);

        pc.add("b"_frag);
        pc.add("a"_frag);
        pc.add("c"_frag);
        pc.add("d"_frag);
        CHECK(keys_of(pc.release())
              == std::vector<std::string>{"b", "a", "c"});
    }

    SUBCASE("duplicates are returned once")
    {
        breadcrumb::possibility_collector pc(string_fragment{}, 3);

        pc.add("b"_frag);
        pc.add("a"_frag);
        pc.add("b"_frag);
        pc.add("c"_frag);
        CHECK(keys_of(pc.release()) == std::vector<std::string>{"b", "a"});
    }

    SUBCASE("search keeps the best matches, best first")
    {
        auto score_of = [](const std::string& key) {
            int retval = 0;
            fts::fuzzy_match("a1", key.c_str(), retval);
            return retval;
        };

        std::vector<std::string> keys;
        for (int lpc = 0; lpc < 5000; lpc++) {
            keys.emplace_back(fmt::format(FMT_STRING("{:x}"), lpc * 7919));
        }

        breadcrumb::possibility_collector pc("a1"_frag, 10);
        for (const auto& key : keys) {
            // Not NUL-terminated, the way keys come out of a larger buffer.
            auto buf = key + "a1a1";
            pc.add(string_fragment::from_bytes(buf.data(), key.size()));
        }
        auto actual = keys_of(pc.release());
        REQUIRE(actual.size() == 10);
        CHECK(std::set<std::string>(actual.begin(), actual.end()).size()
              == 10);
        for (size_t lpc = 1; lpc < actual.size(); lpc++) {
            CHECK(score_of(actual[lpc - 1]) >= score_of(actual[lpc]));
        }

        // The same scores similar_to() would keep, which is what the view
        // narrows to afterward.  Keys with equal scores may differ.
        auto expected = keys | lnav::itertools::similar_to("a1", 10);
        std::vector<int> expected_scores, actual_scores;
        for (const auto& key : expected) {
            expected_scores.push_back(score_of(key));
        }
        for (const auto& key : actual) {
            actual_scores.push_back(score_of(key));
        }
        std::sort(expected_scores.begin(), expected_scores.end());
        std::sort(actual_scores.begin(), actual_scores.end());
        CHECK(actual_scores == expected_scores);
    }

    SUBCASE("no matches")
    {
        breadcrumb::possibility_collector pc("xyz"_frag);

        pc.add("abc"_frag);
        CHECK(pc.release().empty());
    }
}

TEST_CASE("hasher to_string")
{
    char buf[33];
    hasher h;

    h.update("hello");
    h.to_string(buf);
    CHECK(string(buf) == "cae682d36a82683743e01ac7d11e945c");
}

TEST_CASE("from_column")
{
    auto_sqlite3 db;
    auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
    const char* sql = R"(
SELECT 1 AS b, 'abc' AS s, 2.5 AS d, NULL AS n, 42 AS i, '' AS e
)";

    REQUIRE(sqlite3_open(":memory:", db.out()) == SQLITE_OK);
    REQUIRE(sqlite3_prepare_v2(db.in(), sql, -1, stmt.out(), nullptr)
            == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt.in()) == SQLITE_ROW);

    CHECK(from_column<bool>()(stmt.in(), 0) == true);
    CHECK(from_column<int>()(stmt.in(), 0) == 1);
    CHECK(from_column<std::string>()(stmt.in(), 1) == "abc");
    CHECK(from_column<string_fragment>()(stmt.in(), 1) == "abc");
    CHECK(from_column<double>()(stmt.in(), 2) == 2.5);
    CHECK(from_column<int64_t>()(stmt.in(), 4) == 42);

    // An empty string is not a null.
    CHECK(from_column<std::string>()(stmt.in(), 5).empty());
    CHECK(from_column<std::optional<std::string>>()(stmt.in(), 5).has_value());

    // A null column and a column that is not in the row both read as an
    // empty optional.
    CHECK(!from_column<std::optional<int64_t>>()(stmt.in(), 3).has_value());
    CHECK(!from_column<std::optional<int64_t>>()(stmt.in(), 6).has_value());
    CHECK(from_column<std::optional<std::string>>()(stmt.in(), 1).value()
          == "abc");

    // Unlike from_sqlite<>, which applies numeric affinity first, a column
    // that is not stored as an integer is refused rather than coerced.
    CHECK_THROWS_AS(from_column<int64_t>()(stmt.in(), 1),
                    from_sqlite_conversion_error);
}

TEST_CASE("lnav::sql::thread_local_db")
{
    static constexpr size_t THREAD_COUNT = 8;

    std::vector<sqlite3*> dbs(THREAD_COUNT, nullptr);
    std::vector<int> matched(THREAD_COUNT, -1);
    std::vector<int> same_twice(THREAD_COUNT, 0);
    std::vector<int> state_func_prepared(THREAD_COUNT, -1);
    std::vector<std::thread> threads;
    std::mutex open_lock;
    std::condition_variable open_cond;
    size_t open_count = 0;

    for (size_t lpc = 0; lpc < THREAD_COUNT; lpc++) {
        threads.emplace_back([lpc, &dbs, &matched, &same_twice,
                              &state_func_prepared, &open_lock, &open_cond,
                              &open_count]() {
            auto* db = lnav::sql::thread_local_db();

            dbs[lpc] = db;
            // Every thread waits here so that all of the connections are
            // alive at the same time.  A thread that returned early would
            // free its connection and the allocator could hand the same
            // address to a thread that started later, making the
            // distinctness check below pass or fail at random.
            {
                std::unique_lock<std::mutex> lk(open_lock);

                open_count += 1;
                open_cond.notify_all();
                open_cond.wait(
                    lk, [&open_count]() { return open_count == THREAD_COUNT; });
            }
            if (db == nullptr) {
                return;
            }
            // The same thread asking again gets the same connection.
            same_twice[lpc] = (lnav::sql::thread_local_db() == db) ? 1 : 0;

            // A function from one of the thread-safe groups is registered and
            // works.  Running this from every thread at once is what catches
            // unguarded global state in register_sqlite_funcs().
            {
                auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
                const char* sql = "SELECT 1 WHERE startswith('abc', 'a')";

                if (sqlite3_prepare_v2(db, sql, -1, stmt.out(), nullptr)
                    == SQLITE_OK)
                {
                    matched[lpc] = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
                }
            }

            // One that reads lnav's view state is not, so it cannot even be
            // prepared here.  Callers rely on that to spot the statements
            // they have to run on the main thread.
            {
                auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
                const char* sql = "SELECT 1 WHERE log_top_line() > 0";

                state_func_prepared[lpc]
                    = (sqlite3_prepare_v2(db, sql, -1, stmt.out(), nullptr)
                       == SQLITE_OK)
                    ? 1
                    : 0;
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    std::set<sqlite3*> distinct;
    for (size_t lpc = 0; lpc < THREAD_COUNT; lpc++) {
        CHECK(dbs[lpc] != nullptr);
        CHECK(same_twice[lpc] == 1);
        CHECK(matched[lpc] == 1);
        CHECK(state_func_prepared[lpc] == 0);
        distinct.insert(dbs[lpc]);
    }
    // One connection per thread, not one shared between them.
    CHECK(distinct.size() == THREAD_COUNT);

    // The same statement prepares fine against a connection carrying the
    // full set, so the failure above is the missing group and not a typo.
    {
        auto_sqlite3 full_db;
        auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);

        REQUIRE(sqlite3_open(":memory:", full_db.out()) == SQLITE_OK);
        register_sqlite_funcs(full_db.in(), sqlite_registration_funcs);
        CHECK(sqlite3_prepare_v2(full_db.in(),
                                 "SELECT 1 WHERE log_top_line() > 0",
                                 -1,
                                 stmt.out(),
                                 nullptr)
              == SQLITE_OK);
    }
}

TEST_CASE("md4cpp::KNOWN_EMOJIS")
{
    const auto& em = md4cpp::get_emoji_map();

    // The hardcoded table is what the _emoji literal resolves against, and
    // half of these glyphs never reach a golden file, so this is the only
    // thing standing between a typo and a wrong symbol on screen.
    for (const auto& lit : md4cpp::literals::KNOWN_EMOJIS) {
        const auto shortname = lit.el_shortname.to_string();
        const auto iter = em.em_shortname2emoji.find(shortname);

        INFO("shortcode: " << shortname);
        REQUIRE(iter != em.em_shortname2emoji.end());
        CHECK(iter->second.get().e_value == lit.el_value.to_string());
    }
}

namespace {
struct code_line_recorder : md4cpp::typed_event_handler<std::vector<int>> {
    Result<void, std::string> enter_block(const block& bl) override
    {
        if (bl.is<MD_BLOCK_CODE_DETAIL*>()) {
            this->clr_lines.push_back(this->eh_line_number);
        }
        return Ok();
    }

    Result<void, std::string> leave_block(const block& bl) override
    {
        return Ok();
    }

    Result<void, std::string> enter_span(const span& sp) override
    {
        return Ok();
    }

    Result<void, std::string> leave_span(const span& sp) override
    {
        return Ok();
    }

    Result<void, std::string> text(MD_TEXTTYPE tt,
                                   const string_fragment& sf) override
    {
        return Ok();
    }

    std::vector<int> get_result() override { return this->clr_lines; }

    std::vector<int> clr_lines;
};
}  // namespace

TEST_CASE("md4cpp::parse code block line after front matter")
{
    static const auto CONTENT = string_fragment::from_const(
        "---\n"
        "title: test\n"
        "---\n"
        "\n"
        "Some text\n"
        "\n"
        "```lnav\n"
        ":echo hi\n"
        "```\n");

    auto md_file = md4cpp::parse_file("test.md", CONTENT);
    CHECK(md_file.f_frontmatter_format == text_format_t::TF_YAML);

    code_line_recorder clr;
    auto res = md4cpp::parse(md_file.f_body, clr);
    REQUIRE(res.isOk());
    CHECK(res.unwrap() == std::vector<int>{7});
}

TEST_CASE("md4cpp::parse code block without a language has no line")
{
    static const auto CONTENT = string_fragment::from_const(
        "```lnav\n"
        ":echo hi\n"
        "```\n"
        "\n"
        "```\n"
        "plain\n"
        "```\n"
        "\n"
        "    indented\n");

    code_line_recorder clr;
    auto res = md4cpp::parse(CONTENT, clr);
    REQUIRE(res.isOk());
    CHECK(res.unwrap() == std::vector<int>{1, 0, 0});
}

TEST_CASE("md4cpp::parse_file TOML front matter is not greedy")
{
    static const auto CONTENT = string_fragment::from_const(
        "+++\n"
        "title = 'test'\n"
        "+++\n"
        "body\n"
        "+++\n"
        "more body\n");

    auto md_file = md4cpp::parse_file("test.md", CONTENT);
    CHECK(md_file.f_frontmatter_format == text_format_t::TF_TOML);
    CHECK(md_file.f_frontmatter.to_string() == "title = 'test'");
    CHECK(md_file.f_body.to_string() == "body\n+++\nmore body\n");
}

TEST_CASE("md4cpp::parse_file empty and CRLF front matter")
{
    {
        static const auto CONTENT
            = string_fragment::from_const("---\n---\nbody\n");

        auto md_file = md4cpp::parse_file("test.md", CONTENT);
        CHECK(md_file.f_frontmatter_format == text_format_t::TF_YAML);
        CHECK(md_file.f_frontmatter.empty());
        CHECK(md_file.f_body.to_string() == "body\n");
    }
    {
        static const auto CONTENT = string_fragment::from_const(
            "+++\r\ntitle = 'test'\r\n+++\r\nbody\r\n");

        auto md_file = md4cpp::parse_file("test.md", CONTENT);
        CHECK(md_file.f_frontmatter_format == text_format_t::TF_TOML);
        CHECK(md_file.f_frontmatter.to_string() == "title = 'test'");
        CHECK(md_file.f_body.to_string() == "body\r\n");
    }
    {
        static const auto CONTENT
            = string_fragment::from_const("{\"title\": \"test\"}\r\nbody\r\n");

        auto md_file = md4cpp::parse_file("test.md", CONTENT);
        CHECK(md_file.f_frontmatter_format == text_format_t::TF_JSON);
        CHECK(md_file.f_frontmatter.to_string() == "{\"title\": \"test\"}");
    }
}

static std::string
render_md(const char* md)
{
    md2attr_line mdal;

    auto parse_res = md4cpp::parse(string_fragment::from_c_str(md), mdal);
    REQUIRE(parse_res.isOk());
    return parse_res.unwrap().get_string();
}

TEST_CASE("md2attr_line entities")
{
    auto str = render_md("a &#169; b &#x2014; c &bogus; d &amp;\n");

    CHECK(str.find("a \u00a9 b \u2014 c &bogus; d &") != std::string::npos);
}

TEST_CASE("md2attr_line inline HTML")
{
    auto str = render_md("plain <b>bold</b> <i>it</i> end\n");
    CHECK(str.find("plain bold it end") != std::string::npos);

    str = render_md("x <span>a<br>b</span> <!-- c --> y\n");
    CHECK(str.find("<") == std::string::npos);
    CHECK(str.find("x a\nb") != std::string::npos);
}

TEST_CASE("md2attr_line HTML block span with an entity")
{
    auto str = render_md("<div>\n<span style=\"color: red\">a &amp; b</span>\n</div>\n");

    CHECK(str.find("a & b") != std::string::npos);
}

TEST_CASE("md2attr_line table header wider than the column")
{
    auto str = render_md(
        "| https://example.com/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        " | b |\n"
        "|---|---|\n"
        "| x | y |\n");

    CHECK(str.find("https://example.com/") != std::string::npos);
}

TEST_CASE("file_split::piece_path")
{
    using lnav::file_split::is_piece_name;
    using lnav::file_split::piece_path;
    using std::filesystem::path;

    CHECK(piece_path("out", "/var/log/access.log.gz", 1)
          == path("out/access.0001.log"));
    CHECK(piece_path("out", "syslog.1", 12) == path("out/syslog.1.0012"));
    CHECK(piece_path(".", "messages", 10000) == path("./messages.10000"));

    CHECK(is_piece_name("access.0003.log", "access.log.gz"));
    CHECK(is_piece_name("syslog.1.0002", "syslog.1"));
    CHECK_FALSE(is_piece_name("access.log", "access.log.gz"));
    CHECK_FALSE(is_piece_name("access.00a3.log", "access.log"));
    CHECK_FALSE(is_piece_name("syslog.1", "syslog.1"));
}

TEST_CASE("file_split::policy")
{
    using namespace std::chrono_literals;
    using lnav::file_split::limits;
    using lnav::file_split::message_info;
    using lnav::file_split::policy;

    SUBCASE("lines")
    {
        limits lim;
        lim.l_lines = 10;
        policy pol(lim);
        const message_info small{100, 4, 4, std::nullopt};

        CHECK_FALSE(pol.should_cut_before(small));
        pol.add(small);
        CHECK_FALSE(pol.should_cut_before(small));
        pol.add(small);
        CHECK(pol.should_cut_before(small));

        // A message larger than the limit still goes whole into a piece.
        pol.start_piece();
        const message_info huge{100, 50, 50, std::nullopt};
        CHECK_FALSE(pol.should_cut_before(huge));
        pol.add(huge);
        CHECK(pol.should_cut_before(small));
    }

    SUBCASE("entries")
    {
        limits lim;
        lim.l_max_entries = 10;
        policy pol(lim);
        const message_info json{100, 1, 6, std::nullopt};

        CHECK_FALSE(pol.should_cut_before(json));
        pol.add(json);
        CHECK(pol.should_cut_before(json));
    }

    SUBCASE("bytes")
    {
        limits lim;
        lim.l_bytes = 1000;
        policy pol(lim);
        const message_info msg{600, 1, 1, std::nullopt};

        pol.add(msg);
        CHECK(pol.should_cut_before(msg));
    }

    SUBCASE("time")
    {
        limits lim;
        lim.l_duration = 1h;
        policy pol(lim);
        auto at = [](std::chrono::microseconds t) {
            return message_info{10, 1, 1, t};
        };

        CHECK(pol.window_for(-1us) == -1h);
        CHECK(pol.window_for(90min) == 1h);

        pol.add(at(70min));
        CHECK_FALSE(pol.should_cut_before(at(110min)));
        pol.add(at(110min));
        CHECK_FALSE(pol.should_cut_before(message_info{10, 1, 1, std::nullopt}));
        CHECK(pol.should_cut_before(at(120min)));

        pol.start_piece();
        pol.add(at(120min));
        // An earlier timestamp stays in the current piece.
        CHECK_FALSE(pol.should_cut_before(at(90min)));
        pol.add(at(90min));
        CHECK_FALSE(pol.should_cut_before(at(179min)));
        CHECK(pol.should_cut_before(at(180min)));
    }
}

TEST_CASE("file_split::limits::defaults")
{
    using lnav::file_split::limits;
    static constexpr uint64_t MiB = 1024ULL * 1024;
    static constexpr uint64_t GiB = 1024ULL * MiB;
    static constexpr uint64_t max_lines = 1ULL << 27;

    auto lim = limits::defaults(10 * GiB, 8, max_lines);
    CHECK(lim.l_max_entries == max_lines / 8);
    CHECK(lim.l_bytes.value() == 10 * GiB / 8);

    CHECK(limits::defaults(100 * MiB, 8, max_lines).l_bytes.value()
          == 256 * MiB);
    CHECK(limits::defaults(100 * GiB, 8, max_lines).l_bytes.value()
          == 2 * GiB);
    CHECK(limits::defaults(std::nullopt, 8, max_lines).l_bytes.value()
          == 1 * GiB);
}

TEST_CASE("tdigest max with only negative values")
{
    auto td = digestible::tdigest<double>(200);

    for (int round = 0; round < 5; round++) {
        for (int lpc = 0; lpc < 1000; lpc++) {
            td.insert(-5.0 - (lpc % 3));
        }
        td.merge();
    }

    CHECK(td.min() == -7.0);
    CHECK(td.max() == -5.0);
    CHECK(td.quantile(100) == -5.0);
}

TEST_CASE("tdigest quantile near the top is not NaN")
{
    // Repeated values can leave the last centroid with a weight of 2 or 3,
    // which has no room to interpolate between it and the maximum.
    auto rng = std::mt19937(3);

    for (int trial = 0; trial < 500; trial++) {
        const auto count = 5 + static_cast<int>(rng() % 3000);
        const auto compression = 10 + rng() % 200;
        const auto cardinality = 1 + rng() % 50;
        auto td = digestible::tdigest<double>(compression);

        for (int lpc = 0; lpc < count; lpc++) {
            td.insert(static_cast<double>(rng() % cardinality));
        }
        td.merge();
        for (int k = 900; k <= 1000; k++) {
            const auto q = td.quantile(k / 10.0);

            REQUIRE_FALSE(std::isnan(q));
            CHECK(q >= td.min());
            CHECK(q <= td.max());
        }
    }
}

TEST_CASE("tdigest insert of an unmerged t-digest")
{
    auto src = digestible::tdigest<double>(200);
    for (int lpc = 0; lpc < 100; lpc++) {
        src.insert(lpc);
    }

    auto dst = digestible::tdigest<double>(200);
    dst.insert(src);
    CHECK(dst.size() == 100);
    CHECK(dst.min() == 0.0);
    CHECK(dst.max() == 99.0);

    // Merged centroids plus values inserted after the merge.
    src.merge();
    for (int lpc = 100; lpc < 150; lpc++) {
        src.insert(lpc);
    }
    auto dst2 = digestible::tdigest<double>(200);
    dst2.insert(src);
    CHECK(dst2.size() == 150);
    CHECK(dst2.max() == 149.0);
}

TEST_CASE("tdigest copy keeps the compression")
{
    auto fresh = digestible::tdigest<double>(200);
    for (int lpc = 0; lpc < 1000; lpc++) {
        fresh.insert(lpc);
    }
    fresh.merge();

    auto copy = fresh;
    auto assigned = digestible::tdigest<double>(10);
    assigned = fresh;
    for (int lpc = 0; lpc < 200000; lpc++) {
        fresh.insert(lpc % 997);
        copy.insert(lpc % 997);
        assigned.insert(lpc % 997);
    }
    fresh.merge();
    copy.merge();
    assigned.merge();

    CHECK(copy.centroid_count() == fresh.centroid_count());
    CHECK(assigned.centroid_count() == fresh.centroid_count());
    CHECK(copy.quantile(99) == fresh.quantile(99));
}

