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
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "cell_container.hh"

#include <zlib.h>

#include "auto_mem.hh"
#include "injector.bind.hh"
#include "isc.hh"

/** Compresses the containers' full chunks off the thread that fills them. */
class compression_looper : public isc::service<compression_looper, 2> {
protected:
    std::optional<std::chrono::milliseconds> compute_timeout(
        mstime_t current_time) const override
    {
        return std::nullopt;
    }
};

struct compression_looper_tag {};

static auto bound_compression
    = injector::bind_multiple<isc::service_base>()
          .add_singleton<compression_looper, compression_looper_tag>();

namespace injector {
template<>
void
force_linking(compression_looper_tag anno)
{
}
}  // namespace injector

namespace lnav {

static constexpr auto DEFAULT_CHUNK_SIZE = size_t{32 * 1024};
static constexpr auto NULL_CELL_SUB = 0x1;
static constexpr auto COMPRESSION_LEVEL = 2;
/**
 * How many of a container's full chunks can wait for the compression looper
 * before a push waits for it, which bounds the uncompressed data held for a
 * large result.
 */
static constexpr size_t MAX_CHUNKS_IN_FLIGHT = 8;
static constexpr size_t MAX_SPARE_BUFFERS = 4;

namespace {

struct compressed_data {
    std::unique_ptr<const unsigned char[]> cd_data;
    size_t cd_size;
};

/** A deflate stream that's reset for each chunk instead of set up again. */
struct deflater {
    z_stream d_strm{};
    auto_buffer d_buf{auto_buffer::alloc(compressBound(DEFAULT_CHUNK_SIZE))};

    deflater()
    {
        auto rc = deflateInit(&this->d_strm, COMPRESSION_LEVEL);
        require(rc == Z_OK);
    }

    deflater(const deflater&) = delete;
    deflater& operator=(const deflater&) = delete;

    ~deflater() { deflateEnd(&this->d_strm); }

    compressed_data compress(const unsigned char* data, size_t size)
    {
        auto buflen = compressBound(size);
        this->d_buf.expand_to(buflen);
        auto rc = deflateReset(&this->d_strm);
        require(rc == Z_OK);
        this->d_strm.next_in = const_cast<unsigned char*>(data);
        this->d_strm.avail_in = size;
        this->d_strm.next_out = this->d_buf.u_in();
        this->d_strm.avail_out = buflen;
        rc = deflate(&this->d_strm, Z_FINISH);
        require(rc == Z_STREAM_END);
        this->d_buf.resize(this->d_strm.total_out);

        return {this->d_buf.to_unique(), this->d_strm.total_out};
    }
};

compressed_data
compress_data(const unsigned char* data, size_t size)
{
    thread_local deflater DEFLATER;

    return DEFLATER.compress(data, size);
}

}  // namespace

static uint8_t
combine_type_value(uint8_t type, uint8_t subvalue)
{
    return type | subvalue << 2U;
}

struct cell_container::compress_state {
    struct result {
        cell_chunk* r_chunk;
        compressed_data r_data;
    };

    std::mutex cs_mutex;
    std::condition_variable cs_cond;
    /** Bumped by a cancel, so the jobs queued before it do nothing. */
    uint64_t cs_generation{0};
    /** The current generation's jobs that are queued or running. */
    size_t cs_in_flight{0};
    /** The jobs reading a chunk, of any generation. */
    size_t cs_running{0};
    std::vector<result> cs_results;
    /** Set with cs_results, so a check for them needn't take the lock. */
    std::atomic<bool> cs_have_results{false};
};

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
        this->cc_data = this->cc_parent->take_buffer(this->cc_capacity);
    }
    this->cc_compressed.reset();
    this->cc_pending = false;
}

void
cell_chunk::load() const
{
    if (this->cc_data != nullptr) {
        return;
    }

    this->cc_data = this->cc_parent->take_buffer(this->cc_capacity);
    uLongf cap = this->cc_capacity;
    auto rc = uncompress(this->cc_data.get(),
                         &cap,
                         this->cc_compressed.get(),
                         this->cc_compressed_size);
    ensure(rc == Z_OK);
}

