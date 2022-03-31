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

#include <regex>
#include <utility>

#include "yajlpp.hh"

#include "config.h"
#include "fmt/format.h"
#include "yajl/api/yajl_parse.h"
#include "yajlpp_def.hh"

const json_path_handler_base::enum_value_t
    json_path_handler_base::ENUM_TERMINATOR((const char*) nullptr, 0);

json_path_handler_base::json_path_handler_base(const std::string& property)
    : jph_property(property.back() == '#'
                       ? property.substr(0, property.size() - 1)
                       : property),
      jph_regex(pcrepp::quote(property), PCRE_ANCHORED),
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

json_path_handler_base::json_path_handler_base(const pcrepp& property)
    : jph_property(scrub_pattern(property.p_pattern)), jph_regex(property),
      jph_is_array(property.p_pattern.back() == '#'),
      jph_is_pattern_property(true)
{
    memset(&this->jph_callbacks, 0, sizeof(this->jph_callbacks));
}

json_path_handler_base::json_path_handler_base(std::string property,
                                               const pcrepp& property_re)
    : jph_property(std::move(property)), jph_regex(property_re),
      jph_is_array(property_re.p_pattern.find('#') != std::string::npos)
{
    memset(&this->jph_callbacks, 0, sizeof(this->jph_callbacks));
}

