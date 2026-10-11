/**
 * Copyright (c) 2026, Timothy Stack
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
 * THIS SOFTWARE IS PROVIDED BY TIMOTHY STACK AND CONTRIBUTORS ''AS IS'' AND ANY
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
 * @file json_schema_builder.cc
 */

#include <algorithm>
#include <cctype>

#include "yajlpp/json_schema_builder.hh"

#include "base/auto_mem.hh"
#include "base/humanize.hh"
#include "config.h"
#include "yajl/api/yajl_parse.h"
#include "yajlpp/yajlpp.hh"

namespace {

/** Checks that the text is a number, -?[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)? */
bool
is_number(string_fragment value)
{
    size_t pos = 0;
    auto digits = [&]() {
        auto start = pos;
        while (pos < value.length() && isdigit(value[pos])) {
            pos += 1;
        }
        return pos > start;
    };
    if (pos < value.length() && value[pos] == '-') {
        pos += 1;
    }
    auto ok = digits();
    if (ok && pos < value.length() && value[pos] == '.') {
        pos += 1;
        ok = digits();
    }
    if (ok && pos < value.length() && (value[pos] == 'e' || value[pos] == 'E'))
    {
        pos += 1;
        if (pos < value.length() && (value[pos] == '-' || value[pos] == '+')) {
            pos += 1;
        }
        ok = digits();
    }

    return ok && pos == value.length();
}

}  // namespace

struct json_schema_builder_callbacks {
    static json_schema_builder::node* next(void* ctx)
    {
        return static_cast<json_schema_builder*>(ctx)->next_node();
    }

    static int handle_null(void* ctx)
    {
        next(ctx)->n_null = true;
        return 1;
    }

    static int handle_boolean(void* ctx, int)
    {
        next(ctx)->n_boolean = true;
        return 1;
    }

    static int handle_number(void* ctx, const char*, size_t)
    {
        next(ctx)->n_number = true;
        return 1;
    }

    static int handle_string(void* ctx,
                             const unsigned char* str,
                             size_t len,
                             yajl_string_props_t*)
    {
        auto* jsb = static_cast<json_schema_builder*>(ctx);
        auto* n = next(ctx);
        auto text = string_fragment::from_bytes(str, len);

        if (!n->n_example) {
            n->n_example = text.to_string();
        }
        if (jsb->jsb_options.o_scalar_strings) {
            if (text == "null") {
                n->n_null = true;
                return 1;
            }
            if (text == "true" || text == "false") {
                n->n_boolean = true;
                // It's folded into the string type if there are strings.
                n->n_non_measure = true;
                return 1;
            }
            if (is_number(text)) {
                n->n_number = true;
                return 1;
            }
        }

        n->n_string = true;
        auto measure = humanize::try_from<double>(text);
        if (!measure) {
            n->n_non_measure = true;
        } else if (!measure->unit_suffix.empty()) {
            n->n_measure = true;
        }
        return 1;
    }

    static int handle_start_map(void* ctx)
    {
        auto* jsb = static_cast<json_schema_builder*>(ctx);
        auto* n = next(ctx);

        n->n_object = true;
        jsb->jsb_stack.emplace_back(json_schema_builder::frame{n, false});
        return 1;
    }

    static int handle_map_key(void* ctx,
                              const unsigned char* key,
                              size_t len,
                              yajl_string_props_t*)
    {
        auto* jsb = static_cast<json_schema_builder*>(ctx);

        jsb->jsb_key.assign((const char*) key, len);
        return 1;
    }

    static int handle_start_array(void* ctx)
    {
        auto* jsb = static_cast<json_schema_builder*>(ctx);
        auto* n = next(ctx);

        n->n_array = true;
        jsb->jsb_stack.emplace_back(json_schema_builder::frame{n, true});
        return 1;
    }

    static int handle_end_container(void* ctx)
    {
        auto* jsb = static_cast<json_schema_builder*>(ctx);

        jsb->jsb_stack.pop_back();
        return 1;
    }

    static const yajl_callbacks CALLBACKS;
};

const yajl_callbacks json_schema_builder_callbacks::CALLBACKS = {
    handle_null,
    handle_boolean,
    nullptr,
    nullptr,
    handle_number,
    handle_string,
    handle_start_map,
    handle_map_key,
    handle_end_container,
    handle_start_array,
    handle_end_container,
};

