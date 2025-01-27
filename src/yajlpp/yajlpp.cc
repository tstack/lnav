/**
 * Copyright (c) 2015, Timothy Stack
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
 * @file yajlpp.cc
 */

#include <filesystem>
#include <regex>
#include <utility>

#include "yajlpp.hh"

#include "config.h"
#include "fmt/format.h"
#include "yajl/api/yajl_parse.h"
#include "yajlpp_def.hh"

const json_path_handler_base::enum_value_t
    json_path_handler_base::ENUM_TERMINATOR((const char*) nullptr, 0);

yajl_gen_status
yajl_gen_tree(yajl_gen hand, yajl_val val)
{
    switch (val->type) {
        case yajl_t_string: {
            return yajl_gen_string(hand, YAJL_GET_STRING(val));
        }
        case yajl_t_number: {
            if (YAJL_IS_INTEGER(val)) {
                return yajl_gen_integer(hand, YAJL_GET_INTEGER(val));
            }
            if (YAJL_IS_DOUBLE(val)) {
                return yajl_gen_double(hand, YAJL_GET_DOUBLE(val));
            }
            return yajl_gen_number(
                hand, YAJL_GET_NUMBER(val), strlen(YAJL_GET_NUMBER(val)));
        }
        case yajl_t_object: {
            auto rc = yajl_gen_map_open(hand);
            if (rc != yajl_gen_status_ok) {
                return rc;
            }
            for (size_t lpc = 0; lpc < YAJL_GET_OBJECT(val)->len; lpc++) {
                rc = yajl_gen_string(hand, YAJL_GET_OBJECT(val)->keys[lpc]);
                if (rc != yajl_gen_status_ok) {
                    return rc;
                }
                rc = yajl_gen_tree(hand, YAJL_GET_OBJECT(val)->values[lpc]);
                if (rc != yajl_gen_status_ok) {
                    return rc;
                }
            }
            rc = yajl_gen_map_close(hand);
            if (rc != yajl_gen_status_ok) {
                return rc;
            }
            return yajl_gen_status_ok;
        }
        case yajl_t_array: {
            auto rc = yajl_gen_array_open(hand);
            if (rc != yajl_gen_status_ok) {
                return rc;
            }
            for (size_t lpc = 0; lpc < YAJL_GET_ARRAY(val)->len; lpc++) {
                rc = yajl_gen_tree(hand, YAJL_GET_ARRAY(val)->values[lpc]);
                if (rc != yajl_gen_status_ok) {
                    return rc;
                }
            }
            rc = yajl_gen_array_close(hand);
            if (rc != yajl_gen_status_ok) {
                return rc;
            }
            return yajl_gen_status_ok;
        }
        case yajl_t_true: {
            return yajl_gen_bool(hand, true);
        }
        case yajl_t_false: {
            return yajl_gen_bool(hand, false);
        }
        case yajl_t_null: {
            return yajl_gen_null(hand);
        }
        default:
            return yajl_gen_status_ok;
    }
}

void
yajl_cleanup_tree(yajl_val val)
{
    if (YAJL_IS_OBJECT(val)) {
        auto* val_as_obj = YAJL_GET_OBJECT(val);

        for (size_t lpc = 0; lpc < val_as_obj->len;) {
            auto* child_val = val_as_obj->values[lpc];

            yajl_cleanup_tree(child_val);
            if (YAJL_IS_OBJECT(child_val)
                && YAJL_GET_OBJECT(child_val)->len == 0)
            {
                free((char*) val_as_obj->keys[lpc]);
                yajl_tree_free(val_as_obj->values[lpc]);
                val_as_obj->len -= 1;
                for (auto lpc2 = lpc; lpc2 < val_as_obj->len; lpc2++) {
                    val_as_obj->keys[lpc2] = val_as_obj->keys[lpc2 + 1];
                    val_as_obj->values[lpc2] = val_as_obj->values[lpc2 + 1];
                }
            } else {
                lpc++;
            }
        }
    }
}

json_path_handler_base::json_path_handler_base(const std::string& property)
    : jph_property(property.back() == '#'
                       ? property.substr(0, property.size() - 1)
                       : property),
      jph_regex(lnav::pcre2pp::code::from(lnav::pcre2pp::quote(property),
                                          PCRE2_ANCHORED)
                    .unwrap()
                    .to_shared()),
      jph_is_array(property.back() == '#')
{
    memset(&this->jph_callbacks, 0, sizeof(this->jph_callbacks));
}

static std::string
scrub_pattern(const std::string& pattern)
{
    static const std::regex CAPTURE(R"(\(\?\<\w+\>)");

    return std::regex_replace(pattern, CAPTURE, "(");
}

json_path_handler_base::json_path_handler_base(
    const std::shared_ptr<const lnav::pcre2pp::code>& property)
    : jph_property(scrub_pattern(property->get_pattern())), jph_regex(property),
      jph_is_array(property->get_pattern().find('#') != std::string::npos),
      jph_is_pattern_property(property->get_capture_count() > 0)
{
    memset(&this->jph_callbacks, 0, sizeof(this->jph_callbacks));
}

json_path_handler_base::json_path_handler_base(
    std::string property,
    const std::shared_ptr<const lnav::pcre2pp::code>& property_re)
    : jph_property(std::move(property)), jph_regex(property_re),
      jph_is_array(property_re->get_pattern().find('#') != std::string::npos)
{
    memset(&this->jph_callbacks, 0, sizeof(this->jph_callbacks));
}