yajl_gen_status
json_path_handler_base::gen(yajlpp_gen_context& ygc, yajl_gen handle) const
{
    std::vector<std::string> local_paths;

    if (this->jph_path_provider) {
        this->jph_path_provider(ygc.ygc_obj_stack.top(), local_paths);
    } else {
        local_paths.emplace_back(this->jph_property);
    }

    if (this->jph_children) {
        for (const auto& lpath : local_paths) {
            std::string full_path = lpath;
            if (this->jph_path_provider) {
                full_path += "/";
            }
            int start_depth = ygc.ygc_depth;

            yajl_gen_string(handle, lpath);
            yajl_gen_map_open(handle);
            ygc.ygc_depth += 1;

            if (this->jph_obj_provider) {
                pcre_context_static<30> pc;
                pcre_input pi(full_path);

                this->jph_regex.match(pc, pi);
                ygc.ygc_obj_stack.push(this->jph_obj_provider(
                    {{pc, pi}, -1}, ygc.ygc_obj_stack.top()));
                if (!ygc.ygc_default_stack.empty()) {
                    ygc.ygc_default_stack.push(this->jph_obj_provider(
                        {{pc, pi}, -1}, ygc.ygc_default_stack.top()));
                }
            }

            for (auto& jph : this->jph_children->jpc_children) {
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

const char* SCHEMA_TYPE_STRINGS[] = {
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
                ygc.ygc_path.emplace_back(fmt::format(
                    FMT_STRING("<{}>"), this->jph_regex.name_for_capture(0)));
            } else {
                ygc.ygc_path.emplace_back(this->jph_property);
            }
            if (this->jph_children->jpc_definition_id.empty()) {
                schema.gen("title");
                schema.gen(fmt::format(FMT_STRING("/{}"),
                                       fmt::join(ygc.ygc_path, "/")));
                schema.gen("type");
                if (this->jph_is_array) {
                    if (this->jph_regex.p_pattern.find("#?")
                        == std::string::npos) {
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
                FMT_STRING("<{}>"), this->jph_regex.name_for_capture(0)));
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
            if (this->jph_regex.p_pattern.find("#?") == std::string::npos) {
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
            for (auto& ex : this->jph_examples) {
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
    const std::function<
        void(const json_path_handler_base&, const std::string&, void*)>& cb,
    void* root,
    const std::string& base) const
{
    std::vector<std::string> local_paths;

    if (this->jph_path_provider) {
        this->jph_path_provider(root, local_paths);

        for (auto& lpath : local_paths) {
            cb(*this, lpath, nullptr);
        }
    } else {
        local_paths.emplace_back(this->jph_property);

        std::string full_path = base + this->jph_property;
        if (this->jph_children) {
            full_path += "/";
        }
        cb(*this, full_path, nullptr);
    }

    if (this->jph_children) {
        for (const auto& lpath : local_paths) {
            for (auto& jph : this->jph_children->jpc_children) {
                std::string full_path = base + lpath;
                if (this->jph_children) {
                    full_path += "/";
                }
                json_path_container dummy
                    = {json_path_handler(this->jph_property)};
                dummy.jpc_children[0].jph_callbacks = this->jph_callbacks;

                yajlpp_parse_context ypc("possibilities", &dummy);
                void* child_root = root;

                ypc.set_path(full_path).with_obj(root).update_callbacks();
                if (this->jph_obj_provider) {
                    std::string full_path = lpath + "/";
                    pcre_input pi(full_path);

                    if (!this->jph_regex.match(ypc.ypc_pcre_context, pi)) {
                        ensure(false);
                    }
                    child_root = this->jph_obj_provider(
                        {{ypc.ypc_pcre_context, pi}, -1}, root);
                }

                jph.walk(cb, child_root, full_path);
            }
        }
    } else {
        for (auto& lpath : local_paths) {
            void* field = nullptr;

            if (this->jph_field_getter) {
                field = this->jph_field_getter(root, lpath);
            }
            cb(*this, base + lpath, field);
        }
    }
}

nonstd::optional<int>
json_path_handler_base::to_enum_value(const string_fragment& sf) const
{
    for (int lpc = 0; this->jph_enum_values[lpc].first; lpc++) {
        const enum_value_t& ev = this->jph_enum_values[lpc];

        if (sf == ev.first) {
            return ev.second;
        }
    }

    return nonstd::nullopt;
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
    std::string source, const struct json_path_container* handlers)
    : ypc_source(std::move(source)), ypc_handlers(handlers)
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
        && ypc->ypc_path[ypc->ypc_path.size() - 2] == '#') {
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
    if (ypc->ypc_handlers != nullptr) {
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
    } else {
        size_t start = ypc->ypc_path.size();
        ypc->ypc_path.resize(ypc->ypc_path.size() + len);
        memcpy(&ypc->ypc_path[start], key, len);
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

    pcre_input pi(&this->ypc_path[0], 0, this->ypc_path.size() - 1);

    this->ypc_callbacks = DEFAULT_CALLBACKS;

    if (handlers == nullptr) {
        handlers = this->ypc_handlers;
        this->ypc_handler_stack.clear();
    }

    if (!this->ypc_active_paths.empty()) {
        std::string curr_path(&this->ypc_path[0], this->ypc_path.size() - 1);

        if (this->ypc_active_paths.find(curr_path)
            == this->ypc_active_paths.end()) {
            return;
        }
    }

    if (child_start == 0 && !this->ypc_obj_stack.empty()) {
        while (this->ypc_obj_stack.size() > 1) {
            this->ypc_obj_stack.pop();
        }
    }

    for (auto& jph : handlers->jpc_children) {
        pi.reset(&this->ypc_path[1 + child_start],
                 0,
                 this->ypc_path.size() - 2 - child_start);
        if (jph.jph_regex.match(this->ypc_pcre_context, pi)) {
            pcre_context::capture_t* cap = this->ypc_pcre_context.all();

            if (jph.jph_obj_provider) {
                int index = this->index_for_provider();

                if ((1 + child_start + cap->c_end
                     != (int) this->ypc_path.size() - 1)
                    && (!jph.is_array() || index >= 0))
                {
                    this->ypc_obj_stack.push(jph.jph_obj_provider(
                        {{this->ypc_pcre_context, pi}, index},
                        this->ypc_obj_stack.top()));
                }
            }

            if (jph.jph_children) {
                this->ypc_handler_stack.emplace_back(&jph);

                if (1 + child_start + cap->c_end
                    != (int) this->ypc_path.size() - 1) {
                    this->update_callbacks(jph.jph_children,
                                           1 + child_start + cap->c_end);
                    return;
                }
            } else {
                if (1 + child_start + cap->c_end
                    != (int) this->ypc_path.size() - 1) {
                    continue;
                }

                this->ypc_current_handler = &jph;
            }

            if (jph.jph_callbacks.yajl_null != nullptr)
                this->ypc_callbacks.yajl_null = jph.jph_callbacks.yajl_null;
            if (jph.jph_callbacks.yajl_boolean != nullptr)
                this->ypc_callbacks.yajl_boolean
                    = jph.jph_callbacks.yajl_boolean;
            if (jph.jph_callbacks.yajl_integer != nullptr)
                this->ypc_callbacks.yajl_integer
                    = jph.jph_callbacks.yajl_integer;
            if (jph.jph_callbacks.yajl_double != nullptr)
                this->ypc_callbacks.yajl_double = jph.jph_callbacks.yajl_double;
            if (jph.jph_callbacks.yajl_string != nullptr)
                this->ypc_callbacks.yajl_string = jph.jph_callbacks.yajl_string;

            return;
        }
    }

    this->ypc_handler_stack.emplace_back(nullptr);
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

    int line_number = ypc->get_line_number();

    if (handler != nullptr && strlen(handler->jph_synopsis) > 0
        && strlen(handler->jph_description) > 0)
    {
        ypc->report_error(lnav_log_level_t::WARNING,
                          "%s:line %d",
                          ypc->ypc_source.c_str(),
                          line_number);
        ypc->report_error(lnav_log_level_t::WARNING,
                          "  unexpected data for path");

        ypc->report_error(lnav_log_level_t::WARNING,
                          "    %s %s -- %s",
                          &ypc->ypc_path[0],
                          handler->jph_synopsis,
                          handler->jph_description);
    } else if (ypc->ypc_path[1]) {
        ypc->report_error(lnav_log_level_t::WARNING,
                          "%s:line %d",
                          ypc->ypc_source.c_str(),
                          line_number);
        ypc->report_error(lnav_log_level_t::WARNING, "  unexpected path --");

        ypc->report_error(
            lnav_log_level_t::WARNING, "    %s", &ypc->ypc_path[0]);
    } else {
        ypc->report_error(lnav_log_level_t::WARNING,
                          "%s:line %d\n  unexpected JSON value",
                          ypc->ypc_source.c_str(),
                          line_number);
    }

    if (ypc->ypc_callbacks.yajl_boolean
            != (int (*)(void*, int)) yajlpp_parse_context::handle_unused
        || ypc->ypc_callbacks.yajl_integer
            != (int (*)(void*, long long)) yajlpp_parse_context::handle_unused
        || ypc->ypc_callbacks.yajl_double
            != (int (*)(void*, double)) yajlpp_parse_context::handle_unused
        || ypc->ypc_callbacks.yajl_string
            != (int (*)(void*, const unsigned char*, size_t))
                yajlpp_parse_context::handle_unused)
    {
        ypc->report_error(lnav_log_level_t::WARNING,
                          "  expecting one of the following data types --");
    }

    if (ypc->ypc_callbacks.yajl_boolean
        != (int (*)(void*, int)) yajlpp_parse_context::handle_unused)
    {
        ypc->report_error(lnav_log_level_t::WARNING, "    boolean");
    }
    if (ypc->ypc_callbacks.yajl_integer
        != (int (*)(void*, long long)) yajlpp_parse_context::handle_unused)
    {
        ypc->report_error(lnav_log_level_t::WARNING, "    integer");
    }
    if (ypc->ypc_callbacks.yajl_double
        != (int (*)(void*, double)) yajlpp_parse_context::handle_unused)
    {
        ypc->report_error(lnav_log_level_t::WARNING, "    float");
    }
    if (ypc->ypc_callbacks.yajl_string
        != (int (*)(void*, const unsigned char*, size_t))
            yajlpp_parse_context::handle_unused)
    {
        ypc->report_error(lnav_log_level_t::WARNING, "    string");
    }

    if (handler == nullptr) {
        const json_path_container* accepted_handlers;

        if (ypc->ypc_sibling_handlers) {
            accepted_handlers = ypc->ypc_sibling_handlers;
        } else {
            accepted_handlers = ypc->ypc_handlers;
        }

        ypc->report_error(lnav_log_level_t::WARNING, "  accepted paths --");
        for (auto& jph : accepted_handlers->jpc_children) {
            ypc->report_error(lnav_log_level_t::WARNING,
                              "    %s %s -- %s",
                              jph.jph_property.c_str(),
                              jph.jph_synopsis,
                              jph.jph_description);
        }
    }

    return 1;
}

const yajl_callbacks yajlpp_parse_context::DEFAULT_CALLBACKS = {
    yajlpp_parse_context::handle_unused,
    (int (*)(void*, int)) yajlpp_parse_context::handle_unused,
    (int (*)(void*, long long)) yajlpp_parse_context::handle_unused,
    (int (*)(void*, double)) yajlpp_parse_context::handle_unused,
    nullptr,
    (int (*)(void*, const unsigned char*, size_t))
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

    yajl_status retval = yajl_parse(this->ypc_handle, jsonText, jsonTextLen);

    size_t consumed = yajl_get_bytes_consumed(this->ypc_handle);

    this->ypc_line_number
        += std::count(&jsonText[0], &jsonText[consumed], '\n');

    this->ypc_json_text = nullptr;

    if (retval != yajl_status_ok && this->ypc_error_reporter) {
        auto* msg = yajl_get_error(this->ypc_handle, 1, jsonText, jsonTextLen);

        this->ypc_error_reporter(
            *this,
            lnav_log_level_t::ERROR,
            fmt::format(FMT_STRING("error:{}:{}:invalid json -- {}"),
                        this->ypc_source,
                        this->get_line_number(),
                        reinterpret_cast<const char*>(msg))
                .c_str());
        yajl_free_error(this->ypc_handle, msg);
    }

    return retval;
}

yajl_status
yajlpp_parse_context::complete_parse()
{
    yajl_status retval = yajl_complete_parse(this->ypc_handle);

    if (retval != yajl_status_ok && this->ypc_error_reporter) {
        auto* msg = yajl_get_error(this->ypc_handle, 0, nullptr, 0);

        this->ypc_error_reporter(
            *this,
            lnav_log_level_t::ERROR,
            fmt::format(FMT_STRING("error:{}:invalid json -- {}"),
                        this->ypc_source,
                        reinterpret_cast<const char*>(msg))
                .c_str());
        yajl_free_error(this->ypc_handle, msg);
    }

    return retval;
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
        static intern_string_t SLASH = intern_string::lookup("/");

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
    this->ypc_callbacks = DEFAULT_CALLBACKS;
    memset(&this->ypc_alt_callbacks, 0, sizeof(this->ypc_alt_callbacks));
    this->ypc_sibling_handlers = nullptr;
    this->ypc_current_handler = nullptr;
    while (!this->ypc_obj_stack.empty()) {
        this->ypc_obj_stack.pop();
    }
}

void
yajlpp_parse_context::set_static_handler(json_path_handler_base& jph)
{
    this->ypc_path.clear();
    this->ypc_path.push_back('/');
    this->ypc_path.push_back('\0');
    this->ypc_path_index_stack.clear();
    this->ypc_array_index.clear();
    if (jph.jph_callbacks.yajl_null != nullptr) {
        this->ypc_callbacks.yajl_null = jph.jph_callbacks.yajl_null;
    }
    if (jph.jph_callbacks.yajl_boolean != nullptr) {
        this->ypc_callbacks.yajl_boolean = jph.jph_callbacks.yajl_boolean;
    }
    if (jph.jph_callbacks.yajl_integer != nullptr) {
        this->ypc_callbacks.yajl_integer = jph.jph_callbacks.yajl_integer;
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
    if (this->ypc_handlers) {
        len_out
            = json_ptr::decode(frag_in, &this->ypc_path[start], end - start);
        retval = frag_in;
    } else {
        retval = &this->ypc_path[start];
        len_out = end - start;
    }

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
    } else {
        return this->ypc_line_number;
    }
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
        }
        schema.gen("$schema");
        schema.gen("http://json-schema.org/draft-07/schema#");
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
    this->jph_children = &container;
    return *this;
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
    auto pattern_count = count_if(
        this->jpc_children.begin(), this->jpc_children.end(), [](auto& jph) {
            return jph.jph_is_pattern_property;
        });
    auto plain_count = this->jpc_children.size() - pattern_count;

    if (plain_count > 0) {
        yajl_gen_string(ygc.ygc_handle, "properties");
        {
            yajlpp_map properties(ygc.ygc_handle);

            for (auto& child_handler : this->jpc_children) {
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
                properties.gen(child_handler.jph_property);
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
dump_schema_to(const json_path_container& jpc,
               const char* internals_dir,
               const char* name)
{
    yajlpp_gen genner;
    yajlpp_gen_context ygc(genner, jpc);
    auto schema_path = fmt::format(FMT_STRING("{}/{}"), internals_dir, name);
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

    return string_fragment((const char*) buf, 0, len);
}
