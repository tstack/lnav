/**
 * Copyright (c) 2020, Timothy Stack
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
 */

#include "config.h"

#include <sstream>
#include <unordered_map>

#include "base/lnav_log.hh"
#include "pugixml/pugixml.hpp"
#include "sql_util.hh"
#include "xml_util.hh"
#include "vtab_module.hh"
#include "yajlpp/yajlpp.hh"

using namespace std;

enum {
    XP_COL_RESULT,
    XP_COL_NODE_PATH,
    XP_COL_NODE_ATTR,
    XP_COL_NODE_TEXT,
    XP_COL_XPATH,
    XP_COL_VALUE,
};

static
thread_local std::unordered_map<std::string, pugi::xpath_query> QUERY_CACHE;

static
pugi::xpath_query checkout_query(const std::string& query)
{
    auto iter = QUERY_CACHE.find(query);
    if (iter == QUERY_CACHE.end()) {
        auto xquery = pugi::xpath_query(query.c_str());

        if (!xquery) {
            return xquery;
        }

        auto pair = QUERY_CACHE.emplace(query, std::move(xquery));

        iter = pair.first;
    }

    auto retval = std::move(iter->second);

    QUERY_CACHE.erase(iter);

    return retval;
}

static
void checkin_query(const std::string& query_str, pugi::xpath_query query)
{
    if (!query) {
        return;
    }

    QUERY_CACHE[query_str] = std::move(query);
}