yajl_gen_status
json_path_handler_base::gen(yajlpp_gen_context& ygc, yajl_gen handle) const
{
    if (this->jph_is_array) {
        auto size = this->jph_size_provider(ygc.ygc_obj_stack.top());
        auto md = lnav::pcre2pp::match_data::unitialized();

        yajl_gen_string(handle, this->jph_property);
        yajl_gen_array_open(handle);
        for (size_t index = 0; index < size; index++) {
            yajlpp_provider_context ypc{&md, index};
            yajlpp_gen_context elem_ygc(handle, *this->jph_children);
            elem_ygc.ygc_depth = 1;
            elem_ygc.ygc_obj_stack.push(
                this->jph_obj_provider(ypc, ygc.ygc_obj_stack.top()));

            elem_ygc.gen();
        }
        yajl_gen_array_close(handle);

        return yajl_gen_status_ok;
    }

    std::vector<std::string> local_paths;

    if (this->jph_path_provider) {
        this->jph_path_provider(ygc.ygc_obj_stack.top(), local_paths);
    } else {
        local_paths.emplace_back(this->jph_property);
    }

    if (this->jph_children) {
        for (const auto& lpath : local_paths) {
            std::string full_path = json_ptr::encode_str(lpath);
            int start_depth = ygc.ygc_depth;

            yajl_gen_string(handle, lpath);
            yajl_gen_map_open(handle);
            ygc.ygc_depth += 1;

            if (this->jph_obj_provider) {
                thread_local auto md = lnav::pcre2pp::match_data::unitialized();

                auto find_res
                    = this->jph_regex->capture_from(full_path).into(md).matches(
                        PCRE2_NO_UTF_CHECK);

                ygc.ygc_obj_stack.push(this->jph_obj_provider(
                    {&md, yajlpp_provider_context::nindex},
                    ygc.ygc_obj_stack.top()));
                if (!ygc.ygc_default_stack.empty()) {
                    ygc.ygc_default_stack.push(this->jph_obj_provider(
                        {&md, yajlpp_provider_context::nindex},
                        ygc.ygc_default_stack.top()));
                }
            }

            for (const auto& jph : this->jph_children->jpc_children) {
                yajl_gen_status status = jph.gen(ygc, handle);

                const unsigned char* buf;
                size_t len;
                yajl_gen_get_buf(handle, &buf, &len);
                if (status != yajl_gen_status_ok) {
                    log_error("yajl_gen failure for: %s -- %d",
                              jph.jph_property.c_str(),
                              status);
                    return status;
                }
            }

            if (this->jph_obj_provider) {
                ygc.ygc_obj_stack.pop();
                if (!ygc.ygc_default_stack.empty()) {
                    ygc.ygc_default_stack.pop();
                }
            }

            while (ygc.ygc_depth > start_depth) {
                yajl_gen_map_close(handle);
                ygc.ygc_depth -= 1;
            }
        }
    } else if (this->jph_gen_callback != nullptr) {
        return this->jph_gen_callback(ygc, *this, handle);
    }

    return yajl_gen_status_ok;
}

const char* const SCHEMA_TYPE_STRINGS[] = {
    "any",
    "boolean",
    "integer",
    "number",
    "string",
    "array",
    "object",
};

yajl_gen_status
json_path_handler_base::gen_schema(yajlpp_gen_context& ygc) const
{
    if (this->jph_children) {
        {
            yajlpp_map schema(ygc.ygc_handle);

            if (this->jph_description && this->jph_description[0]) {
                schema.gen("description");
                schema.gen(this->jph_description);
            }
            if (this->jph_is_pattern_property) {
                ygc.ygc_path.emplace_back(
                    fmt::format(FMT_STRING("<{}>"),
                                this->jph_regex->get_name_for_capture(1)));
            } else {
                ygc.ygc_path.emplace_back(this->jph_property);
            }
            if (this->jph_children->jpc_definition_id.empty()) {
                schema.gen("title");
                schema.gen(fmt::format(FMT_STRING("/{}"),
                                       fmt::join(ygc.ygc_path, "/")));
                schema.gen("type");
                if (this->jph_is_array) {
                    if (this->jph_regex->get_pattern().find("#?")
                        == std::string::npos)
                    {
                        schema.gen("array");
                    } else {
                        yajlpp_array type_array(ygc.ygc_handle);

                        type_array.gen("array");
                        for (auto schema_type : this->get_types()) {
                            type_array.gen(
                                SCHEMA_TYPE_STRINGS[(int) schema_type]);
                        }
                    }
                    schema.gen("items");
                    yajl_gen_map_open(ygc.ygc_handle);
                    yajl_gen_string(ygc.ygc_handle, "type");
                    this->gen_schema_type(ygc);
                } else {
                    this->gen_schema_type(ygc);
                }
                this->jph_children->gen_schema(ygc);
                if (this->jph_is_array) {
                    yajl_gen_map_close(ygc.ygc_handle);
                }
            } else {
                schema.gen("title");
                schema.gen(fmt::format(FMT_STRING("/{}"),
                                       fmt::join(ygc.ygc_path, "/")));
                this->jph_children->gen_schema(ygc);
            }
            ygc.ygc_path.pop_back();
        }
    } else {
        yajlpp_map schema(ygc.ygc_handle);

        if (this->jph_is_pattern_property) {
            ygc.ygc_path.emplace_back(fmt::format(
                FMT_STRING("<{}>"), this->jph_regex->get_name_for_capture(1)));
        } else {
            ygc.ygc_path.emplace_back(this->jph_property);
        }

        schema.gen("title");
        schema.gen(
            fmt::format(FMT_STRING("/{}"), fmt::join(ygc.ygc_path, "/")));
        if (this->jph_description && this->jph_description[0]) {
            schema.gen("description");
            schema.gen(this->jph_description);
        }

        schema.gen("type");

        if (this->jph_is_array) {
            if (this->jph_regex->get_pattern().find("#?") == std::string::npos)
            {
                schema.gen("array");
            } else {
                yajlpp_array type_array(ygc.ygc_handle);

                type_array.gen("array");
                for (auto schema_type : this->get_types()) {
                    type_array.gen(SCHEMA_TYPE_STRINGS[(int) schema_type]);
                }
            }
            yajl_gen_string(ygc.ygc_handle, "items");
            yajl_gen_map_open(ygc.ygc_handle);
            yajl_gen_string(ygc.ygc_handle, "type");
        }

        this->gen_schema_type(ygc);

        if (!this->jph_examples.empty()) {
            schema.gen("examples");

            yajlpp_array example_array(ygc.ygc_handle);
            for (const auto& ex : this->jph_examples) {
                example_array.gen(ex);
            }
        }

        if (this->jph_is_array) {
            yajl_gen_map_close(ygc.ygc_handle);
        }

        ygc.ygc_path.pop_back();
    }

    return yajl_gen_status_ok;
}

yajl_gen_status
json_path_handler_base::gen_schema_type(yajlpp_gen_context& ygc) const
{
    yajlpp_generator schema(ygc.ygc_handle);

    auto types = this->get_types();
    if (types.size() == 1) {
        yajl_gen_string(ygc.ygc_handle, SCHEMA_TYPE_STRINGS[(int) types[0]]);
    } else {
        yajlpp_array type_array(ygc.ygc_handle);

        for (auto schema_type : types) {
            type_array.gen(SCHEMA_TYPE_STRINGS[(int) schema_type]);
        }
    }

    for (auto& schema_type : types) {
        switch (schema_type) {
            case schema_type_t::STRING:
                if (this->jph_min_length > 0) {
                    schema("minLength");
                    schema(this->jph_min_length);
                }
                if (this->jph_max_length < INT_MAX) {
                    schema("maxLength");
                    schema(this->jph_max_length);
                }
                if (this->jph_pattern_re) {
                    schema("pattern");
                    schema(this->jph_pattern_re);
                }
                if (this->jph_enum_values) {
                    schema("enum");

                    yajlpp_array enum_array(ygc.ygc_handle);
                    for (int lpc = 0; this->jph_enum_values[lpc].first; lpc++) {
                        enum_array.gen(this->jph_enum_values[lpc].first);
                    }
                }
                break;
            case schema_type_t::INTEGER:
            case schema_type_t::NUMBER:
                if (this->jph_min_value > LLONG_MIN) {
                    schema("minimum");
                    schema(this->jph_min_value);
                }
                break;
            default:
                break;
        }
    }

    return yajl_gen_keys_must_be_strings;
}

