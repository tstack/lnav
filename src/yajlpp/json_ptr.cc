/**
 * Copyright (c) 2014, Timothy Stack
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
 * @file json_ptr.cc
 */

#include "yajlpp/json_ptr.hh"

#include "base/intern_string.hh"
#include "base/lnav_log.hh"
#include "base/short_alloc.h"
#include "config.h"
#include "fmt/format.h"
#include "yajl/api/yajl_gen.h"

static int
handle_null(void* ctx)
{
    auto* jpw = (json_ptr_walk*) ctx;

    jpw->jpw_callback(jpw->jpw_ptr_str.c_str(), null_value_t{});
    jpw->inc_array_index();

    return 1;
}

static int
handle_boolean(void* ctx, int boolVal)
{
    auto* jpw = (json_ptr_walk*) ctx;

    jpw->jpw_callback(jpw->jpw_ptr_str.c_str(), static_cast<bool>(boolVal));
    jpw->inc_array_index();

    return 1;
}

static int
handle_integer(void* ctx, long long int integerVal)
{
    auto jpw = (json_ptr_walk*) ctx;

    jpw->jpw_callback(jpw->jpw_ptr_str.c_str(), int64_t{integerVal});
    jpw->inc_array_index();

    return 1;
}

static int
handle_float(void* ctx, double floatVal)
{
    auto jpw = (json_ptr_walk*) ctx;

    jpw->jpw_callback(jpw->jpw_ptr_str.c_str(), floatVal);
    jpw->inc_array_index();

    return 1;
}

static void
appender(void* ctx, const char* strVal, size_t strLen)
{
    auto& str = *(std::string*) ctx;

    str.append(strVal, strLen);
}

static int
handle_string(void* ctx,
              const unsigned char* stringVal,
              size_t len,
              yajl_string_props_t*)
{
    auto jpw = (json_ptr_walk*) ctx;
    auto_mem<yajl_gen_t> gen(yajl_gen_free);
    std::string str;

    gen = yajl_gen_alloc(nullptr);
    yajl_gen_config(gen.in(), yajl_gen_print_callback, appender, &str);
    yajl_gen_string(gen.in(), stringVal, len);
    jpw->jpw_callback(jpw->current_ptr(), str);
    jpw->inc_array_index();

    return 1;
}

static int
handle_start_map(void* ctx)
{
    auto jpw = (json_ptr_walk*) ctx;

    jpw->push_component_verbatim("/"_frag);
    jpw->push_component_verbatim(""_frag);
    jpw->jpw_array_indexes.push_back(-1);

    return 1;
}

static int
handle_map_key(void* ctx,
               const unsigned char* key,
               size_t len,
               yajl_string_props_t* props)
{
    auto frag = string_fragment::from_bytes(key, len);
    auto jpw = (json_ptr_walk*) ctx;
    stack_buf allocator;

    jpw->pop_component();
    jpw->push_component(frag);

    return 1;
}

static int
handle_end_map(void* ctx)
{
    auto jpw = (json_ptr_walk*) ctx;

    jpw->pop_component();  // last key
    jpw->pop_component();  // slash
    jpw->jpw_array_indexes.pop_back();
    jpw->inc_array_index();

    return 1;
}

static int
handle_start_array(void* ctx)
{
    auto jpw = (json_ptr_walk*) ctx;

    jpw->push_component_verbatim("/"_frag);
    jpw->jpw_array_indexes.push_back(0);
    fmt::format_to(std::back_inserter(jpw->jpw_ptr_str),
                   FMT_STRING("{}"),
                   jpw->jpw_array_indexes.back());
    jpw->jpw_components.emplace_back(jpw->jpw_ptr_str.size());

    return 1;
}

static int
handle_end_array(void* ctx)
{
    auto jpw = (json_ptr_walk*) ctx;

    jpw->pop_component();  // last key
    jpw->pop_component();  // slash
    jpw->jpw_array_indexes.pop_back();
    jpw->inc_array_index();

    return 1;
}

const yajl_callbacks json_ptr_walk::callbacks = {
    handle_null,
    handle_boolean,
    handle_integer,
    handle_float,
    nullptr,
    handle_string,
    handle_start_map,
    handle_map_key,
    handle_end_map,
    handle_start_array,
    handle_end_array,
};