struct xpath_vtab {
    static constexpr const char *NAME = "xpath";
    static constexpr const char *CREATE_STMT = R"(
-- The xpath() table-valued function allows you to execute an xpath expression
CREATE TABLE xpath (
    result text,        -- The result of the xpath expression
    node_path text,     -- The absolute path to the node selected by the expression
    node_attr text,     -- The node attributes stored in a JSON object
    node_text text,     -- The text portion of the node selected by the expression

    xpath text HIDDEN,
    value text HIDDEN
);
)";

    struct cursor {
        sqlite3_vtab_cursor base;
        sqlite3_int64 c_rowid{0};
        string c_xpath;
        string c_value;
        bool c_value_as_blob{false};
        pugi::xpath_query c_query;
        pugi::xml_document c_doc;
        pugi::xpath_node_set c_results;

        cursor(sqlite3_vtab *vt)
            : base({vt}) {
        };

        ~cursor() {
            this->reset();
        }

        int reset() {
            this->c_rowid = 0;
            checkin_query(this->c_xpath, std::move(this->c_query));

            return SQLITE_OK;
        };

        int next() {
            this->c_rowid += 1;

            return SQLITE_OK;
        };

        int eof() {
            return this->c_rowid >= (int64_t) this->c_results.size();
        };

        int get_rowid(sqlite3_int64 &rowid_out) {
            rowid_out = this->c_rowid;

            return SQLITE_OK;
        };
    };

    int get_column(const cursor &vc, sqlite3_context *ctx, int col) {
        switch (col) {
            case XP_COL_RESULT: {
                auto& xpath_node = vc.c_results[vc.c_rowid];

                if (xpath_node.node()) {
                    ostringstream oss;

                    // XXX avoid the extra allocs
                    xpath_node.node().print(oss);
                    auto node_xml = oss.str();
                    sqlite3_result_text(ctx,
                                        node_xml.c_str(),
                                        node_xml.length(),
                                        SQLITE_TRANSIENT);
                } else if (xpath_node.attribute()) {
                    sqlite3_result_text(ctx,
                                        xpath_node.attribute().value(),
                                        -1,
                                        SQLITE_TRANSIENT);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case XP_COL_NODE_PATH: {
                auto& xpath_node = vc.c_results[vc.c_rowid];
                auto x_node = xpath_node.node();
                auto x_attr = xpath_node.attribute();

                if (x_node || x_attr) {
                    if (!x_node) {
                        x_node = xpath_node.parent();
                    }

                    auto node_path = lnav::pugixml::get_actual_path(x_node);
                    if (x_attr) {
                        node_path += "/@" + std::string(x_attr.name());
                    }
                    sqlite3_result_text(ctx,
                                        node_path.c_str(),
                                        node_path.length(),
                                        SQLITE_TRANSIENT);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case XP_COL_NODE_ATTR: {
                auto& xpath_node = vc.c_results[vc.c_rowid];
                auto x_node = xpath_node.node();
                auto x_attr = xpath_node.attribute();

                if (x_node || x_attr) {
                    if (!x_node) {
                        x_node = xpath_node.parent();
                    }

                    yajlpp_gen gen;

                    yajl_gen_config(gen, yajl_gen_beautify, false);

                    {
                        yajlpp_map attrs(gen);

                        for (const auto& attr : x_node.attributes()) {
                            attrs.gen(attr.name());
                            attrs.gen(attr.value());
                        }
                    }

                    auto sf = gen.to_string_fragment();

                    sqlite3_result_text(ctx,
                                        sf.data(),
                                        sf.length(),
                                        SQLITE_TRANSIENT);
                    sqlite3_result_subtype(ctx, 'J');
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case XP_COL_NODE_TEXT: {
                auto& xpath_node = vc.c_results[vc.c_rowid];
                auto x_node = xpath_node.node();
                auto x_attr = xpath_node.attribute();

                if (x_node || x_attr) {
                    if (!x_node) {
                        x_node = xpath_node.parent();
                    }

                    auto node_text = x_node.text();

                    sqlite3_result_text(ctx,
                                        node_text.get(),
                                        -1,
                                        SQLITE_TRANSIENT);
                } else {
                    sqlite3_result_null(ctx);
                }
                break;
            }
            case XP_COL_XPATH:
                sqlite3_result_text(ctx,
                                    vc.c_xpath.c_str(),
                                    vc.c_xpath.length(),
                                    SQLITE_STATIC);
                break;
            case XP_COL_VALUE:
                if (vc.c_value_as_blob) {
                    sqlite3_result_blob64(ctx,
                                          vc.c_value.c_str(),
                                          vc.c_value.length(),
                                          SQLITE_STATIC);
                } else {
                    sqlite3_result_text(ctx,
                                        vc.c_value.c_str(),
                                        vc.c_value.length(),
                                        SQLITE_STATIC);
                }
                break;
        }

        return SQLITE_OK;
    }
};

static int rcBestIndex(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo)
{
    vtab_index_constraints vic(pIdxInfo);
    vtab_index_usage viu(pIdxInfo);

    for (auto iter = vic.begin(); iter != vic.end(); ++iter) {
        if (iter->op != SQLITE_INDEX_CONSTRAINT_EQ) {
            continue;
        }

        switch (iter->iColumn) {
            case XP_COL_VALUE:
            case XP_COL_XPATH:
                viu.column_used(iter);
                break;
        }
    }

    viu.allocate_args(2);
    return SQLITE_OK;
}

static int rcFilter(sqlite3_vtab_cursor *pVtabCursor,
                    int idxNum, const char *idxStr,
                    int argc, sqlite3_value **argv)
{
    auto *pCur = (xpath_vtab::cursor *)pVtabCursor;

    if (argc != 2) {
        pCur->c_xpath.clear();
        pCur->c_value.clear();
        return SQLITE_OK;
    }

    pCur->c_value_as_blob = (sqlite3_value_type(argv[1]) == SQLITE_BLOB);
    auto byte_count = sqlite3_value_bytes(argv[1]);

    if (byte_count == 0) {
        pCur->c_rowid = 0;
        return SQLITE_OK;
    }

    auto blob = (const char *) sqlite3_value_blob(argv[1]);
    pCur->c_value.assign(blob, byte_count);
    auto parse_res = pCur->c_doc.load_string(pCur->c_value.c_str());
    if (!parse_res) {
        pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf(
            "Invalid XML document at offset %d: %s",
            parse_res.offset, parse_res.description());
        return SQLITE_ERROR;
    }

    pCur->c_xpath = (const char *) sqlite3_value_text(argv[0]);
    pCur->c_query = checkout_query(pCur->c_xpath);
    if (!pCur->c_query) {
        auto& res = pCur->c_query.result();
        pVtabCursor->pVtab->zErrMsg = sqlite3_mprintf(
            "Invalid XPATH expression at offset %d: %s",
            res.offset, res.description());
        return SQLITE_ERROR;
    }

    pCur->c_rowid = 0;
    pCur->c_results = pCur->c_doc.select_nodes(pCur->c_query);

    return SQLITE_OK;
}

int register_xpath_vtab(sqlite3 *db)
{
    static vtab_module<tvt_no_update<xpath_vtab>> XPATH_MODULE;
    static help_text xpath_help = help_text("xpath",
        "A table-valued function that executes an xpath expression over an XML "
        "string and returns the selected values.")
        .sql_table_valued_function()
        .with_parameter({"xpath",
                         "The XPATH expression to evaluate over the XML document."})
        .with_parameter({"xmldoc",
                         "The XML document as a string."})
        .with_result({"result",
                      "The result of the XPATH expression."})
        .with_result({"node_path",
                      "The absolute path to the node containing the result."})
        .with_result({"node_attr",
                      "The node's attributes stored in JSON object."})
        .with_result({"node_text",
                      "The node's text value."})
        .with_tags({"string", "xml"})
        .with_example({
            "To select the XML nodes on the path '/abc/def'",
            "SELECT * FROM xpath('/abc/def', '<abc><def a=\"b\">Hello</def><def>Bye</def></abc>')"
        })
        .with_example({
            "To select all 'a' attributes on the path '/abc/def'",
            "SELECT * FROM xpath('/abc/def/@a', '<abc><def a=\"b\">Hello</def><def>Bye</def></abc>')"
        })
        .with_example({
            "To select the text nodes on the path '/abc/def'",
            "SELECT * FROM xpath('/abc/def/text()', '<abc><def a=\"b\">Hello &#x2605;</def></abc>')"
        });

    int rc;

    XPATH_MODULE.vm_module.xBestIndex = rcBestIndex;
    XPATH_MODULE.vm_module.xFilter = rcFilter;

    rc = XPATH_MODULE.create(db, "xpath");
    sqlite_function_help.insert(make_pair("xpath", &xpath_help));
    xpath_help.index_tags();

    ensure(rc == SQLITE_OK);

    return rc;
}