void
json_path_handler_base::walk(
    const std::function<void(
        const json_path_handler_base&, const std::string&, const void*)>& cb,
    void* root,
    const std::string& base) const
{
    std::vector<std::string> local_paths;

    if (this->jph_path_provider) {
        this->jph_path_provider(root, local_paths);

        for (const auto& lpath : local_paths) {
            const void* field = nullptr;
            if (this->jph_field_getter) {
                field = this->jph_field_getter(root, lpath);
            }
            cb(*this,
               fmt::format(FMT_STRING("{}{}{}"),
                           base,
                           json_ptr::encode_str(lpath),
                           this->jph_children ? "/" : ""),
               field);
        }
        if (this->jph_obj_deleter) {
            local_paths.clear();
            this->jph_path_provider(root, local_paths);
        }
        if (this->jph_field_getter) {
            const auto* field = this->jph_field_getter(root, std::nullopt);
            if (field != nullptr) {
                cb(*this, base, field);
            }
        }
    } else {
        local_paths.emplace_back(this->jph_property);

        std::string full_path = base + this->jph_property;
        if (this->jph_children) {
            full_path += "/";

            const void* field = nullptr;
            if (this->jph_field_getter) {
                field = this->jph_field_getter(root, this->jph_property);
            }
            cb(*this, full_path, field);
        }
    }

    if (this->jph_children) {
        for (const auto& lpath : local_paths) {
            for (const auto& jph : this->jph_children->jpc_children) {
                static const intern_string_t POSS_SRC
                    = intern_string::lookup("possibilities");

                std::string full_path = base + json_ptr::encode_str(lpath);
                if (this->jph_children) {
                    full_path += "/";
                }
                json_path_container dummy{
                    json_path_handler(this->jph_property, this->jph_regex),
                };

                yajlpp_parse_context ypc(POSS_SRC, &dummy);
                void* child_root = root;

                ypc.set_path(full_path).with_obj(root).update_callbacks();
                if (this->jph_obj_provider) {
                    thread_local auto md
                        = lnav::pcre2pp::match_data::unitialized();

                    const auto short_path = json_ptr::encode_str(lpath) + "/";

                    if (!this->jph_regex->capture_from(short_path)
                             .into(md)
                             .matches(PCRE2_NO_UTF_CHECK)
                             .ignore_error())
                    {
                        log_error(
                            "path-handler regex (%s) does not match path: "
                            "%s",
                            this->jph_regex->get_pattern().c_str(),
                            full_path.c_str());
                        ensure(false);
                    }
                    child_root = this->jph_obj_provider(
                        {&md, yajlpp_provider_context::nindex}, root);
                }

                jph.walk(cb, child_root, full_path);
            }
        }
    } else {
        for (const auto& lpath : local_paths) {
            const void* field = nullptr;

            if (this->jph_field_getter) {
                field = this->jph_field_getter(root, lpath);
            }
            cb(*this, base + lpath, field);
        }
    }
}

std::optional<int>
json_path_handler_base::to_enum_value(const string_fragment& sf) const
{
    for (int lpc = 0; this->jph_enum_values[lpc].first; lpc++) {
        const auto& ev = this->jph_enum_values[lpc];

        if (sf == ev.first) {
            return ev.second;
        }
    }

    return std::nullopt;
}

const char*
json_path_handler_base::to_enum_string(int value) const
{
    for (int lpc = 0; this->jph_enum_values[lpc].first; lpc++) {
        const auto& ev = this->jph_enum_values[lpc];

        if (ev.second == value) {
            return ev.first;
        }
    }

    return "";
}

std::vector<json_path_handler_base::schema_type_t>
json_path_handler_base::get_types() const
{
    std::vector<schema_type_t> retval;

    if (this->jph_callbacks.yajl_boolean) {
        retval.push_back(schema_type_t::BOOLEAN);
    }
    if (this->jph_callbacks.yajl_integer) {
        retval.push_back(schema_type_t::INTEGER);
    }
    if (this->jph_callbacks.yajl_double || this->jph_callbacks.yajl_number) {
        retval.push_back(schema_type_t::NUMBER);
    }
    if (this->jph_callbacks.yajl_string) {
        retval.push_back(schema_type_t::STRING);
    }
    if (this->jph_children) {
        retval.push_back(schema_type_t::OBJECT);
    }
    if (retval.empty()) {
        retval.push_back(schema_type_t::ANY);
    }
    return retval;
}

yajlpp_parse_context::yajlpp_parse_context(
    intern_string_t source, const struct json_path_container* handlers)
    : ypc_source(source), ypc_handlers(handlers)
{
    this->ypc_path.reserve(4096);
    this->ypc_path.push_back('/');
    this->ypc_path.push_back('\0');
    this->ypc_callbacks = DEFAULT_CALLBACKS;
    memset(&this->ypc_alt_callbacks, 0, sizeof(this->ypc_alt_callbacks));
}

int
yajlpp_parse_context::map_start(void* ctx)
{
    yajlpp_parse_context* ypc = (yajlpp_parse_context*) ctx;
    int retval = 1;

    require(ypc->ypc_path.size() >= 2);

    ypc->ypc_path_index_stack.push_back(ypc->ypc_path.size() - 1);

    if (ypc->ypc_path.size() > 1
        && ypc->ypc_path[ypc->ypc_path.size() - 2] == '#')
    {
        ypc->ypc_array_index.back() += 1;
    }

    if (ypc->ypc_alt_callbacks.yajl_start_map != nullptr) {
        retval = ypc->ypc_alt_callbacks.yajl_start_map(ypc);
    }

    return retval;
}

int
yajlpp_parse_context::map_key(void* ctx, const unsigned char* key, size_t len)
{
    yajlpp_parse_context* ypc = (yajlpp_parse_context*) ctx;
    int retval = 1;

    require(ypc->ypc_path.size() >= 2);

    ypc->ypc_path.resize(ypc->ypc_path_index_stack.back());
    if (ypc->ypc_path.back() != '/') {
        ypc->ypc_path.push_back('/');
    }
    for (size_t lpc = 0; lpc < len; lpc++) {
        switch (key[lpc]) {
            case '~':
                ypc->ypc_path.push_back('~');
                ypc->ypc_path.push_back('0');
                break;
            case '/':
                ypc->ypc_path.push_back('~');
                ypc->ypc_path.push_back('1');
                break;
            case '#':
                ypc->ypc_path.push_back('~');
                ypc->ypc_path.push_back('2');
                break;
            default:
                ypc->ypc_path.push_back(key[lpc]);
                break;
        }
    }
    ypc->ypc_path.push_back('\0');

    if (ypc->ypc_alt_callbacks.yajl_map_key != nullptr) {
        retval = ypc->ypc_alt_callbacks.yajl_map_key(ctx, key, len);
    }

    if (ypc->ypc_handlers != nullptr) {
        ypc->update_callbacks();
    }

    ensure(ypc->ypc_path.size() >= 2);

    return retval;
}

