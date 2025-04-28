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
 */

#include "config.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "base/ansi_scrubber.hh"
#include "data_parser.hh"
#include "data_scanner.hh"
#include "doctest/doctest.h"
#include "document.sections.hh"
#include "pretty_printer.hh"

TEST_CASE("lnav::document::sections::basics")
{
    attr_line_t INPUT = R"(
{
   "msg": "Hello, World!",
   "obj": {
      "a": 1,
      "b": "Two",
      "c": 3.0
   },
   "arr": [1, 2, 3],
   "arr2": [
      456,
      789,
      {
        "def": 123,
        "ghi": null,
        "jkl": "other"
      },
      {
        "def": 456,
        "ghi": null,
        "jkl": "OTHER"
      },
      {
        "def": 789,
        "ghi": null,
        "jkl": "OtHeR"
      }
   ]
}
)";

    auto meta = lnav::document::discover(INPUT).perform();

    meta.m_sections_tree.visit_all([](const auto& intv) {
        auto ser = intv.value.match(
            [](const std::string& name) { return name; },
            [](const size_t index) { return fmt::format("{}", index); });
        printf("interval %d:%d %s\n", intv.start, intv.stop, ser.c_str());
    });
    lnav::document::hier_node::depth_first(
        meta.m_sections_root.get(), [](const auto* node) {
            printf("node %p %d\n", node, node->hn_start);
            for (const auto& pair : node->hn_named_children) {
                printf("  child: %p %s\n", pair.second, pair.first.c_str());
            }
        });
}

TEST_CASE("lnav::document::sections::empty")
{
    attr_line_t INPUT
        = R"(SOCKET 1 (10) creating new listening socket on port -1)";

    auto meta = lnav::document::discover(INPUT).perform();

    meta.m_sections_tree.visit_all([](const auto& intv) {
        auto ser = intv.value.match(
            [](const std::string& name) { return name; },
            [](const size_t index) { return fmt::format("{}", index); });
        printf("interval %d:%d %s\n", intv.start, intv.stop, ser.c_str());
    });
    lnav::document::hier_node::depth_first(
        meta.m_sections_root.get(), [](const auto* node) {
            printf("node %p %d\n", node, node->hn_start);
            for (const auto& pair : node->hn_named_children) {
                printf("  child: %p %s\n", pair.second, pair.first.c_str());
            }
        });
}

TEST_CASE("lnav::document::sections::doc")
{
    attr_line_t INPUT = R"(

NAME
    foo -- bar

SYNOPSIS
    foo -o -b

DESCRIPTION
    Lorem ipsum

   AbcDef
      Lorem ipsum

)";

    auto meta = lnav::document::discover(INPUT)
                    .with_text_format(text_format_t::TF_MAN)
                    .perform();

    CHECK(meta.m_sections_root->hn_named_children.size() == 3);
    meta.m_sections_tree.visit_all([](const auto& intv) {
        auto ser = intv.value.match(
            [](const std::string& name) { return name; },
            [](const size_t index) { return fmt::format("{}", index); });
        printf("interval %d:%d %s\n", intv.start, intv.stop, ser.c_str());
    });
    lnav::document::hier_node::depth_first(
        meta.m_sections_root.get(), [](const auto* node) {
            printf("node %p %d\n", node, node->hn_start);
            for (const auto& pair : node->hn_named_children) {
                printf("  child: %p %s\n", pair.second, pair.first.c_str());
            }
        });
}

TEST_CASE("lnav::document::sections::doc for diff")
{
    attr_line_t INPUT = R"(
[sql] add json_group_object aggregate function

diff --git a/NEWS b/NEWS
index d239d2f..7a06070 100644
--- a/NEWS
+++ b/NEWS
@@ -4,6 +4,8 @@ lnav v0.8.1:
      * Log formats can now create SQL views and execute other statements
        by adding '.sql' files to their format directories.  The SQL scripts
        will be executed on startup.
+     * Added a 'json_group_object' aggregate SQL function that collects values
+       from a GROUP BY query into an JSON object.

      Interface Changes:
      * The 'o/O' hotkeys have been reassigned to navigate through log
diff --git a/configure.ac b/configure.ac
index 718a2d4..10f5580 100644
--- a/configure.ac
+++ b/configure.ac
@@ -39,8 +39,8 @@ AC_PROG_CXX

 CPPFLAGS="$CPPFLAGS -D_ISOC99_SOURCE -D__STDC_LIMIT_MACROS"

-# CFLAGS=`echo $CFLAGS | sed 's/-O2//g'`
-# CXXFLAGS=`echo $CXXFLAGS | sed 's/-O2//g'`
+CFLAGS=`echo $CFLAGS | sed 's/-O2//g'`
+CXXFLAGS=`echo $CXXFLAGS | sed 's/-O2//g'`

 AC_ARG_VAR(SFTP_TEST_URL)
)";

    auto meta = lnav::document::discover(INPUT).perform();

    meta.m_sections_tree.visit_all([](const auto& intv) {
        auto ser = intv.value.match(
            [](const std::string& name) { return name; },
            [](const size_t index) { return fmt::format("{}", index); });
        printf("interval %d:%d %s\n", intv.start, intv.stop, ser.c_str());
    });
    lnav::document::hier_node::depth_first(
        meta.m_sections_root.get(), [](const auto* node) {
            printf("node %p %d\n", node, node->hn_start);
            for (const auto& pair : node->hn_named_children) {
                printf("  child: %p %s\n", pair.second, pair.first.c_str());
            }
        });

    CHECK(meta.m_sections_root->hn_named_children.size() == 2);
}

