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

#ifndef lnav_cell_container_hh
#define lnav_cell_container_hh

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "auto_mem.hh"
#include "intern_string.hh"
#include "lnav_log.hh"

namespace lnav {

namespace cell_type {

constexpr uint8_t CT_NULL = 0;
constexpr uint8_t CT_INTEGER = 1;
constexpr uint8_t CT_FLOAT = 2;
constexpr uint8_t CT_TEXT = 3;

}  // namespace cell_type

struct cell_container;

struct cell_chunk {
    cell_chunk(cell_container* parent,
               std::unique_ptr<unsigned char[]> data,
               size_t capacity);

    size_t available() const { return this->cc_capacity - this->cc_size; }

    unsigned char* alloc(size_t amount)
    {
        auto* retval = &this->cc_data[this->cc_size];

        this->cc_size += amount;
        return retval;
    }

    void reset();

    void load() const;

    /** Drops the uncompressed data, unless the looper still needs it. */
    void evict() const;

    cell_container* cc_parent;
    std::unique_ptr<cell_chunk> cc_next;
    mutable std::unique_ptr<unsigned char[]> cc_data;
    const size_t cc_capacity;
    size_t cc_size{0};
    std::unique_ptr<const unsigned char[]> cc_compressed;
    size_t cc_compressed_size{0};
    /**
     * Full and handed to the compression looper, whose result hasn't been
     * collected, so cc_data has to stay.  Only touched by the thread that
     * owns the container.
     */
    bool cc_pending{false};
};

struct cell_container {
    cell_container();
    ~cell_container();

    static constexpr uint8_t SHORT_TEXT_LENGTH = 0xff >> 2;

    void push_null_cell();
    void push_int_cell(int64_t i);
    void push_float_cell(double f);
    void push_float_with_units_cell(double actual,
                                    const string_fragment& as_str);
    void push_text_cell(const string_fragment& sf);

    struct cursor {
        const cell_chunk* c_chunk;
        size_t c_offset;

        cursor(const cell_chunk* chunk, size_t offset)
            : c_chunk(chunk), c_offset(offset)
        {
        }

        const unsigned char* udata() const;

        std::optional<cursor> sync() const;

        std::optional<cursor> next() const;

        uint8_t get_type() const;
        uint8_t get_sub_value() const;
        size_t get_text_length() const;
        string_fragment get_text() const;
        string_fragment get_float_as_text() const;
        int64_t get_int() const;
        double get_float() const;

        template<typename A>
        string_fragment to_string_fragment(A allocator) const
        {
            switch (this->get_type()) {
                case cell_type::CT_NULL:
                    return string_fragment::from_const("<NULL>");
                case cell_type::CT_INTEGER: {
                    constexpr size_t buf_size = 32;
                    auto* bits = allocator.allocate(buf_size);
                    auto fmt_res = fmt::format_to_n(
                        bits, buf_size, FMT_STRING("{}"), this->get_int());
                    return string_fragment::from_bytes(bits, fmt_res.size);
                }
                case cell_type::CT_FLOAT: {
                    auto as_str_len = this->get_sub_value();
                    if (as_str_len > 0) {
                        return this->get_float_as_text();
                    }
                    constexpr size_t buf_size = 32;
                    auto* bits = allocator.allocate(buf_size);
                    auto fmt_res = fmt::format_to_n(
                        bits, buf_size, FMT_STRING("{}"), this->get_float());
                    return string_fragment::from_bytes(bits, fmt_res.size);
                }
                case cell_type::CT_TEXT: {
                    return this->get_text();
                }
                default:
                    ensure(false);
            }
        }
    };

    cursor end_cursor() const
    {
        return cursor{this->cc_last, this->cc_last->cc_size};
    }

    unsigned char* alloc(size_t amount);

    void load_chunk_into_cache(const cell_chunk* cc);

    void reset();

    std::unique_ptr<cell_chunk> cc_first;
    cell_chunk* cc_last;

    static constexpr size_t CHUNK_CACHE_SIZE = 3;
    std::array<const cell_chunk*, CHUNK_CACHE_SIZE> cc_chunk_cache;

    /**
     * Buffers no chunk is using, kept for new chunks and for chunks that
     * are decompressed.
     */
    std::vector<std::unique_ptr<unsigned char[]>> cc_spare_buffers;

    /**
     * The jobs this container sent to the compression looper and their
     * results.  Shared with the jobs, so a job that runs after the
     * container is gone finds its generation is stale and does nothing.
     */
    struct compress_state;
    std::shared_ptr<compress_state> cc_compress_state;

    std::unique_ptr<unsigned char[]> take_buffer(size_t capacity);

    void give_buffer(std::unique_ptr<unsigned char[]> buf, size_t capacity);

private:
    bool in_cache(const cell_chunk* cc) const;

    /**
     * Hands a full chunk to the compression looper, or compresses it here
     * when the looper isn't running.
     */
    void compress_chunk(cell_chunk* cc);

    /** Applies the looper's finished chunks and frees their data. */
    void collect_compressed();

    /**
     * Makes the queued jobs do nothing and waits for the ones reading a
     * chunk.
     */
    void cancel_compression();
};

}  // namespace lnav

#endif
