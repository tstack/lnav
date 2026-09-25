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
 *
 * @file yaml-extension-functions.cc
 */

#include <string>

#define RYML_SINGLE_HDR_DEFINE_NOW

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/lnav.console.hh"
#include "base/lnav.ryml.hh"
#include "fmt/format.h"
#include "scn/scan.h"
#include "sqlite-extension-func.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"

using namespace lnav::roles::literals;

static constexpr auto INPUT_NAME = "input"_frag;

static void
ryml_error_to_um(const char* msg, size_t len, ryml::Location loc, void* ud)
{
    const auto& sf = *(static_cast<const string_fragment*>(ud));
    // Some messages count their NUL terminator in the length.
    while (len > 0 && msg[len - 1] == '\0') {
        len -= 1;
    }
    auto msg_str = string_fragment::from_bytes(msg, len).trim().to_string();
    auto um = lnav::console::user_message::error("failed to parse YAML content")
                  .with_reason(msg_str)
                  .move();

    // Errors that do not come from the parser, like the emitter's, have the
    // location of the ryml source instead of the input.
    if (string_fragment::from_bytes(loc.name.data(), loc.name.size())
            == INPUT_NAME
        && (ssize_t) loc.offset <= sf.length())
    {
        if ((ssize_t) loc.offset == sf.length() && loc.line > 0) {
            loc.line -= 1;
        }
        auto snippet_line
            = sf.find_left_boundary(loc.offset, string_fragment::tag1{'\n'})
                  .find_right_boundary(0, string_fragment::tag1{'\n'});
        um.with_snippet(lnav::console::snippet::from(
            source_location{intern_string::lookup(INPUT_NAME),
                            (int32_t) loc.line},
            snippet_line.to_string()));
    }

    throw um;
}

/**
 * ryml only writes "null", "true", "false", and decimal numbers without
 * quotes, so rewrite the other plain spellings that the YAML 1.2 core schema
 * gives those types.
 */
static void
normalize_scalars(ryml::Tree& tree, size_t node)
{
    if (tree.has_val(node) && !tree.is_val_quoted(node)) {
        const auto val = tree.val(node);

        if (val.empty() || val == "~" || val == "Null" || val == "NULL") {
            tree.set_val(node, "null");
        } else if (val == "True" || val == "TRUE") {
            tree.set_val(node, "true");
        } else if (val == "False" || val == "FALSE") {
            tree.set_val(node, "false");
        } else if (val.len > 2
                   && (val.begins_with("0x") || val.begins_with("0o")))
        {
            const auto base = val.begins_with("0x") ? 16 : 8;
            auto scan_res = scn::scan_int<int64_t>(
                std::string_view{val.str + 2, val.len - 2}, base);

            if (scan_res && scan_res->range().empty()) {
                auto dec = fmt::to_string(scan_res->value());

                tree.set_val(node, tree.to_arena(ryml::to_csubstr(dec)));
            }
        }
    }

    for (auto child = tree.first_child(node); child != ryml::NONE;
         child = tree.next_sibling(child))
    {
        normalize_scalars(tree, child);
    }
}

static json_string
yaml_to_json(string_fragment in)
{
    // The callbacks belong to this parser and the tree it makes, not to ryml
    // as a whole, since this can run on more than one thread at a time.
    ryml::Callbacks callbacks(&in, nullptr, nullptr, ryml_error_to_um);
    ryml::Parser parser(callbacks);
    auto tree = parser.parse_in_arena(ryml::to_csubstr(INPUT_NAME.data()),
                                      lnav::ryml::to_csubstr(in));

    tree.resolve();
    normalize_scalars(tree, tree.root_id());

    std::string output;
    const auto root = tree.root_id();
    if (tree.is_stream(root)) {
        // JSON has no streams, so each document becomes an array element.
        output.push_back('[');
        for (auto doc = tree.first_child(root); doc != ryml::NONE;
             doc = tree.next_sibling(doc))
        {
            if (output.size() > 1) {
                output.push_back(',');
            }
            output.append(ryml::emitrs_json<std::string>(tree, doc));
        }
        output.push_back(']');
    } else {
        output = ryml::emitrs_json<std::string>(tree, root);
    }

    return json_string{auto_buffer::from(output.data(), output.size())};
}

int
yaml_extension_functions(FuncDef** basic_funcs, FuncDefAgg** agg_funcs)
{
    static FuncDef yaml_funcs[] = {
        sqlite_func_adapter<decltype(&yaml_to_json), yaml_to_json>::builder(
            help_text("yaml_to_json",
                      "Convert a YAML document to a JSON-encoded string")
                .sql_function()
                .with_prql_path({"yaml", "to_json"})
                .with_parameter({"yaml", "The YAML value to convert to JSON."})
                .with_tags({"json", "yaml"})
                .with_example({
                    "To convert the document \"abc: def\"",
                    "SELECT yaml_to_json('abc: def')",
                }))
            .with_result_subtype(),

        {nullptr},
    };

    *basic_funcs = yaml_funcs;
    *agg_funcs = nullptr;

    return SQLITE_OK;
}