void
yajlpp_parse_context::update_callbacks(const json_path_container* orig_handlers,
                                       int child_start)
{
    const json_path_container* handlers = orig_handlers;

    this->ypc_current_handler = nullptr;

    if (this->ypc_handlers == nullptr) {
        return;
    }

    this->ypc_sibling_handlers = orig_handlers;
    this->ypc_callbacks = DEFAULT_CALLBACKS;

    if (handlers == nullptr) {
        handlers = this->ypc_handlers;
        this->ypc_handler_stack.clear();
        this->ypc_array_handler_count = 0;
    }

    if (!this->ypc_active_paths.empty()) {
        std::string curr_path(&this->ypc_path[0], this->ypc_path.size() - 1);

        if (this->ypc_active_paths.find(curr_path)
            == this->ypc_active_paths.end())
        {
            return;
        }
    }

    if (child_start == 0 && !this->ypc_obj_stack.empty()) {
        while (this->ypc_obj_stack.size() > 1) {
            this->ypc_obj_stack.pop();
        }
    }

    auto path_frag = string_fragment::from_byte_range(
        this->ypc_path.data(), 1 + child_start, this->ypc_path.size() - 1);
    for (const auto& jph : handlers->jpc_children) {
        thread_local auto md = lnav::pcre2pp::match_data::unitialized();

        if (jph.jph_regex->capture_from(path_frag)
                .into(md)
                .matches(PCRE2_NO_UTF_CHECK)
                .ignore_error()
            && (md.remaining().empty() || md.remaining().startswith("/")))
        {
            auto cap = md[0].value();

            if (jph.jph_is_array) {
                this->ypc_array_handler_count += 1;
            }
            if (jph.jph_obj_provider) {
                auto index = this->ypc_array_handler_count == 0
                    ? static_cast<size_t>(-1)
                    : this->ypc_array_index[this->ypc_array_handler_count - 1];

                if ((!jph.is_array()
                     || cap.sf_end != (int) this->ypc_path.size() - 1)
                    && (!jph.is_array()
                        || index != yajlpp_provider_context::nindex))
                {
                    this->ypc_obj_stack.push(jph.jph_obj_provider(
                        {&md, index, this}, this->ypc_obj_stack.top()));
                }
            }

            if (jph.jph_children) {
                this->ypc_handler_stack.emplace_back(&jph);

                if (cap.sf_end != (int) this->ypc_path.size() - 1) {
                    this->update_callbacks(jph.jph_children, cap.sf_end);
                    return;
                }
            } else {
                if (cap.sf_end != (int) this->ypc_path.size() - 1) {
                    continue;
                }

                this->ypc_current_handler = &jph;
            }

            if (jph.jph_callbacks.yajl_null != nullptr) {
                this->ypc_callbacks.yajl_null = jph.jph_callbacks.yajl_null;
            }
            if (jph.jph_callbacks.yajl_boolean != nullptr) {
                this->ypc_callbacks.yajl_boolean
                    = jph.jph_callbacks.yajl_boolean;
            }
            if (jph.jph_callbacks.yajl_integer != nullptr) {
                this->ypc_callbacks.yajl_integer
                    = jph.jph_callbacks.yajl_integer;
            }
            if (jph.jph_callbacks.yajl_number != nullptr) {
                this->ypc_callbacks.yajl_number = jph.jph_callbacks.yajl_number;
            }
            if (jph.jph_callbacks.yajl_double != nullptr) {
                this->ypc_callbacks.yajl_double = jph.jph_callbacks.yajl_double;
            }
            if (jph.jph_callbacks.yajl_string != nullptr) {
                this->ypc_callbacks.yajl_string = jph.jph_callbacks.yajl_string;
            }
            if (jph.jph_is_array) {
                this->ypc_array_handler_count -= 1;
            }
            return;
        }
    }
}

int
yajlpp_parse_context::map_end(void* ctx)
{
    yajlpp_parse_context* ypc = (yajlpp_parse_context*) ctx;
    int retval = 1;

    ypc->ypc_path.resize(ypc->ypc_path_index_stack.back());
    ypc->ypc_path.push_back('\0');
    ypc->ypc_path_index_stack.pop_back();

    if (ypc->ypc_alt_callbacks.yajl_end_map != nullptr) {
        retval = ypc->ypc_alt_callbacks.yajl_end_map(ctx);
    }

    ypc->update_callbacks();

    ensure(ypc->ypc_path.size() >= 2);

    return retval;
}

int
yajlpp_parse_context::array_start(void* ctx)
{
    yajlpp_parse_context* ypc = (yajlpp_parse_context*) ctx;
    int retval = 1;

    ypc->ypc_path_index_stack.push_back(ypc->ypc_path.size() - 1);
    ypc->ypc_path[ypc->ypc_path.size() - 1] = '#';
    ypc->ypc_path.push_back('\0');
    ypc->ypc_array_index.push_back(-1);

    if (ypc->ypc_alt_callbacks.yajl_start_array != nullptr) {
        retval = ypc->ypc_alt_callbacks.yajl_start_array(ctx);
    }

    ypc->update_callbacks();

    ensure(ypc->ypc_path.size() >= 2);

    return retval;
}

int
yajlpp_parse_context::array_end(void* ctx)
{
    yajlpp_parse_context* ypc = (yajlpp_parse_context*) ctx;
    int retval = 1;

    ypc->ypc_path.resize(ypc->ypc_path_index_stack.back());
    ypc->ypc_path.push_back('\0');
    ypc->ypc_path_index_stack.pop_back();
    ypc->ypc_array_index.pop_back();

    if (ypc->ypc_alt_callbacks.yajl_end_array != nullptr) {
        retval = ypc->ypc_alt_callbacks.yajl_end_array(ctx);
    }

    ypc->update_callbacks();

    ensure(ypc->ypc_path.size() >= 2);

    return retval;
}

