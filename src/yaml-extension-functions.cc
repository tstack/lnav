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

#include "base/itertools.hh"
#include "ryml_all.hpp"
#include "sqlite-extension-func.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"

using namespace lnav::roles::literals;

static void
ryml_error_to_um(const char* msg, size_t len, ryml::Location loc, void* ud)
{
    intern_string_t src = intern_string::lookup(
        string_fragment::from_bytes(loc.name.data(), loc.name.size()));
    auto& sf = *(static_cast<string_fragment*>(ud));
    auto msg_str = string_fragment::from_bytes(msg, len).trim().to_string();

    if (loc.offset == sf.length()) {
        loc.line -= 1;
    }
    auto snippet_line
        = sf.find_left_boundary(loc.offset, string_fragment::tag1{'\n'})
              .find_right_boundary(0, string_fragment::tag1{'\n'});
    throw lnav::console::user_message::error("failed to parse YAML content")
        .with_reason(msg_str)
        .with_snippet(lnav::console::snippet::from(
            source_location{src, (int32_t) loc.line},
            snippet_line.to_string()));
}

static json_string
yaml_to_json(string_fragment in)
{
    ryml::Callbacks callbacks(&in, nullptr, nullptr, ryml_error_to_um);

    ryml::set_callbacks(callbacks);
    auto tree = ryml::parse_in_arena(
        "input", ryml::csubstr{in.data(), (size_t) in.length()});

    auto output = ryml::emit_json(
        tree, tree.root_id(), ryml::substr{}, /*error_on_excess*/ false);
    auto buf = auto_buffer::alloc(output.len);
    buf.resize(output.len);
    output = ryml::emit_json(tree,
                             tree.root_id(),
                             ryml::substr(buf.in(), buf.size()),
                             /*error_on_excess*/ true);

    return json_string{std::move(buf)};
}

int
yaml_extension_functions(struct FuncDef** basic_funcs,
                         struct FuncDefAgg** agg_funcs)
{
    static struct FuncDef yaml_funcs[] = {
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
                })),

        {nullptr},
    };

    *basic_funcs = yaml_funcs;

    return SQLITE_OK;
}
