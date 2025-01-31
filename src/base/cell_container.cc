/**
 * Copyright (c) 2025, Timothy Stack
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

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>

#include "cell_container.hh"

#include <zlib.h>

namespace lnav {

static constexpr auto DEFAULT_CHUNK_SIZE = size_t{32 * 1024};
static constexpr auto NULL_CELL_SUB = 0x1;

static uint8_t
combine_type_value(uint8_t type, uint8_t subvalue)
{
    return type | subvalue << 2U;
}

cell_chunk::cell_chunk(cell_container* parent,
                       std::unique_ptr<unsigned char[]> data,
                       size_t capacity)
    : cc_parent(parent), cc_data(std::move(data)), cc_capacity(capacity)
{
}

void
cell_chunk::reset()
{
    this->cc_next.reset();
    this->cc_size = 0;
    if (this->cc_data == nullptr) {
        this->cc_data = std::make_unique<unsigned char[]>(this->cc_capacity);
    }
    this->cc_compressed.reset();
}

void
cell_chunk::load() const
{
    if (this->cc_data != nullptr) {
        return;
    }

    this->cc_data = std::make_unique<unsigned char[]>(this->cc_capacity);
    uLongf cap = this->cc_capacity;
    auto rc = uncompress(this->cc_data.get(),
                         &cap,
                         this->cc_compressed.get(),
                         this->cc_compressed_size);
    ensure(rc == Z_OK);
}

cell_container::cell_container()
    : cc_first(std::make_unique<cell_chunk>(
          this,
          std::make_unique<unsigned char[]>(DEFAULT_CHUNK_SIZE),
          DEFAULT_CHUNK_SIZE)),
      cc_last(cc_first.get()),
      cc_compress_buffer(auto_buffer::alloc(compressBound(DEFAULT_CHUNK_SIZE))),
      cc_chunk_cache({})
{
    // log_debug("cell container %p  chunk %p", this, this->cc_last);
}

unsigned char*
cell_container::alloc(size_t amount)
{
    if (this->cc_last->available() < amount) {
        auto last = this->cc_last;

        auto buflen = compressBound(last->cc_size);
        if (this->cc_compress_buffer.capacity() < buflen) {
            this->cc_compress_buffer.expand_to(buflen);
        }

        auto rc = compress2(this->cc_compress_buffer.u_in(),
                            &buflen,
                            last->cc_data.get(),
                            last->cc_size,
                            2);
        require(rc == Z_OK);
        this->cc_compress_buffer.resize(buflen);
        last->cc_compressed = this->cc_compress_buffer.to_unique();
        last->cc_compressed_size = buflen;

        auto chunk_size = std::max(amount, DEFAULT_CHUNK_SIZE);
        if (chunk_size > last->cc_capacity) {
            last->cc_data = std::make_unique<unsigned char[]>(chunk_size);
        }
        last->cc_next = std::make_unique<cell_chunk>(
            this, std::move(last->cc_data), chunk_size);
        this->cc_last = last->cc_next.get();
        // log_debug("allocating chunk with %lu %p", chunk_size, this->cc_last);

        ensure(this->cc_last->cc_capacity >= chunk_size);
    }

    return this->cc_last->alloc(amount);
}

void
cell_container::load_chunk_into_cache(const cell_chunk* cc)
{
    if (cc->cc_data != nullptr) {
        return;
    }

    if (this->cc_chunk_cache[2] != nullptr) {
        this->cc_chunk_cache[2]->evict();
    }
    this->cc_chunk_cache[2] = this->cc_chunk_cache[1];
    this->cc_chunk_cache[1] = this->cc_chunk_cache[0];
    this->cc_chunk_cache[0] = cc;
    cc->load();
}

void
cell_container::reset()
{
    this->cc_last = this->cc_first.get();
    this->cc_first->reset();
    this->cc_chunk_cache = {};
}

void
cell_container::push_null_cell()
{
    auto* cell_data = this->alloc(1);

    // log_debug("push null");
    cell_data[0] = combine_type_value(cell_type::CT_NULL, NULL_CELL_SUB);
}

void
cell_container::push_int_cell(int64_t i)
{
    auto* cell_data = this->alloc(1 + 8);

    // log_debug("push int %d", i);
    cell_data[0] = combine_type_value(cell_type::CT_INTEGER, 0);
    const auto* i_bits = reinterpret_cast<const uint8_t*>(&i);
    cell_data[1] = i_bits[0];
    cell_data[2] = i_bits[1];
    cell_data[3] = i_bits[2];
    cell_data[4] = i_bits[3];
    cell_data[5] = i_bits[4];
    cell_data[6] = i_bits[5];
    cell_data[7] = i_bits[6];
    cell_data[8] = i_bits[7];
}

void
cell_container::push_float_cell(double f)
{
    auto* cell_data = this->alloc(1 + 8);

    // log_debug("push float %f", f);
    cell_data[0] = combine_type_value(cell_type::CT_FLOAT, 0);

    const auto* f_bits = reinterpret_cast<const uint8_t*>(&f);
    cell_data[1] = f_bits[0];
    cell_data[2] = f_bits[1];
    cell_data[3] = f_bits[2];
    cell_data[4] = f_bits[3];
    cell_data[5] = f_bits[4];
    cell_data[6] = f_bits[5];
    cell_data[7] = f_bits[6];
    cell_data[8] = f_bits[7];
}

void
cell_container::push_float_with_units_cell(double actual,
                                           const string_fragment& as_str)
{
    require(as_str.length() < SHORT_TEXT_LENGTH);

    auto* cell_data = this->alloc(1 + 8 + as_str.length());

    // log_debug("push float %f", f);
    cell_data[0] = combine_type_value(cell_type::CT_FLOAT, as_str.length());

    const auto* actual_bits = reinterpret_cast<const uint8_t*>(&actual);
    cell_data[1] = actual_bits[0];
    cell_data[2] = actual_bits[1];
    cell_data[3] = actual_bits[2];
    cell_data[4] = actual_bits[3];
    cell_data[5] = actual_bits[4];
    cell_data[6] = actual_bits[5];
    cell_data[7] = actual_bits[6];
    cell_data[8] = actual_bits[7];

    memcpy(&cell_data[9], as_str.data(), as_str.length());
}

void
cell_container::push_text_cell(const string_fragment& sf)
{
    // log_debug("push text %.*s", sf.length(), sf.data());
    if (sf.length() < SHORT_TEXT_LENGTH) {
        auto* cell_data = this->alloc(1 + sf.length());
        cell_data[0] = combine_type_value(cell_type::CT_TEXT, sf.length());
        memcpy(&cell_data[1], sf.udata(), sf.length());
    } else {
        auto* cell_data = this->alloc(1 + 4 + sf.length());
        auto len = sf.length();
        cell_data[0]
            = combine_type_value(cell_type::CT_TEXT, SHORT_TEXT_LENGTH);
        cell_data[1] = len & 0xff;
        cell_data[2] = (len >> 8) & 0xff;
        cell_data[3] = (len >> 16) & 0xff;
        cell_data[4] = (len >> 24) & 0xff;
        memcpy(&cell_data[5], sf.udata(), sf.length());
    }
}

std::optional<cell_container::cursor>
cell_container::cursor::sync() const
{
    const auto* cc = this->c_chunk;
    auto next_offset = this->c_offset;

    if (next_offset < cc->cc_size) {
        this->c_chunk->cc_parent->load_chunk_into_cache(cc);
        return *this;
    }

    cc = cc->cc_next.get();
    if (cc == nullptr) {
        return std::nullopt;
    }

    this->c_chunk->cc_parent->load_chunk_into_cache(cc);
    return cursor{cc, size_t{0}};
}

std::optional<cell_container::cursor>
cell_container::cursor::next() const
{
    size_t advance = 0;

    switch (this->get_type()) {
        case cell_type::CT_NULL:
            advance = 1;
            break;
        case cell_type::CT_TEXT: {
            auto len = this->get_text_length();
            advance = 1 + len;
            if (len >= SHORT_TEXT_LENGTH) {
                advance += 4;
            }
            break;
        }
        case cell_type::CT_INTEGER:
        case cell_type::CT_FLOAT:
            advance = 1 + 8 + this->get_sub_value();
            break;
    }

    const auto* cc = this->c_chunk;
    auto next_offset = this->c_offset + advance;
    if (this->c_offset + advance >= this->c_chunk->cc_size) {
        cc = this->c_chunk->cc_next.get();
        if (cc != nullptr) {
            this->c_chunk->cc_parent->load_chunk_into_cache(cc);
        }
        next_offset = 0;
#if 0
        log_debug("advanced past current chunk %p, going to %p %lu",
                  this->c_chunk,
                  cc,
                  next_offset);
#endif
    } else {
        // log_debug("good chunk %p %d", cc, advance);
    }
    if (cc == nullptr) {
        return std::nullopt;
    }

    if (cc->cc_data[next_offset] == 0) {
        if (cc->cc_next.get() == nullptr) {
            // log_debug("end of the line");
            return std::nullopt;
        }

        cc = cc->cc_next.get();
        next_offset = 0;
    }

    this->c_chunk->cc_parent->load_chunk_into_cache(cc);
    return cursor{cc, next_offset};
}

uint8_t
cell_container::cursor::get_type() const
{
    // log_debug("get_type %d", this->udata()[0] & 0x03);
    return this->udata()[0] & 0x03;
}

uint8_t
cell_container::cursor::get_sub_value() const
{
    return this->udata()[0] >> 2;
}

const unsigned char*
cell_container::cursor::udata() const
{
    return &this->c_chunk->cc_data[this->c_offset];
}

string_fragment
cell_container::cursor::get_text() const
{
    auto sub = this->get_sub_value();
    auto len = this->get_text_length();

    if (sub < SHORT_TEXT_LENGTH) {
        return string_fragment::from_bytes(&this->udata()[1], len);
    }

    return string_fragment::from_bytes(&this->udata()[5], len);
}

int64_t
cell_container::cursor::get_int() const
{
    int64_t retval = 0;

    auto* retval_bits = reinterpret_cast<uint8_t*>(&retval);
    retval_bits[0] = this->udata()[1];
    retval_bits[1] = this->udata()[2];
    retval_bits[2] = this->udata()[3];
    retval_bits[3] = this->udata()[4];
    retval_bits[4] = this->udata()[5];
    retval_bits[5] = this->udata()[6];
    retval_bits[6] = this->udata()[7];
    retval_bits[7] = this->udata()[8];

    return retval;
}

double
cell_container::cursor::get_float() const
{
    double retval = 0;

    auto* retval_bits = reinterpret_cast<uint8_t*>(&retval);
    retval_bits[0] = this->udata()[1];
    retval_bits[1] = this->udata()[2];
    retval_bits[2] = this->udata()[3];
    retval_bits[3] = this->udata()[4];
    retval_bits[4] = this->udata()[5];
    retval_bits[5] = this->udata()[6];
    retval_bits[6] = this->udata()[7];
    retval_bits[7] = this->udata()[8];

    return retval;
}

string_fragment
cell_container::cursor::get_float_as_text() const
{
    const auto* cell_data = this->udata();
    const auto sub = this->get_sub_value();

    require(sub > 0);

    return string_fragment::from_bytes(&cell_data[9], sub);
}

size_t
cell_container::cursor::get_text_length() const
{
    auto sub = this->get_sub_value();
    if (sub < SHORT_TEXT_LENGTH) {
        return sub;
    }

    size_t retval = this->c_chunk->cc_data[this->c_offset + 1]
        | (this->c_chunk->cc_data[this->c_offset + 2] << 8)
        | (this->c_chunk->cc_data[this->c_offset + 3] << 16)
        | (this->c_chunk->cc_data[this->c_offset + 4] << 24);

    return retval;
}

}  // namespace lnav