int
yajlpp_parse_context::handle_unused(void* ctx)
{
    yajlpp_parse_context* ypc = (yajlpp_parse_context*) ctx;

    if (ypc->ypc_ignore_unused) {
        return 1;
    }

    const json_path_handler_base* handler = ypc->ypc_current_handler;
    lnav::console::user_message msg;

    if (handler != nullptr && strlen(handler->jph_synopsis) > 0
        && strlen(handler->jph_description) > 0)
    {
        auto help_text = handler->get_help_text(ypc);
        std::vector<std::string> expected_types;

        if (ypc->ypc_callbacks.yajl_boolean
            != (int (*)(void*, int)) yajlpp_parse_context::handle_unused)
        {
            expected_types.emplace_back("boolean");
        }
        if (ypc->ypc_callbacks.yajl_integer
            != (int (*)(void*, long long)) yajlpp_parse_context::handle_unused)
        {
            expected_types.emplace_back("integer");
        }
        if (ypc->ypc_callbacks.yajl_double
            != (int (*)(void*, double)) yajlpp_parse_context::handle_unused)
        {
            expected_types.emplace_back("float");
        }
        if (ypc->ypc_callbacks.yajl_string
            != (int (*)(
                void*, const unsigned char*, size_t, yajl_string_props_t*))
                yajlpp_parse_context::handle_unused)
        {
            expected_types.emplace_back("string");
        }
        if (!expected_types.empty()) {
            help_text.appendf(
                FMT_STRING("  expecting one of the following types: {}"),
                fmt::join(expected_types, ", "));
        }
        msg = lnav::console::user_message::warning(
                  attr_line_t("unexpected data for property ")
                      .append_quoted(lnav::roles::symbol(
                          ypc->get_full_path().to_string())))
                  .with_help(help_text);
    } else if (ypc->ypc_path[1]) {
        msg = lnav::console::user_message::warning(
            attr_line_t("unexpected value for property ")
                .append_quoted(
                    lnav::roles::symbol(ypc->get_full_path().to_string())));
    } else {
        msg = lnav::console::user_message::error("unexpected JSON value");
    }

    if (handler == nullptr) {
        const json_path_container* accepted_handlers;

        if (ypc->ypc_sibling_handlers) {
            accepted_handlers = ypc->ypc_sibling_handlers;
        } else {
            accepted_handlers = ypc->ypc_handlers;
        }

        attr_line_t help_text;

        if (accepted_handlers->jpc_children.size() == 1
            && accepted_handlers->jpc_children.front().jph_is_array)
        {
            const auto& jph = accepted_handlers->jpc_children.front();

            help_text.append("expecting an array of ")
                .append(lnav::roles::variable(jph.jph_synopsis))
                .append(" values");
        } else {
            help_text.append(lnav::roles::h2("Available Properties"))
                .append("\n");
            for (const auto& jph : accepted_handlers->jpc_children) {
                help_text.append("  ")
                    .append(lnav::roles::symbol(jph.jph_property))
                    .append(lnav::roles::symbol(
                        jph.jph_children != nullptr ? "/" : ""))
                    .append(" ")
                    .append(lnav::roles::variable(jph.jph_synopsis))
                    .append("\n");
            }
        }
        msg.with_help(help_text);
    }

    msg.with_snippet(ypc->get_snippet());

    ypc->report_error(msg);

    return 1;
}

int
yajlpp_parse_context::handle_unused_or_delete(void* ctx)
{
    yajlpp_parse_context* ypc = (yajlpp_parse_context*) ctx;

    if (!ypc->ypc_handler_stack.empty()
        && ypc->ypc_handler_stack.back()->jph_obj_deleter)
    {
        thread_local auto md = lnav::pcre2pp::match_data::unitialized();

        auto key_start = ypc->ypc_path_index_stack.back();
        auto path_frag = string_fragment::from_byte_range(
            ypc->ypc_path.data(), key_start + 1, ypc->ypc_path.size() - 1);
        yajlpp_provider_context provider_ctx{&md, static_cast<size_t>(-1)};
        ypc->ypc_handler_stack.back()
            ->jph_regex->capture_from(path_frag)
            .into(md)
            .matches(PCRE2_NO_UTF_CHECK);

        ypc->ypc_handler_stack.back()->jph_obj_deleter(
            provider_ctx, ypc->ypc_obj_stack.top());
        return 1;
    }

    return handle_unused(ctx);
}

const yajl_callbacks yajlpp_parse_context::DEFAULT_CALLBACKS = {
    yajlpp_parse_context::handle_unused_or_delete,
    (int (*)(void*, int)) yajlpp_parse_context::handle_unused,
    (int (*)(void*, long long)) yajlpp_parse_context::handle_unused,
    (int (*)(void*, double)) yajlpp_parse_context::handle_unused,
    nullptr,
    (int (*)(void*, const unsigned char*, size_t, yajl_string_props_t*))
        yajlpp_parse_context::handle_unused,
    yajlpp_parse_context::map_start,
    yajlpp_parse_context::map_key,
    yajlpp_parse_context::map_end,
    yajlpp_parse_context::array_start,
    yajlpp_parse_context::array_end,
};

yajl_status
yajlpp_parse_context::parse(const unsigned char* jsonText, size_t jsonTextLen)
{
    this->ypc_json_text = jsonText;
    this->ypc_json_text_len = jsonTextLen;

    const auto retval = yajl_parse(this->ypc_handle, jsonText, jsonTextLen);
    const auto consumed = yajl_get_bytes_consumed(this->ypc_handle);

    this->ypc_line_number
        += std::count(&jsonText[0], &jsonText[consumed], '\n');

    this->ypc_json_text = nullptr;

    if (retval != yajl_status_ok && this->ypc_error_reporter) {
        auto* msg = yajl_get_error(this->ypc_handle, 1, jsonText, jsonTextLen);

        this->report_error(
            lnav::console::user_message::error("invalid JSON")
                .with_snippet(lnav::console::snippet::from(this->ypc_source,
                                                           (const char*) msg)
                                  .with_line(this->get_line_number())));
        yajl_free_error(this->ypc_handle, msg);
    }

    return retval;
}

yajl_status
yajlpp_parse_context::complete_parse()
{
    const auto retval = yajl_complete_parse(this->ypc_handle);

    if (retval != yajl_status_ok && this->ypc_error_reporter) {
        auto* msg = yajl_get_error(
            this->ypc_handle, 0, this->ypc_json_text, this->ypc_json_text_len);

        this->report_error(lnav::console::user_message::error("invalid JSON")
                               .with_reason((const char*) msg)
                               .with_snippet(this->get_snippet()));
        yajl_free_error(this->ypc_handle, msg);
    }

    return retval;
}

yajl_status
yajlpp_parse_context::parse_frag(string_fragment sf)
{
    this->ypc_json_text = sf.udata();
    this->ypc_json_text_len = sf.length();

    const auto rc
        = yajl_parse(this->ypc_handle, this->ypc_json_text, sf.length());
    size_t consumed = yajl_get_bytes_consumed(this->ypc_handle);
    this->ypc_total_consumed += consumed;
    this->ypc_line_number += std::count(
        &this->ypc_json_text[0], &this->ypc_json_text[consumed], '\n');

    if (rc != yajl_status_ok) {
        if (this->ypc_error_reporter) {
            auto* msg = yajl_get_error(this->ypc_handle,
                                       1,
                                       this->ypc_json_text,
                                       this->ypc_json_text_len);

            this->report_error(
                lnav::console::user_message::error("invalid JSON")
                    .with_reason((const char*) msg)
                    .with_snippet(this->get_snippet()));
            yajl_free_error(this->ypc_handle, msg);
        }
    }

    this->ypc_json_text = nullptr;
    this->ypc_json_text_len = 0;

    return rc;
}

