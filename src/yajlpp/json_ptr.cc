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

#ifdef __CYGWIN__
#    include <alloca.h>
#endif

#include "config.h"
#include "fmt/format.h"
#include "yajl/api/yajl_gen.h"
#include "yajlpp/json_ptr.hh"

static int
handle_null(void* ctx)
{
    json_ptr_walk* jpw = (json_ptr_walk*) ctx;

    jpw->jpw_values.emplace_back(jpw->current_ptr(), yajl_t_null, "null");
    jpw->inc_array_index();

    return 1;
}

static int
handle_boolean(void* ctx, int boolVal)
{
    json_ptr_walk* jpw = (json_ptr_walk*) ctx;

    jpw->jpw_values.emplace_back(jpw->current_ptr(),
                                 boolVal ? yajl_t_true : yajl_t_false,
                                 boolVal ? "true" : "false");
    jpw->inc_array_index();

    return 1;
}

static int
handle_number(void* ctx, const char* numberVal, size_t numberLen)
{
    json_ptr_walk* jpw = (json_ptr_walk*) ctx;

    jpw->jpw_values.emplace_back(
        jpw->current_ptr(), yajl_t_number, std::string(numberVal, numberLen));
    jpw->inc_array_index();

    return 1;
}

static void
appender(void* ctx, const char* strVal, size_t strLen)
{
    std::string& str = *(std::string*) ctx;

    str.append(strVal, strLen);
}

static int
handle_string(void* ctx, const unsigned char* stringVal, size_t len)
{
    json_ptr_walk* jpw = (json_ptr_walk*) ctx;
    auto_mem<yajl_gen_t> gen(yajl_gen_free);
    std::string str;

    gen = yajl_gen_alloc(nullptr);
    yajl_gen_config(gen.in(), yajl_gen_print_callback, appender, &str);
    yajl_gen_string(gen.in(), stringVal, len);
    jpw->jpw_values.emplace_back(jpw->current_ptr(), yajl_t_string, str);
    jpw->inc_array_index();

    return 1;
}

static int
handle_start_map(void* ctx)
{
    json_ptr_walk* jpw = (json_ptr_walk*) ctx;

    jpw->jpw_keys.emplace_back("");
    jpw->jpw_array_indexes.push_back(-1);

    return 1;
}

static int
handle_map_key(void* ctx, const unsigned char* key, size_t len)
{
    json_ptr_walk* jpw = (json_ptr_walk*) ctx;
    char partially_encoded_key[len + 32];
    size_t required_len;

    jpw->jpw_keys.pop_back();

    required_len = json_ptr::encode(partially_encoded_key,
                                    sizeof(partially_encoded_key),
                                    (const char*) key,
                                    len);
    if (required_len < sizeof(partially_encoded_key)) {
        jpw->jpw_keys.emplace_back(&partially_encoded_key[0], required_len);
    } else {
        auto fully_encoded_key = (char*) alloca(required_len);

        json_ptr::encode(
            fully_encoded_key, required_len, (const char*) key, len);
        jpw->jpw_keys.emplace_back(&fully_encoded_key[0], required_len);
    }

    return 1;
}

static int
handle_end_map(void* ctx)
{
    json_ptr_walk* jpw = (json_ptr_walk*) ctx;

    jpw->jpw_keys.pop_back();
    jpw->jpw_array_indexes.pop_back();

    jpw->inc_array_index();

    return 1;
}

static int
handle_start_array(void* ctx)
{
    json_ptr_walk* jpw = (json_ptr_walk*) ctx;

    jpw->jpw_keys.emplace_back("");
    jpw->jpw_array_indexes.push_back(0);

    return 1;
}

static int
handle_end_array(void* ctx)
{
    json_ptr_walk* jpw = (json_ptr_walk*) ctx;

    jpw->jpw_keys.pop_back();
    jpw->jpw_array_indexes.pop_back();
    jpw->inc_array_index();

    return 1;
}

const yajl_callbacks json_ptr_walk::callbacks = {handle_null,
                                                 handle_boolean,
                                                 nullptr,
                                                 nullptr,
                                                 handle_number,
                                                 handle_string,
                                                 handle_start_map,
                                                 handle_map_key,
                                                 handle_end_map,
                                                 handle_start_array,
                                                 handle_end_array};

size_t
json_ptr::encode(char* dst, size_t dst_len, const char* src, size_t src_len)
{
    size_t retval = 0;

    if (src_len == (size_t) -1) {
        src_len = strlen(src);
    }

    for (size_t lpc = 0; lpc < src_len; lpc++) {
        switch (src[lpc]) {
            case '/':
            case '~':
            case '#':
                if (retval < dst_len) {
                    dst[retval] = '~';
                    retval += 1;
                    if (src[lpc] == '~') {
                        dst[retval] = '0';
                    } else if (src[lpc] == '#') {
                        dst[retval] = '2';
                    } else {
                        dst[retval] = '1';
                    }
                } else {
                    retval += 1;
                }
                break;
            default:
                if (retval < dst_len) {
                    dst[retval] = src[lpc];
                }
                break;
        }
        retval += 1;
    }
    if (retval < dst_len) {
        dst[retval] = '\0';
    }

    return retval;
}

size_t
json_ptr::decode(char* dst, const char* src, ssize_t src_len)
{
    size_t retval = 0;

    if (src_len == -1) {
        src_len = strlen(src);
    }

    for (int lpc = 0; lpc < src_len; lpc++) {
        switch (src[lpc]) {
            case '~':
                if ((lpc + 1) < src_len) {
                    switch (src[lpc + 1]) {
                        case '0':
                            dst[retval++] = '~';
                            lpc += 1;
                            break;
                        case '1':
                            dst[retval++] = '/';
                            lpc += 1;
                            break;
                        case '2':
                            dst[retval++] = '#';
                            lpc += 1;
                            break;
                        default:
                            break;
                    }
                }
                break;
            default:
                dst[retval++] = src[lpc];
                break;
        }
    }

    dst[retval] = '\0';

    return retval;
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

std::string
json_ptr_walk::current_ptr()
{
    std::string retval;

    for (size_t lpc = 0; lpc < this->jpw_array_indexes.size(); lpc++) {
        retval.append("/");
        if (this->jpw_array_indexes[lpc] == -1) {
            retval.append(this->jpw_keys[lpc]);
        } else {
            fmt::format_to(std::back_inserter(retval),
                           FMT_STRING("{}"),
                           this->jpw_array_indexes[lpc]);
        }
    }

    this->jpw_max_ptr_len = std::max(this->jpw_max_ptr_len, retval.size());

    return retval;
}

void
json_ptr_walk::update_error_msg(yajl_status status,
                                const char* buffer,
                                ssize_t len)
{
    switch (status) {
        case yajl_status_ok:
            break;
        case yajl_status_client_canceled:
            this->jpw_error_msg = "internal error";
            break;
        case yajl_status_error: {
            auto* msg = yajl_get_error(
                this->jpw_handle, 1, (const unsigned char*) buffer, len);
            this->jpw_error_msg = std::string((const char*) msg);

            yajl_free_error(this->jpw_handle, msg);
            break;
        }
    }
}

yajl_status
json_ptr_walk::complete_parse()
{
    yajl_status retval;

    retval = yajl_complete_parse(this->jpw_handle);
    this->update_error_msg(retval, nullptr, -1);
    return retval;
}

yajl_status
json_ptr_walk::parse(const char* buffer, ssize_t len)
{
    yajl_status retval;

    retval = yajl_parse(this->jpw_handle, (const unsigned char*) buffer, len);
    this->update_error_msg(retval, buffer, len);
    return retval;
}
