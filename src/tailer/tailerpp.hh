/**
 * Copyright (c) 2021, Timothy Stack
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

#ifndef lnav_tailerpp_hh
#define lnav_tailerpp_hh

#include <string>

#include "sha-256.h"
#include "auto_mem.hh"
#include "base/result.h"
#include "fmt/format.h"
#include "mapbox/variant.hpp"

#include "tailer.h"

namespace tailer {

struct packet_eof {
};

struct packet_error {
    std::string pe_path;
    std::string pe_msg;
};

struct hash_frag {
    uint8_t thf_hash[SHA_256_HASH_SIZE];

    bool operator==(const hash_frag &other) const
    {
        return memcmp(this->thf_hash, other.thf_hash, sizeof(this->thf_hash)) ==
               0;
    }
};

struct packet_log {
    std::string pl_msg;
};

struct packet_offer_block {
    std::string pob_path;
    int64_t pob_offset;
    int64_t pob_length;
    hash_frag pob_hash;
};

struct packet_tail_block {
    std::string ptb_path;
    int64_t ptb_offset;
    std::vector<uint8_t> ptb_bits;
};

using packet = mapbox::util::variant<
    packet_eof, packet_error, packet_offer_block, packet_tail_block>;

int readall(int sock, void *buf, size_t len);

inline Result<void, std::string> read_payloads_into(int fd)
{
    tailer_packet_payload_type_t payload_type;

    readall(fd, &payload_type, sizeof(payload_type));
    if (payload_type != TPPT_DONE) {
        return Err(std::string("not done"));
    }

    return Ok();
}

template<typename ...Ts>
Result<void, std::string>
read_payloads_into(int fd, std::vector<uint8_t> &bits, Ts &...args)
{
    tailer_packet_payload_type_t payload_type;

    if (readall(fd, &payload_type, sizeof(payload_type)) == -1) {
        return Err(fmt::format("unable to read bits payload type"));
    }
    if (payload_type != TPPT_BITS) {
        return Err(
            fmt::format("expecting bits payload, found: {}", payload_type));
    }

    int64_t length;
    if (readall(fd, &length, sizeof(length)) == -1) {
        return Err(std::string("unable to read bits length"));
    }

    bits.resize(length);
    if (readall(fd, bits.data(), length) == -1) {
        return Err(fmt::format("unable to read bits of length: {}", length));
    }

    return read_payloads_into(fd, args...);
}

template<typename ...Ts>
Result<void, std::string>
read_payloads_into(int fd, hash_frag &thf, Ts &...args)
{
    tailer_packet_payload_type_t payload_type;

    readall(fd, &payload_type, sizeof(payload_type));
    if (payload_type != TPPT_HASH) {
        return Err(
            fmt::format("expecting int64 payload, found: {}", payload_type));
    }

    readall(fd, thf.thf_hash, SHA_256_HASH_SIZE);

    return read_payloads_into(fd, args...);
}

template<typename ...Ts>
Result<void, std::string> read_payloads_into(int fd, int64_t &i, Ts &...args)
{
    tailer_packet_payload_type_t payload_type;

    readall(fd, &payload_type, sizeof(payload_type));
    if (payload_type != TPPT_INT64) {
        return Err(
            fmt::format("expecting int64 payload, found: {}", payload_type));
    }

    readall(fd, &i, sizeof(i));

    return read_payloads_into(fd, args...);
}

template<typename ...Ts>
Result<void, std::string>
read_payloads_into(int fd, std::string &str, Ts &...args)
{
    tailer_packet_payload_type_t payload_type;

    readall(fd, &payload_type, sizeof(payload_type));
    if (payload_type != TPPT_STRING) {
        printf("not a string! %d\n", payload_type);
        return Err(
            fmt::format("expecting string payload, found: {}", payload_type));
    }

    int32_t length;
    if (readall(fd, &length, sizeof(length)) == -1) {
        return Err(std::string("unable to read string length"));
    }

    auto_mem<char> child_str;

    child_str = (char *) malloc(length);
    if (child_str == nullptr) {
        return Err(fmt::format("string size is too large: {}", length));
    }
    if (readall(fd, child_str, length) == -1) {
        return Err(fmt::format("unable to read string of size: {}", length));
    }
    str.assign(child_str.in(), length);

    return read_payloads_into(fd, args...);
}

Result<packet, std::string> read_packet(int fd);

}

#endif