bool
yajlpp_parse_context::parse_doc(string_fragment_producer& sfp)
{
    auto retval = true;

    while (true) {
        auto next_res = sfp.next();
        if (next_res.is<string_fragment_producer::eof>()) {
            break;
        }
        if (next_res.is<string_fragment_producer::error>()) {
            const auto err = next_res.get<string_fragment_producer::error>();
            this->report_error(
                lnav::console::user_message::error("unable to read file")
                    .with_reason(err.what)
                    .with_snippet(this->get_snippet()));
            break;
        }

        auto sf = next_res.get<string_fragment>();

        if (this->parse_frag(sf) != yajl_status_ok) {
            log_error("parse frag failed %s", this->ypc_source.c_str());
            retval = false;
            break;
        }
    }
    if (retval && this->complete_parse() != yajl_status_ok) {
        retval = false;
    }

    return retval;
}

string_fragment
yajlpp_parse_context::get_path_as_string_fragment() const
{
    if (this->ypc_path.size() <= 1) {
        return string_fragment();
    }
    return string_fragment::from_bytes(&this->ypc_path[1],
                                       this->ypc_path.size() - 2);
}

const intern_string_t
yajlpp_parse_context::get_path() const
{
    if (this->ypc_path.size() <= 1) {
        return intern_string_t();
    }
    return intern_string::lookup(&this->ypc_path[1], this->ypc_path.size() - 2);
}

const intern_string_t
yajlpp_parse_context::get_full_path() const
{
    if (this->ypc_path.size() <= 1) {
        static const intern_string_t SLASH = intern_string::lookup("/");

        return SLASH;
    }
    return intern_string::lookup(&this->ypc_path[0], this->ypc_path.size() - 1);
}

void
yajlpp_parse_context::reset(const struct json_path_container* handlers)
{
    this->ypc_handlers = handlers;
    this->ypc_path.clear();
    this->ypc_path.push_back('/');
    this->ypc_path.push_back('\0');
    this->ypc_path_index_stack.clear();
    this->ypc_array_index.clear();
    this->ypc_array_handler_count = 0;
    this->ypc_callbacks = DEFAULT_CALLBACKS;
    memset(&this->ypc_alt_callbacks, 0, sizeof(this->ypc_alt_callbacks));
    this->ypc_sibling_handlers = nullptr;
    this->ypc_current_handler = nullptr;
    while (!this->ypc_obj_stack.empty()) {
        this->ypc_obj_stack.pop();
    }
}

void
yajlpp_parse_context::set_static_handler(const json_path_handler_base& jph)
{
    this->ypc_path.clear();
    this->ypc_path.push_back('/');
    this->ypc_path.push_back('\0');
    this->ypc_path_index_stack.clear();
    this->ypc_array_index.clear();
    this->ypc_array_handler_count = 0;
    if (jph.jph_callbacks.yajl_null != nullptr) {
        this->ypc_callbacks.yajl_null = jph.jph_callbacks.yajl_null;
    }
    if (jph.jph_callbacks.yajl_boolean != nullptr) {
        this->ypc_callbacks.yajl_boolean = jph.jph_callbacks.yajl_boolean;
    }
    if (jph.jph_callbacks.yajl_integer != nullptr) {
        this->ypc_callbacks.yajl_integer = jph.jph_callbacks.yajl_integer;
    }
    if (jph.jph_callbacks.yajl_number != nullptr) {
        this->ypc_callbacks.yajl_number = jph.jph_callbacks.yajl_number;
    } else {
        this->ypc_callbacks.yajl_number = nullptr;
    }
    if (jph.jph_callbacks.yajl_double != nullptr) {
        this->ypc_callbacks.yajl_double = jph.jph_callbacks.yajl_double;
    }
    if (jph.jph_callbacks.yajl_string != nullptr) {
        this->ypc_callbacks.yajl_string = jph.jph_callbacks.yajl_string;
    }
}

yajlpp_parse_context&
yajlpp_parse_context::set_path(const std::string& path)
{
    this->ypc_path.resize(path.size() + 1);
    std::copy(path.begin(), path.end(), this->ypc_path.begin());
    this->ypc_path[path.size()] = '\0';
    for (size_t lpc = 0; lpc < path.size(); lpc++) {
        switch (path[lpc]) {
            case '/':
                this->ypc_path_index_stack.push_back(
                    this->ypc_path_index_stack.empty() ? 1 : 0 + lpc);
                break;
        }
    }
    return *this;
}

const char*
yajlpp_parse_context::get_path_fragment(int offset,
                                        char* frag_in,
                                        size_t& len_out) const
{
    const char* retval;
    size_t start, end;

    if (offset < 0) {
        offset = ((int) this->ypc_path_index_stack.size()) + offset;
    }
    start = this->ypc_path_index_stack[offset] + ((offset == 0) ? 0 : 1);
    if ((offset + 1) < (int) this->ypc_path_index_stack.size()) {
        end = this->ypc_path_index_stack[offset + 1];
    } else {
        end = this->ypc_path.size() - 1;
    }
    if (this->ypc_handlers != nullptr) {
        len_out
            = json_ptr::decode(frag_in, &this->ypc_path[start], end - start);
        retval = frag_in;
    } else {
        retval = &this->ypc_path[start];
        len_out = end - start;
    }

    return retval;
}

yajl_status
yajlpp_parse_context::parse(string_fragment_producer& sfp)
{
    yajl_status retval = yajl_status_ok;
    while (retval == yajl_status_ok) {
        auto next_res = sfp.next();
        if (next_res.is<string_fragment_producer::eof>()) {
            break;
        }

        if (next_res.is<string_fragment_producer::error>()) {
            const auto err = next_res.get<string_fragment_producer::error>();
            this->report_error(
                lnav::console::user_message::error("unable to read file")
                    .with_reason(err.what)
                    .with_snippet(this->get_snippet()));
            break;
        }

        auto sf = next_res.get<string_fragment>();
        retval = this->parse(sf);
        if (retval != yajl_status_ok) {
            auto* msg
                = yajl_get_error(this->ypc_handle, 1, sf.udata(), sf.length());
            auto um = lnav::console::user_message::error("invalid JSON")
                          .with_snippet(lnav::console::snippet::from(
                              this->ypc_source, attr_line_t((const char*) msg)))
                          .with_errno_reason();
            this->report_error(um);
            yajl_free_error(this->ypc_handle, msg);
        }
    }

    retval = this->complete_parse();

    return retval;
}

