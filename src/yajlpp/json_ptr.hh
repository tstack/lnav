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

#include <string>
#include <vector>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "yajl/api/yajl_parse.h"
#include "yajl/api/yajl_tree.h"

class json_ptr_walk {
public:
    const static yajl_callbacks callbacks;

    json_ptr_walk()
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

    void clear() { this->jpw_values.clear(); }

    void inc_array_index()
    {
        if (!this->jpw_array_indexes.empty()
            && this->jpw_array_indexes.back() != -1)
        {
            this->jpw_array_indexes.back() += 1;
        }
    }

    std::string current_ptr();

    struct walk_triple {
        walk_triple(std::string ptr, yajl_type type, std::string value)
            : wt_ptr(std::move(ptr)), wt_type(type), wt_value(std::move(value))
        {
        }

        std::string wt_ptr;
        yajl_type wt_type;
        std::string wt_value;
    };

    using walk_list_t = std::vector<walk_triple>;

    auto_mem<yajl_handle_t> jpw_handle{yajl_free};
    std::string jpw_error_msg;
    walk_list_t jpw_values;
    std::vector<std::string> jpw_keys;
    std::vector<int32_t> jpw_array_indexes;
    size_t jpw_max_ptr_len{0};
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

    static size_t encode(char* dst,
                         size_t dst_len,
                         const char* src,
                         size_t src_len = -1);

    static std::string encode_str(const char* src, size_t src_len = -1);
    static std::string encode_str(const std::string& src)
    {
        return encode_str(src.c_str(), src.size());
    }

    static size_t decode(char* dst, const char* src, ssize_t src_len = -1);
    static std::string decode(const string_fragment& sf);

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
