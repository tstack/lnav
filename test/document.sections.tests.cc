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
#include "doctest/doctest.h"
#include "document.sections.hh"

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

    auto meta = lnav::document::discover_structure(INPUT, line_range{0, -1});

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

    auto meta = lnav::document::discover_structure(INPUT, line_range{0, -1});

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

    auto meta = lnav::document::discover_structure(INPUT, line_range{0, -1});

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

    auto meta = lnav::document::discover_structure(INPUT, line_range{0, -1});

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
