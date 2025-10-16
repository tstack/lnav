/**
 * Copyright (c) 2014-2019, Timothy Stack
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
 * @file json_ptr.hh
 */

#ifndef json_ptr_hh
#define json_ptr_hh

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "ArenaAlloc/arenaalloc.h"
#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/lnav.resolver.hh"
#include "base/result.h"
#include "base/short_alloc.h"
#include "yajl/api/yajl_parse.h"
#include "yajl/api/yajl_tree.h"

class json_ptr_walk {
public:
    const static yajl_callbacks callbacks;

    using walk_callback_t
        = std::function<void(const std::string& ptr, const scoped_value_t&)>;

    json_ptr_walk(walk_callback_t cb) : jpw_callback(cb)
    {
        this->jpw_handle = yajl_alloc(&callbacks, nullptr, this);
    }

    yajl_status parse(const unsigned char* buffer, ssize_t len);

    yajl_status parse(const char* buffer, ssize_t len)
    {
        return this->parse((const unsigned char*) buffer, len);
    }

    yajl_status parse(const string_fragment& sf)
    {
        return this->parse(sf.udata(), sf.length());
    }

    yajl_status complete_parse();

    yajl_status parse_fully(const string_fragment& sf)
    {
        auto retval = this->parse(sf);
        if (retval == yajl_status_ok) {
            retval = this->complete_parse();
        }

        return retval;
    }

    void update_error_msg(yajl_status status,
                          const unsigned char* buffer,
                          ssize_t len);

    void inc_array_index();

    void pop_component()
    {
        if (!this->jpw_components.empty()) {
            this->jpw_components.pop_back();
            if (this->jpw_components.empty()) {
                this->jpw_ptr_str.clear();
            } else {
                this->jpw_ptr_str.resize(this->jpw_components.back());
            }
        }
    }

    void push_component_verbatim(const string_fragment& in);

    void push_component(const string_fragment& in);

    const std::string& current_ptr();

    auto_mem<yajl_handle_t> jpw_handle{yajl_free};
    std::string jpw_ptr_str;
    std::vector<int32_t> jpw_array_indexes;
    std::vector<size_t> jpw_components;
    std::string jpw_error_msg;
    size_t jpw_max_ptr_len{0};
    walk_callback_t jpw_callback;
};

struct json_walk_collector {
    static Result<json_walk_collector, std::string> parse_fully(
        string_fragment in)
    {
        json_walk_collector jwc;
        json_ptr_walk jpw(
            [&jwc](const std::string& ptr, const scoped_value_t& sv) {
                jwc.jwc_values.emplace_back(ptr, sv);
            });
        if (jpw.parse_fully(in) == yajl_status_ok) {
            return Ok(jwc);
        }
        return Err(jpw.jpw_error_msg);
    }

    std::vector<std::pair<std::string, scoped_value_t>> jwc_values;
};

class json_ptr {
public:
    enum class match_state_t {
        DONE,
        VALUE,
        ERR_INVALID_TYPE,
        ERR_NO_SLASH,
        ERR_INVALID_ESCAPE,
        ERR_INVALID_INDEX,
    };

    static string_fragment encode(string_fragment in, stack_buf& buf);
    static void encode_to(string_fragment in, std::string& out);

    static string_fragment decode(string_fragment in, stack_buf& buf);

    json_ptr(const char* value) : jp_value(value), jp_pos(value) {}

    bool expect_map(int32_t& depth, int32_t& index);

    bool at_key(int32_t depth, const char* component, ssize_t len = -1);

    void exit_container(int32_t& depth, int32_t& index);

    bool expect_array(int32_t& depth, int32_t& index);

    bool at_index(int32_t& depth, int32_t& index, bool primitive = true);

    bool reached_end() const { return this->jp_pos[0] == '\0'; }

    std::string error_msg() const;

    const char* jp_value;
    const char* jp_pos;
    int32_t jp_depth{0};
    int32_t jp_array_index{-1};
    match_state_t jp_state{match_state_t::VALUE};
};

#endif