int
yajlpp_parse_context::get_line_number() const
{
    if (this->ypc_handle != nullptr && this->ypc_json_text) {
        size_t consumed = yajl_get_bytes_consumed(this->ypc_handle);
        long current_count = std::count(
            &this->ypc_json_text[0], &this->ypc_json_text[consumed], '\n');

        return this->ypc_line_number + current_count;
    }
    return this->ypc_line_number;
}

void
yajlpp_gen_context::gen()
{
    yajlpp_map root(this->ygc_handle);

    for (const auto& jph : this->ygc_handlers->jpc_children) {
        jph.gen(*this, this->ygc_handle);
    }
}

void
yajlpp_gen_context::gen_schema(const json_path_container* handlers)
{
    if (handlers == nullptr) {
        handlers = this->ygc_handlers;
    }

    {
        yajlpp_map schema(this->ygc_handle);

        if (!handlers->jpc_schema_id.empty()) {
            schema.gen("$id");
            schema.gen(handlers->jpc_schema_id);
            schema.gen("title");
            schema.gen(handlers->jpc_schema_id);
        }
        schema.gen("$schema");
        schema.gen("http://json-schema.org/draft-07/schema#");
        if (!handlers->jpc_description.empty()) {
            schema.gen("description");
            schema.gen(handlers->jpc_description);
        }
        handlers->gen_schema(*this);

        if (!this->ygc_schema_definitions.empty()) {
            schema.gen("definitions");

            yajlpp_map defs(this->ygc_handle);
            for (auto& container : this->ygc_schema_definitions) {
                defs.gen(container.first);

                yajlpp_map def(this->ygc_handle);

                def.gen("title");
                def.gen(container.first);
                if (!container.second->jpc_description.empty()) {
                    def.gen("description");
                    def.gen(container.second->jpc_description);
                }
                def.gen("type");
                def.gen("object");
                def.gen("$$target");
                def.gen(fmt::format(FMT_STRING("#/definitions/{}"),
                                    container.first));
                container.second->gen_properties(*this);
            }
        }
    }
}

yajlpp_gen_context&
yajlpp_gen_context::with_context(yajlpp_parse_context& ypc)
{
    this->ygc_obj_stack = ypc.ypc_obj_stack;
    if (ypc.ypc_current_handler == nullptr && !ypc.ypc_handler_stack.empty()
        && ypc.ypc_handler_stack.back() != nullptr)
    {
        this->ygc_handlers = ypc.ypc_handler_stack.back()->jph_children;
        this->ygc_depth += 1;
    }
    return *this;
}

json_path_handler&
json_path_handler::with_children(const json_path_container& container)
{
    // XXX pattern with children cannot match everything, need to use [^/]+
    require(!this->jph_is_pattern_property
            || (this->jph_property.find(".*") == std::string::npos
                && this->jph_property.find(".+") == std::string::npos));

    this->jph_children = &container;
    return *this;
}

lnav::console::snippet
yajlpp_parse_context::get_snippet() const
{
    auto line_number = this->get_line_number();
    attr_line_t content;

    if (this->ypc_json_text != nullptr) {
        auto in_text_line = line_number - this->ypc_line_number;
        const auto* line_start = this->ypc_json_text;
        auto text_len_remaining = this->ypc_json_text_len;

        while (in_text_line > 0) {
            const auto* line_end = (const unsigned char*) memchr(
                line_start, '\n', text_len_remaining);
            if (line_end == nullptr) {
                break;
            }

            text_len_remaining -= (line_end - line_start) + 1;
            line_start = line_end + 1;
            in_text_line -= 1;
        }

        if (text_len_remaining > 0) {
            const auto* line_end = (const unsigned char*) memchr(
                line_start, '\n', text_len_remaining);
            if (line_end) {
                text_len_remaining = (line_end - line_start);
            }
            content.append(
                string_fragment::from_bytes(line_start, text_len_remaining));
        }
    }

    content.with_attr_for_all(VC_ROLE.value(role_t::VCR_QUOTED_CODE));
    return lnav::console::snippet::from(this->ypc_source, content)
        .with_line(line_number);
}

void
json_path_handler_base::validate_string(yajlpp_parse_context& ypc,
                                        string_fragment sf) const
{
    if (this->jph_pattern) {
        if (!this->jph_pattern->find_in(sf).ignore_error()) {
            this->report_pattern_error(&ypc, sf.to_string());
        }
    }
    if (sf.empty() && this->jph_min_length > 0) {
        ypc.report_error(lnav::console::user_message::error(
                             attr_line_t("invalid value for option ")
                                 .append_quoted(lnav::roles::symbol(
                                     ypc.get_full_path().to_string())))
                             .with_reason("empty values are not allowed")
                             .with_snippet(ypc.get_snippet())
                             .with_help(this->get_help_text(&ypc)));
    } else if (sf.length() < this->jph_min_length) {
        ypc.report_error(
            lnav::console::user_message::error(
                attr_line_t()
                    .append_quoted(sf)
                    .append(" is not a valid value for option ")
                    .append_quoted(
                        lnav::roles::symbol(ypc.get_full_path().to_string())))
                .with_reason(attr_line_t("value must be at least ")
                                 .append(lnav::roles::number(
                                     fmt::to_string(this->jph_min_length)))
                                 .append(" characters long"))
                .with_snippet(ypc.get_snippet())
                .with_help(this->get_help_text(&ypc)));
    }
}

void
json_path_handler_base::report_pattern_error(yajlpp_parse_context* ypc,
                                             const std::string& value_str) const
{
    ypc->report_error(
        lnav::console::user_message::error(
            attr_line_t()
                .append_quoted(value_str)
                .append(" is not a valid value for option ")
                .append_quoted(
                    lnav::roles::symbol(ypc->get_full_path().to_string())))
            .with_snippet(ypc->get_snippet())
            .with_reason(attr_line_t("value does not match pattern: ")
                             .append(lnav::roles::symbol(this->jph_pattern_re)))
            .with_help(this->get_help_text(ypc)));
}

void
json_path_handler_base::report_tz_error(yajlpp_parse_context* ypc,
                                        const std::string& value_str,
                                        const char* msg) const
{
    auto help_al = attr_line_t()
                       .append(lnav::roles::h2("Available time zones"))
                       .append("\n");

    try {
        for (const auto& tz : date::get_tzdb().zones) {
            help_al.append("    ")
                .append(lnav::roles::symbol(tz.name()))
                .append("\n");
        }
    } catch (const std::runtime_error& e) {
        log_error("unable to load timezones: %s", e.what());
    }

    ypc->report_error(lnav::console::user_message::error(
                          attr_line_t().append_quoted(value_str).append(
                              " is not a valid timezone"))
                          .with_snippet(ypc->get_snippet())
                          .with_reason(msg)
                          .with_help(help_al));
}