void
cell_chunk::evict() const
{
    if (!this->cc_pending && this->cc_data != nullptr) {
        this->cc_parent->give_buffer(std::move(this->cc_data),
                                     this->cc_capacity);
    }
}

cell_container::cell_container()
    : cc_first(std::make_unique<cell_chunk>(
          this,
          std::make_unique<unsigned char[]>(DEFAULT_CHUNK_SIZE),
          DEFAULT_CHUNK_SIZE)),
      cc_last(cc_first.get()), cc_chunk_cache({})
{
    // log_debug("cell container %p  chunk %p", this, this->cc_last);
}

cell_container::~cell_container()
{
    this->cancel_compression();
}

std::unique_ptr<unsigned char[]>
cell_container::take_buffer(size_t capacity)
{
    if (capacity == DEFAULT_CHUNK_SIZE && !this->cc_spare_buffers.empty()) {
        auto retval = std::move(this->cc_spare_buffers.back());
        this->cc_spare_buffers.pop_back();
        return retval;
    }

    return std::make_unique<unsigned char[]>(capacity);
}

void
cell_container::give_buffer(std::unique_ptr<unsigned char[]> buf,
                            size_t capacity)
{
    if (capacity == DEFAULT_CHUNK_SIZE
        && this->cc_spare_buffers.size() < MAX_SPARE_BUFFERS)
    {
        this->cc_spare_buffers.emplace_back(std::move(buf));
    }
}

bool
cell_container::in_cache(const cell_chunk* cc) const
{
    return std::find(
               this->cc_chunk_cache.begin(), this->cc_chunk_cache.end(), cc)
        != this->cc_chunk_cache.end();
}

void
cell_container::compress_chunk(cell_chunk* cc)
{
    auto& looper = injector::get<compression_looper&, compression_looper_tag>();
    if (!looper.is_started() || !looper.is_looping()) {
        auto res = compress_data(cc->cc_data.get(), cc->cc_size);
        cc->cc_compressed = std::move(res.cd_data);
        cc->cc_compressed_size = res.cd_size;
        this->give_buffer(std::move(cc->cc_data), cc->cc_capacity);
        return;
    }

    if (this->cc_compress_state == nullptr) {
        this->cc_compress_state = std::make_shared<compress_state>();
    }
    auto& state = *this->cc_compress_state;
    uint64_t gen;
    {
        std::unique_lock<std::mutex> lk(state.cs_mutex);
        // The looper is only stopped at exit, but don't wait on one that is.
        while (state.cs_in_flight >= MAX_CHUNKS_IN_FLIGHT
               && looper.is_looping())
        {
            state.cs_cond.wait_for(lk, std::chrono::milliseconds{100});
        }
        state.cs_in_flight += 1;
        gen = state.cs_generation;
    }
    cc->cc_pending = true;

    looper.send([state_ptr = this->cc_compress_state,
                 cc,
                 data = static_cast<const unsigned char*>(cc->cc_data.get()),
                 size = cc->cc_size,
                 gen](auto& looper) {
        auto& state = *state_ptr;
        {
            std::lock_guard<std::mutex> lk(state.cs_mutex);
            if (state.cs_generation != gen) {
                return;
            }
            state.cs_running += 1;
        }

        auto res = compress_data(data, size);

        std::lock_guard<std::mutex> lk(state.cs_mutex);
        state.cs_running -= 1;
        if (state.cs_generation == gen) {
            state.cs_results.emplace_back(
                compress_state::result{cc, std::move(res)});
            state.cs_in_flight -= 1;
            state.cs_have_results.store(true, std::memory_order_release);
        }
        state.cs_cond.notify_all();
    });
}

void
cell_container::collect_compressed()
{
    if (this->cc_compress_state == nullptr
        || !this->cc_compress_state->cs_have_results.load(
            std::memory_order_acquire))
    {
        return;
    }

    std::vector<compress_state::result> results;
    {
        std::lock_guard<std::mutex> lk(this->cc_compress_state->cs_mutex);
        results.swap(this->cc_compress_state->cs_results);
        this->cc_compress_state->cs_have_results.store(
            false, std::memory_order_relaxed);
    }
    for (auto& res : results) {
        auto* cc = res.r_chunk;
        cc->cc_compressed = std::move(res.r_data.cd_data);
        cc->cc_compressed_size = res.r_data.cd_size;
        cc->cc_pending = false;
        // A chunk that was read while it waited is in the cache, and what
        // was read from it stays valid until the cache evicts it.
        if (!this->in_cache(cc)) {
            this->give_buffer(std::move(cc->cc_data), cc->cc_capacity);
        }
    }
}