string_fragment
json_ptr::encode(string_fragment in, stack_buf& buf)
{
    auto outlen = in.length();

    for (const auto ch : in) {
        switch (ch) {
            case '/':
            case '~':
            case '#':
                outlen += 1;
                break;
        }
    }

    auto* outbuf = buf.allocate(outlen);
    auto out_index = std::size_t{0};

    for (const auto ch : in) {
        switch (ch) {
            case '~':
                outbuf[out_index++] = '~';
                outbuf[out_index++] = '0';
                break;
            case '/':
                outbuf[out_index++] = '~';
                outbuf[out_index++] = '1';
                break;
            case '#':
                outbuf[out_index++] = '~';
                outbuf[out_index++] = '2';
                break;
            default:
                outbuf[out_index++] = ch;
                break;
        }
    }

    return string_fragment::from_bytes(outbuf, outlen);
}

void
json_ptr::encode_to(string_fragment in, std::string& out)
{
    for (const auto ch : in) {
        switch (ch) {
            case '~':
                out.push_back('~');
                out.push_back('0');
                break;
            case '/':
                out.push_back('~');
                out.push_back('1');
                break;
            case '#':
                out.push_back('~');
                out.push_back('2');
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
}

string_fragment
json_ptr::decode(string_fragment sf, stack_buf& allocator)
{
    auto* outbuf = allocator.allocate(sf.length());
    auto in_escape = false;
    auto out_index = std::size_t{0};

    for (const auto ch : sf) {
        if (in_escape) {
            switch (ch) {
                case '0':
                    outbuf[out_index++] = '~';
                    break;
                case '1':
                    outbuf[out_index++] = '/';
                    break;
                case '2':
                    outbuf[out_index++] = '#';
                    break;
                default:
                    break;
            }
            in_escape = false;
        } else if (ch == '~') {
            in_escape = true;
        } else {
            outbuf[out_index++] = ch;
        }
    }

    return string_fragment::from_bytes(outbuf, out_index);
}

bool
json_ptr::expect_map(int32_t& depth, int32_t& index)
{
    bool retval;

    if (this->jp_state == match_state_t::DONE) {
        retval = true;
    } else if (depth != this->jp_depth) {
        retval = true;
    } else if (this->reached_end()) {
        retval = true;
    } else if (this->jp_state == match_state_t::VALUE
               && (this->jp_array_index == -1
                   || ((index - 1) == this->jp_array_index)))
    {
        if (this->jp_pos[0] == '/') {
            this->jp_pos += 1;
            this->jp_depth += 1;
            this->jp_state = match_state_t::VALUE;
            this->jp_array_index = -1;
            index = -1;
        }
        retval = true;
    } else {
        retval = true;
    }
    depth += 1;

    return retval;
}

bool
json_ptr::at_key(int32_t depth, const char* component, ssize_t len)
{
    const char* component_end;
    int lpc;

    if (this->jp_state == match_state_t::DONE || depth != this->jp_depth) {
        return true;
    }

    if (len == -1) {
        len = strlen(component);
    }
    component_end = component + len;

    for (lpc = 0; component < component_end; lpc++, component++) {
        char ch = this->jp_pos[lpc];

        if (this->jp_pos[lpc] == '~') {
            switch (this->jp_pos[lpc + 1]) {
                case '0':
                    ch = '~';
                    break;
                case '1':
                    ch = '/';
                    break;
                default:
                    this->jp_state = match_state_t::ERR_INVALID_ESCAPE;
                    return false;
            }
            lpc += 1;
        } else if (this->jp_pos[lpc] == '/') {
            ch = '\0';
        }

        if (ch != *component) {
            return true;
        }
    }

    this->jp_pos += lpc;
    this->jp_state = match_state_t::VALUE;

    return true;
}

void
json_ptr::exit_container(int32_t& depth, int32_t& index)
{
    depth -= 1;
    if (this->jp_state == match_state_t::VALUE && depth == this->jp_depth
        && (index == -1 || (index - 1 == this->jp_array_index))
        && this->reached_end())
    {
        this->jp_state = match_state_t::DONE;
        index = -1;
    }
}

bool
json_ptr::expect_array(int32_t& depth, int32_t& index)
{
    bool retval;

    if (this->jp_state == match_state_t::DONE) {
        retval = true;
    } else if (depth != this->jp_depth) {
        retval = true;
    } else if (this->reached_end()) {
        retval = true;
    } else if (this->jp_pos[0] == '/' && index == this->jp_array_index) {
        int offset;

        this->jp_depth += 1;

        if (sscanf(this->jp_pos, "/%d%n", &this->jp_array_index, &offset) != 1)
        {
            this->jp_state = match_state_t::ERR_INVALID_INDEX;
            retval = true;
        } else if (this->jp_pos[offset] != '\0' && this->jp_pos[offset] != '/')
        {
            this->jp_state = match_state_t::ERR_INVALID_INDEX;
            retval = true;
        } else {
            index = 0;
            this->jp_pos += offset;
            this->jp_state = match_state_t::VALUE;
            retval = true;
        }
    } else {
        this->jp_state = match_state_t::ERR_NO_SLASH;
        retval = true;
    }

    depth += 1;

    return retval;
}

bool
json_ptr::at_index(int32_t& depth, int32_t& index, bool primitive)
{
    bool retval;

    if (this->jp_state == match_state_t::DONE) {
        retval = false;
    } else if (depth < this->jp_depth) {
        retval = false;
    } else if (depth == this->jp_depth) {
        if (index == -1) {
            if (this->jp_array_index == -1) {
                retval = this->reached_end();
                if (primitive && retval) {
                    this->jp_state = match_state_t::DONE;
                }
            } else {
                retval = false;
            }
        } else if (index == this->jp_array_index) {
            retval = this->reached_end();
            this->jp_array_index = -1;
            index = -1;
            if (primitive && retval) {
                this->jp_state = match_state_t::DONE;
            }
        } else {
            index += 1;
            retval = false;
        }
    } else if (index == -1) {
        retval = this->reached_end();
    } else {
        retval = false;
    }

    return retval;
}

std::string
json_ptr::error_msg() const
{
    switch (this->jp_state) {
        case match_state_t::ERR_INVALID_ESCAPE:
            return fmt::format(FMT_STRING("invalid escape sequence near -- {}"),
                               this->jp_pos);
        case match_state_t::ERR_INVALID_INDEX:
            return fmt::format(FMT_STRING("expecting array index at -- {}"),
                               this->jp_pos);
        case match_state_t::ERR_INVALID_TYPE:
            return fmt::format(FMT_STRING("expecting container at -- {}"),
                               this->jp_pos);
        default:
            break;
    }

    return "";
}

void
json_ptr_walk::push_component(const string_fragment& in)
{
    json_ptr::encode_to(in, this->jpw_ptr_str);
    this->jpw_components.emplace_back(this->jpw_ptr_str.size());
}

void
json_ptr_walk::push_component_verbatim(const string_fragment& in)
{
    this->jpw_ptr_str += in;
    this->jpw_components.emplace_back(this->jpw_ptr_str.size());
}

const std::string&
json_ptr_walk::current_ptr()
{
    this->jpw_max_ptr_len
        = std::max(this->jpw_max_ptr_len, this->jpw_ptr_str.size());
    return this->jpw_ptr_str;
}

void
json_ptr_walk::update_error_msg(yajl_status status,
                                const unsigned char* buffer,
                                ssize_t len)
{
    switch (status) {
        case yajl_status_ok:
            break;
        case yajl_status_client_canceled:
            this->jpw_error_msg = "internal error";
            break;
        case yajl_status_error: {
            auto* msg = yajl_get_error(this->jpw_handle, 1, buffer, len);
            this->jpw_error_msg = std::string((const char*) msg);

            yajl_free_error(this->jpw_handle, msg);
            break;
        }
    }
}

void
json_ptr_walk::inc_array_index()
{
    if (!this->jpw_array_indexes.empty()
        && this->jpw_array_indexes.back() != -1)
    {
        this->jpw_array_indexes.back() += 1;
        this->pop_component();
        fmt::format_to(std::back_inserter(this->jpw_ptr_str),
                       FMT_STRING("{}"),
                       this->jpw_array_indexes.back());
        this->jpw_components.emplace_back(this->jpw_ptr_str.size());
    }
}

yajl_status
json_ptr_walk::complete_parse()
{
    auto retval = yajl_complete_parse(this->jpw_handle);
    this->update_error_msg(retval, nullptr, 0);
    return retval;
}

yajl_status
json_ptr_walk::parse(const unsigned char* buffer, ssize_t len)
{
    auto retval = yajl_parse(this->jpw_handle, buffer, len);
    this->update_error_msg(retval, buffer, len);
    return retval;
}