TEST_CASE("lnav::document::sections::doc for SQL")
{
    attr_line_t INPUT = R"(
CREATE TABLE IF NOT EXISTS http_status_codes
(
    status  INTEGER PRIMARY KEY,
    message TEXT,

    FOREIGN KEY (status) REFERENCES access_log (sc_status)
);

CREATE TABLE lnav_example_log
(
    log_line        INTEGER PRIMARY KEY,
    log_part        TEXT COLLATE naturalnocase,
    log_time        DATETIME,
    log_actual_time DATETIME hidden,
    log_idle_msecs  int,
    log_level       TEXT collate loglevel,
    log_mark        boolean,
    log_comment     TEXT,
    log_tags        TEXT,
    log_filters     TEXT,

    ex_procname     TEXT collate 'BINARY',
    ex_duration     INTEGER,

    log_time_msecs  int hidden,
    log_path        TEXT hidden collate naturalnocase,
    log_text        TEXT hidden,
    log_body        TEXT hidden
);
)";

    auto meta = lnav::document::discover(INPUT).perform();

    for (const auto& sa : INPUT.al_attrs) {
        printf("attr %d:%d %s\n",
               sa.sa_range.lr_start,
               sa.sa_range.lr_end,
               sa.sa_type->sat_name);
    }
    meta.m_sections_tree.visit_all([](const auto& intv) {
        auto ser = intv.value.match(
            [](const std::string& name) { return name; },
            [](const size_t index) { return fmt::format("{}", index); });
        printf("interval %d:%d %s\n", intv.start, intv.stop, ser.c_str());
    });
    lnav::document::hier_node::depth_first(
        meta.m_sections_root.get(), [](const auto* node) {
            printf("node %p %d\n", node, node->hn_start);
            for (const auto& pair : node->hn_named_children) {
                printf("  child: %p %s\n", pair.second, pair.first.c_str());
            }
        });

    CHECK(meta.m_sections_root->hn_named_children.size() == 2);
}

TEST_CASE("lnav::document::sections::sql")
{
    attr_line_t INPUT
        = R"(2022-06-03T22:05:58.186Z verbose -[35642] [Originator@6876 sub=Default] [VdbStatement]Executing SQL:
-->       INSERT INTO PM_CLUSTER_DRAFT_VALIDATION_STATE
-->         (draft_id, errors, hosts) VALUES (?::integer, ?::jsonb, ARRAY[]::text[])
-->         ON CONFLICT (draft_id) DO UPDATE
-->           SET errors = EXCLUDED.errors, hosts = EXCLUDED.hosts
-->
)";

    auto meta = lnav::document::discover(INPUT).perform();

    meta.m_sections_tree.visit_all([](const auto& intv) {
        auto ser = intv.value.match(
            [](const std::string& name) { return name; },
            [](const size_t index) { return fmt::format("{}", index); });
        printf("interval %d:%d %s\n", intv.start, intv.stop, ser.c_str());
    });
    lnav::document::hier_node::depth_first(
        meta.m_sections_root.get(), [](const auto* node) {
            printf("node %p %d\n", node, node->hn_start);
            for (const auto& pair : node->hn_named_children) {
                printf("  child: %p %s\n", pair.second, pair.first.c_str());
            }
        });
}

TEST_CASE("lnav::document::sections::afl1")
{
    attr_line_t INPUT = "{(</:>(\n---\x00\n+++\x00\n(";

    auto meta = lnav::document::discover(INPUT).perform();

    CHECK(meta.m_sections_root->hn_children.empty());
}

TEST_CASE("lnav::document::sections::afl2")
{
    attr_line_t INPUT = "{(</:>(\n---\x000\n+++\x000\n0";

    auto meta = lnav::document::discover(INPUT).perform();

    CHECK(meta.m_sections_root->hn_children.empty());
}

TEST_CASE("lnav::document::sections::afl3")
{
    attr_line_t INPUT = "0\x5b\n\n\x1b[70O[";

    scrub_ansi_string(INPUT.al_string, &INPUT.al_attrs);

    data_scanner ds(INPUT.al_string);
    pretty_printer pp(&ds, INPUT.al_attrs);
    attr_line_t pretty_al;

    pp.append_to(pretty_al);
    for (const auto& sa : pretty_al.al_attrs) {
        require(sa.sa_range.lr_end == -1
                || sa.sa_range.lr_start
                    <= sa.sa_range.lr_end);
    }
}