const json_schema_builder::node*
json_schema_builder::node::property(string_fragment name) const
{
    for (const auto& prop : this->n_properties) {
        if (name == prop.n_name) {
            return &prop;
        }
    }

    return nullptr;
}

json_schema_builder::node*
json_schema_builder::next_node()
{
    if (this->jsb_stack.empty()) {
        return &this->jsb_root;
    }

    auto& top = this->jsb_stack.back();
    if (top.f_array) {
        if (!this->jsb_options.o_tuples) {
            if (!top.f_node->n_items) {
                top.f_node->n_items = std::make_unique<node>();
            }
            return top.f_node->n_items.get();
        }

        auto index = top.f_index++;
        if (top.f_node->n_prefix_items.size() <= index) {
            top.f_node->n_prefix_items.resize(index + 1);
        }
        return &top.f_node->n_prefix_items[index];
    }

    auto& props = top.f_node->n_properties;
    auto iter = std::find_if(props.begin(), props.end(), [this](const auto& p) {
        return p.n_name == this->jsb_key;
    });
    if (iter == props.end()) {
        props.emplace_back();
        props.back().n_name = this->jsb_key;
        iter = std::prev(props.end());
    }
    return &(*iter);
}

Result<void, std::string>
json_schema_builder::add_sample(string_fragment json)
{
    static const yajl_callbacks NO_CALLBACKS = {};

    // Validate first, so an invalid sample adds nothing to the tree.
    auto check_handle = yajlpp::alloc_handle(&NO_CALLBACKS, nullptr);
    auto rc = yajl_parse(check_handle, json.udata(), json.length());
    if (rc == yajl_status_ok) {
        rc = yajl_complete_parse(check_handle);
    }
    if (rc != yajl_status_ok) {
        auto* msg
            = yajl_get_error(check_handle, 1, json.udata(), json.length());
        auto retval = std::string((const char*) msg);

        yajl_free_error(check_handle, msg);
        return Err(retval);
    }

    auto handle
        = yajlpp::alloc_handle(&json_schema_builder_callbacks::CALLBACKS, this);

    this->jsb_stack.clear();
    yajl_parse(handle, json.udata(), json.length());
    yajl_complete_parse(handle);

    this->jsb_samples += 1;
    return Ok();
}

void
json_schema_builder::gen_keywords(yajl_gen gen, const node& n) const
{
    yajlpp_generator g(gen);
    auto scalar_strings = this->jsb_options.o_scalar_strings && n.n_string;
    std::vector<const char*> types;

    if (n.n_object) {
        types.emplace_back("object");
    }
    if (n.n_array) {
        types.emplace_back("array");
    }
    if (n.n_string) {
        types.emplace_back("string");
    }
    if (n.n_number && !scalar_strings) {
        types.emplace_back("number");
    }
    if (n.n_boolean && !scalar_strings) {
        types.emplace_back("boolean");
    }
    if (n.n_null) {
        types.emplace_back("null");
    }

    if (types.size() == 1) {
        g("type");
        g(types.front());
    } else if (!types.empty()) {
        g("type");
        g(types);
    }
    if (n.n_string && n.n_measure && !n.n_non_measure) {
        g("format");
        g(MEASURE_FORMAT);
    }

    if (!n.n_properties.empty()) {
        g("properties");
        yajlpp_map props(gen);
        for (const auto& prop : n.n_properties) {
            props.gen(prop.n_name);
            yajlpp_map sub(gen);
            this->gen_keywords(gen, prop);
        }
    }

    if (!n.n_prefix_items.empty()) {
        g("prefixItems");
        {
            yajlpp_array items(gen);
            for (const auto& item : n.n_prefix_items) {
                yajlpp_map sub(gen);
                this->gen_keywords(gen, item);
            }
        }
    }
    if (n.n_array && this->jsb_options.o_tuples) {
        g("items");
        g(false);
    } else if (n.n_items) {
        g("items");
        yajlpp_map sub(gen);
        this->gen_keywords(gen, *n.n_items);
    }
}

std::string
json_schema_builder::to_string() const
{
    yajlpp_gen gen;

    yajl_gen_config(gen, yajl_gen_beautify, false);
    {
        yajlpp_map root(gen);

        root.gen("$schema");
        root.gen(SCHEMA_URI);
        this->gen_keywords(gen, this->jsb_root);
    }

    return gen.to_string_fragment().to_string();
}