attr_line_t
json_path_handler_base::get_help_text(const std::string& full_path) const
{
    attr_line_t retval;

    retval.append(lnav::roles::h2("Property Synopsis"))
        .append("\n  ")
        .append(lnav::roles::symbol(full_path))
        .append(" ")
        .append(lnav::roles::variable(this->jph_synopsis))
        .append("\n")
        .append(lnav::roles::h2("Description"))
        .append("\n  ")
        .append(this->jph_description)
        .append("\n");

    if (this->jph_enum_values != nullptr) {
        retval.append(lnav::roles::h2("Allowed Values")).append("\n  ");

        for (int lpc = 0; this->jph_enum_values[lpc].first; lpc++) {
            const auto& ev = this->jph_enum_values[lpc];

            retval.append(lpc == 0 ? "" : ", ")
                .append(lnav::roles::symbol(ev.first));
        }
    }

    if (!this->jph_examples.empty()) {
        retval
            .append(lnav::roles::h2(
                this->jph_examples.size() == 1 ? "Example" : "Examples"))
            .append("\n");

        for (const auto& ex : this->jph_examples) {
            retval.appendf(FMT_STRING("  {}\n"), ex);
        }
    }

    return retval;
}

attr_line_t
json_path_handler_base::get_help_text(yajlpp_parse_context* ypc) const
{
    return this->get_help_text(ypc->get_full_path().to_string());
}

void
json_path_handler_base::report_min_value_error(yajlpp_parse_context* ypc,
                                               long long value) const
{
    ypc->report_error(
        lnav::console::user_message::error(
            attr_line_t()
                .append_quoted(fmt::to_string(value))
                .append(" is not a valid value for option ")
                .append_quoted(
                    lnav::roles::symbol(ypc->get_full_path().to_string())))
            .with_reason(attr_line_t("value must be greater than or equal to ")
                             .append(lnav::roles::number(
                                 fmt::to_string(this->jph_min_value))))
            .with_snippet(ypc->get_snippet())
            .with_help(this->get_help_text(ypc)));
}

void
json_path_handler_base::report_duration_error(
    yajlpp_parse_context* ypc,
    const std::string& value_str,
    const relative_time::parse_error& pe) const
{
    ypc->report_error(lnav::console::user_message::error(
                          attr_line_t()
                              .append_quoted(value_str)
                              .append(" is not a valid duration value "
                                      "for option ")
                              .append_quoted(lnav::roles::symbol(
                                  ypc->get_full_path().to_string())))
                          .with_snippet(ypc->get_snippet())
                          .with_reason(pe.pe_msg)
                          .with_help(this->get_help_text(ypc)));
}

void
json_path_handler_base::report_enum_error(yajlpp_parse_context* ypc,
                                          const std::string& value_str) const
{
    ypc->report_error(lnav::console::user_message::error(
                          attr_line_t()
                              .append_quoted(value_str)
                              .append(" is not a valid value for option ")
                              .append_quoted(lnav::roles::symbol(
                                  ypc->get_full_path().to_string())))
                          .with_snippet(ypc->get_snippet())
                          .with_help(this->get_help_text(ypc)));
}

void
json_path_handler_base::report_error(yajlpp_parse_context* ypc,
                                     const std::string& value,
                                     lnav::console::user_message um) const
{
    ypc->report_error(um.with_snippet(ypc->get_snippet())
                          .with_help(this->get_help_text(ypc)));
}

void
json_path_container::gen_schema(yajlpp_gen_context& ygc) const
{
    if (!this->jpc_definition_id.empty()) {
        ygc.ygc_schema_definitions[this->jpc_definition_id] = this;

        yajl_gen_string(ygc.ygc_handle, "$ref");
        yajl_gen_string(ygc.ygc_handle,
                        fmt::format(FMT_STRING("#/definitions/{}"),
                                    this->jpc_definition_id));
        return;
    }

    this->gen_properties(ygc);
}

void
json_path_container::gen_properties(yajlpp_gen_context& ygc) const
{
    static const auto FWD_SLASH = lnav::pcre2pp::code::from_const(R"(\[\^/\])");
    auto pattern_count = count_if(
        this->jpc_children.begin(), this->jpc_children.end(), [](auto& jph) {
            return jph.jph_is_pattern_property;
        });
    auto plain_count = this->jpc_children.size() - pattern_count;

    if (plain_count > 0) {
        yajl_gen_string(ygc.ygc_handle, "properties");
        {
            yajlpp_map properties(ygc.ygc_handle);

            for (const auto& child_handler : this->jpc_children) {
                if (child_handler.jph_is_pattern_property) {
                    continue;
                }
                properties.gen(child_handler.jph_property);
                child_handler.gen_schema(ygc);
            }
        }
    }
    if (pattern_count > 0) {
        yajl_gen_string(ygc.ygc_handle, "patternProperties");
        {
            yajlpp_map properties(ygc.ygc_handle);

            for (const auto& child_handler : this->jpc_children) {
                if (!child_handler.jph_is_pattern_property) {
                    continue;
                }

                auto pattern = child_handler.jph_property;
                pattern = FWD_SLASH.replace(pattern, ".");
                properties.gen(fmt::format(FMT_STRING("^{}$"), pattern));
                child_handler.gen_schema(ygc);
            }
        }
    }

    yajl_gen_string(ygc.ygc_handle, "additionalProperties");
    yajl_gen_bool(ygc.ygc_handle, false);
}

static void
schema_printer(FILE* file, const char* str, size_t len)
{
    fwrite(str, len, 1, file);
}

void
dump_schema_to(const json_path_container& jpc, const char* internals_dir)
{
    yajlpp_gen genner;
    yajlpp_gen_context ygc(genner, jpc);
    auto internals_dir_path = std::filesystem::path(internals_dir);
    auto schema_file_name = std::filesystem::path(jpc.jpc_schema_id).filename();
    auto schema_path = internals_dir_path / schema_file_name;
    auto file = std::unique_ptr<FILE, decltype(&fclose)>(
        fopen(schema_path.c_str(), "w+"), fclose);

    if (!file.get()) {
        return;
    }

    yajl_gen_config(genner, yajl_gen_beautify, true);
    yajl_gen_config(
        genner, yajl_gen_print_callback, schema_printer, file.get());

    ygc.gen_schema();
}

string_fragment
yajlpp_gen::to_string_fragment()
{
    const unsigned char* buf;
    size_t len;

    yajl_gen_get_buf(this->yg_handle.in(), &buf, &len);

    return string_fragment::from_bytes(buf, len);
}

namespace yajlpp {

auto_mem<yajl_handle_t>
alloc_handle(const yajl_callbacks* cb, void* cu)
{
    auto_mem<yajl_handle_t> retval(yajl_free);

    retval = yajl_alloc(cb, nullptr, cu);

    return retval;
}

}  // namespace yajlpp