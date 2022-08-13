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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lnav_tailerpp_hh
#define lnav_tailerpp_hh

#include <string>
#include <vector>

#include "base/result.h"
#include "fmt/format.h"
#include "mapbox/variant.hpp"
#include "sha-256.h"
#include "tailer.h"

namespace tailer {

struct packet_eof {};

struct packet_error {
    std::string pe_path;
    std::string pe_msg;
};

struct packet_announce {
    std::string pa_uname;
};

struct hash_frag {
    uint8_t thf_hash[SHA256_BLOCK_SIZE];

    bool operator==(const hash_frag& other) const
    {
        return memcmp(this->thf_hash, other.thf_hash, sizeof(this->thf_hash))
            == 0;
    }
};

struct packet_log {
    std::string pl_msg;
};

struct packet_offer_block {
    std::string pob_root_path;
    std::string pob_path;
    int64_t pob_mtime;
    int64_t pob_offset;
    int64_t pob_length;
    hash_frag pob_hash;
};

struct packet_tail_block {
    std::string ptb_root_path;
    std::string ptb_path;
    int64_t ptb_mtime;
    int64_t ptb_offset;
    std::vector<uint8_t> ptb_bits;
};

struct packet_synced {
    std::string ps_root_path;
    std::string ps_path;
};

struct packet_link {
    std::string pl_root_path;
    std::string pl_path;
    std::string pl_link_value;
};

struct packet_preview_error {
    int64_t ppe_id;
    std::string ppe_path;
    std::string ppe_msg;
};

struct packet_preview_data {
    int64_t ppd_id;
    std::string ppd_path;
    std::vector<uint8_t> ppd_bits;
};

struct packet_possible_path {
    std::string ppp_path;
};

using packet = mapbox::util::variant<packet_eof,
                                     packet_announce,
                                     packet_error,
                                     packet_offer_block,
                                     packet_tail_block,
                                     packet_link,
                                     packet_preview_error,
                                     packet_preview_data,
                                     packet_possible_path,
                                     packet_synced>;

struct recv_payload_type {};
struct recv_payload_length {};
struct recv_payload_content {};

int readall(int sock, void* buf, size_t len);

namespace details {

template<class...>
using void_t = void;

template<class, class = void>
struct has_data : std::false_type {};

template<class T>
struct has_data<T, decltype(void(std::declval<T&>().data()))>
    : std::true_type {};

template<typename T, std::enable_if_t<has_data<T>::value, bool> = true>
uint8_t*
get_data(T& t)
{
    return (uint8_t*) t.data();
}

template<typename T, std::enable_if_t<!has_data<T>::value, bool> = true>
uint8_t*
get_data(T& t)
{
    return (uint8_t*) &t;
}

}  // namespace details

template<int PAYLOAD_TYPE, typename EXPECT = recv_payload_type>
struct protocol_recv {
    static constexpr bool HAS_LENGTH = (PAYLOAD_TYPE == TPPT_STRING)
        || (PAYLOAD_TYPE == TPPT_BITS);

    using after_type = typename std::conditional<HAS_LENGTH,
                                                 recv_payload_length,
                                                 recv_payload_content>::type;

    static Result<protocol_recv<PAYLOAD_TYPE, after_type>, std::string> create(
        int fd)
    {
        return protocol_recv<PAYLOAD_TYPE>(fd).read_type();
    }

    Result<protocol_recv<PAYLOAD_TYPE, after_type>, std::string> read_type() &&
    {
        static_assert(std::is_same<EXPECT, recv_payload_type>::value,
                      "read_type() cannot be called in this state");

        tailer_packet_payload_type_t payload_type;

        if (readall(this->pr_fd, &payload_type, sizeof(payload_type)) == -1) {
            return Err(
                fmt::format(FMT_STRING("unable to read payload type: {}"),
                            strerror(errno)));
        }

        if (payload_type != PAYLOAD_TYPE) {
            return Err(fmt::format(
                FMT_STRING("payload-type mismatch, got: {}; expected: {}"),
                (int) payload_type,
                PAYLOAD_TYPE));
        }

        return Ok(protocol_recv<PAYLOAD_TYPE, after_type>(this->pr_fd));
    }

    template<typename T>
    Result<protocol_recv<PAYLOAD_TYPE, recv_payload_content>, std::string>
    read_length(T& data) &&
    {
        static_assert(std::is_same<EXPECT, recv_payload_length>::value,
                      "read_length() cannot be called in this state");

        if (readall(this->pr_fd, &this->pr_length, sizeof(this->pr_length))
            == -1)
        {
            return Err(
                fmt::format(FMT_STRING("unable to read content length: {}"),
                            strerror(errno)));
        }

        try {
            data.resize(this->pr_length);
        } catch (...) {
            return Err(fmt::format(FMT_STRING("unable to resize data to {}"),
                                   this->pr_length));
        }

        return Ok(protocol_recv<PAYLOAD_TYPE, recv_payload_content>(
            this->pr_fd, this->pr_length));
    }

    template<typename T>
    Result<void, std::string> read_content(T& data) &&
    {
        static_assert(std::is_same<EXPECT, recv_payload_content>::value,
                      "read_content() cannot be called in this state");
        static_assert(!HAS_LENGTH || details::has_data<T>::value, "boo");

        if (!HAS_LENGTH) {
            this->pr_length = sizeof(T);
        }
        if (readall(this->pr_fd, details::get_data(data), this->pr_length)
            == -1)
        {
            return Err(fmt::format(FMT_STRING("unable to read content -- {}"),
                                   strerror(errno)));
        }

        return Ok();
    }

private:
    template<int P, typename E>
    friend struct protocol_recv;

    explicit protocol_recv(int fd, int32_t length = 0)
        : pr_fd(fd), pr_length(length)
    {
    }

    int pr_fd;
    int32_t pr_length;
};

inline Result<void, std::string>
read_payloads_into(int fd)
{
    tailer_packet_payload_type_t payload_type;

    readall(fd, &payload_type, sizeof(payload_type));
    if (payload_type != TPPT_DONE) {
        return Err(std::string("not done"));
    }

    return Ok();
}

template<typename... Ts>
Result<void, std::string> read_payloads_into(int fd,
                                             std::string& str,
                                             Ts&... args);

template<typename... Ts>
Result<void, std::string>
read_payloads_into(int fd, std::vector<uint8_t>& bits, Ts&... args)
{
    TRY(TRY(TRY(protocol_recv<TPPT_BITS>::create(fd)).read_length(bits))
            .read_content(bits));

    return read_payloads_into(fd, args...);
}

template<typename... Ts>
Result<void, std::string>
read_payloads_into(int fd, hash_frag& thf, Ts&... args)
{
    TRY(TRY(protocol_recv<TPPT_HASH>::create(fd)).read_content(thf.thf_hash));

    return read_payloads_into(fd, args...);
}

template<typename... Ts>
Result<void, std::string>
read_payloads_into(int fd, int64_t& i, Ts&... args)
{
    TRY(TRY(protocol_recv<TPPT_INT64>::create(fd)).read_content(i));

    return read_payloads_into(fd, args...);
}

template<typename... Ts>
Result<void, std::string>
read_payloads_into(int fd, std::string& str, Ts&... args)
{
    TRY(TRY(TRY(protocol_recv<TPPT_STRING>::create(fd)).read_length(str))
            .read_content(str));

    return read_payloads_into(fd, args...);
}

Result<packet, std::string> read_packet(int fd);

}  // namespace tailer

#endif