void
cell_container::cancel_compression()
{
    if (this->cc_compress_state == nullptr) {
        return;
    }

    auto& state = *this->cc_compress_state;
    std::unique_lock<std::mutex> lk(state.cs_mutex);
    state.cs_generation += 1;
    state.cs_in_flight = 0;
    state.cs_results.clear();
    state.cs_have_results.store(false, std::memory_order_relaxed);
    state.cs_cond.wait(lk, [&state]() { return state.cs_running == 0; });
}

unsigned char*
cell_container::alloc(size_t amount)
{
    if (this->cc_last->available() < amount) {
        auto last = this->cc_last;

        this->compress_chunk(last);
        // After the wait in compress_chunk(), so the chunks that finished
        // during it free their data before the new chunk takes a buffer.
        this->collect_compressed();

        auto chunk_size = std::max(amount, DEFAULT_CHUNK_SIZE);
        last->cc_next = std::make_unique<cell_chunk>(
            this, this->take_buffer(chunk_size), chunk_size);
        this->cc_last = last->cc_next.get();
        // log_debug("allocating chunk with %lu %p", chunk_size, this->cc_last);

        ensure(this->cc_last->cc_capacity >= chunk_size);
    }

    return this->cc_last->alloc(amount);
}

void
cell_container::load_chunk_into_cache(const cell_chunk* cc)
{
    this->collect_compressed();
    if (cc->cc_data != nullptr) {
        // A chunk waiting for the looper still has its data, but goes in
        // the cache like a loaded one so that its data stays until it's
        // evicted.
        if (!cc->cc_pending || this->in_cache(cc)) {
            return;
        }
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
    this->cancel_compression();
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
    // Pick the narrowest little-endian two's-complement width that
    // can hold `i`, and stash that width in the tag's sub-value.
    // Common log columns (status codes, counts, log_line) fit in 1–4
    // bytes, saving 4–7 bytes per cell versus a fixed 8-byte payload.
    uint8_t width;
    if (i >= INT8_MIN && i <= INT8_MAX) {
        width = 1;
    } else if (i >= INT16_MIN && i <= INT16_MAX) {
        width = 2;
    } else if (i >= INT32_MIN && i <= INT32_MAX) {
        width = 4;
    } else {
        width = 8;
    }

    auto* cell_data = this->alloc(1 + width);
    cell_data[0] = combine_type_value(cell_type::CT_INTEGER, width);
    const auto* i_bits = reinterpret_cast<const uint8_t*>(&i);
    for (uint8_t b = 0; b < width; ++b) {
        cell_data[1 + b] = i_bits[b];
    }
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
            // sub_value carries the payload width (1, 2, 4, or 8).
            advance = 1 + this->get_sub_value();
            break;
        case cell_type::CT_FLOAT:
            // sub_value carries the optional units-string length; the
            // 8-byte IEEE 754 payload is always present.
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
    const auto width = this->get_sub_value();
    const auto* bytes = &this->udata()[1];

    int64_t retval = 0;
    auto* retval_bits = reinterpret_cast<uint8_t*>(&retval);
    for (uint8_t b = 0; b < width; ++b) {
        retval_bits[b] = bytes[b];
    }
    // Sign-extend when the stored value is narrower than int64_t.
    if (width < 8 && (retval_bits[width - 1] & 0x80)) {
        for (uint8_t b = width; b < 8; ++b) {
            retval_bits[b] = 0xff;
        }
    }
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

    size_t retval
        = static_cast<size_t>(this->c_chunk->cc_data[this->c_offset + 1])
        | (static_cast<size_t>(this->c_chunk->cc_data[this->c_offset + 2]) << 8)
        | (static_cast<size_t>(this->c_chunk->cc_data[this->c_offset + 3])
           << 16)
        | (static_cast<size_t>(this->c_chunk->cc_data[this->c_offset + 4])
           << 24);

    return retval;
}

}  // namespace lnav
