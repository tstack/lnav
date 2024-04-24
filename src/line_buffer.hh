/**
 * Copyright (c) 2007-2012, Timothy Stack
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
 * @file line_buffer.hh
 */

#ifndef line_buffer_hh
#define line_buffer_hh

#include <array>
#include <exception>
#include <future>
#include <vector>

#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include "base/auto_fd.hh"
#include "base/auto_mem.hh"
#include "base/file_range.hh"
#include "base/is_utf8.hh"
#include "base/lnav.gzip.hh"
#include "base/piper.file.hh"
#include "base/result.h"
#include "log_level.hh"
#include "mapbox/variant.hpp"
#include "safe/safe.h"
#include "shared_buffer.hh"

struct line_info {
    file_range li_file_range;
    struct timeval li_timestamp {
        0, 0
    };
    log_level_t li_level{LEVEL_UNKNOWN};
    bool li_partial{false};
    utf8_scan_result li_utf8_scan_result{};
};

/**
 * Buffer for reading whole lines out of file descriptors.  The class presents
 * a stateless interface, callers specify the offset where a line starts and
 * the class takes care of caching the surrounding range and locating the
 * delimiter.
 *
 * XXX A bit of a wheel reinvention, but I'm not sure how well the libraries
 * handle non-blocking I/O...
 */
class line_buffer {
public:
    static const ssize_t DEFAULT_LINE_BUFFER_SIZE;
    static const ssize_t MAX_LINE_BUFFER_SIZE;
    class error : public std::exception {
    public:
        explicit error(int err) : e_err(err) {}

        int e_err;
    };

#define GZ_WINSIZE           32768U /*> gzip's max supported dictionary is 15-bits */
#define GZ_RAW_MODE          (-15) /*> Raw inflate data mode */
#define GZ_HEADER_MODE       (15 + 32) /*> Automatic zstd or gzip decoding */
#define GZ_BORROW_BITS_MASK  7 /*> Bits (0-7) consumed in previous block */
#define GZ_END_OF_BLOCK_MASK 128 /*> Stopped because reached end-of-block */
#define GZ_END_OF_FILE_MASK  64 /*> Stopped because reached end-of-file */

    /**
     * A memoized gzip file reader that can do random file access faster than
     * gzseek/gzread alone.
     */
    class gz_indexed {
    public:
        gz_indexed();
        gz_indexed(gz_indexed&& other) = default;
        ~gz_indexed() { this->close(); }

        inline operator bool() const { return this->gz_fd != -1; }

        uLong get_source_offset() const
        {
            return !!*this ? this->strm.total_in + this->strm.avail_in : 0;
        }

        void close();
        void init_stream();
        void continue_stream();
        void open(int fd, lnav::gzip::header& hd);
        int stream_data(void* buf, size_t size);
        void seek(off_t offset);

        /**
         * Decompress bytes from the gz file returning at most `size` bytes.
         * offset is the byte-offset in the decompressed data stream.
         */
        int read(void* buf, size_t offset, size_t size);

        struct indexDict {
            off_t in = 0;
            off_t out = 0;
            unsigned char bits = 0;
            unsigned char in_bits = 0;
            Bytef index[GZ_WINSIZE];
            indexDict(z_stream const& s, const file_size_t size);

            int apply(z_streamp s);
        };

    private:
        z_stream strm; /*< gzip streams structure */
        std::vector<indexDict>
            syncpoints; /*< indexed dictionaries as discovered */
        auto_mem<Bytef> inbuf; /*< Compressed data buffer */
        int gz_fd = -1; /*< The file to read data from. */
    };

    /** Construct an empty line_buffer. */
    line_buffer();

    line_buffer(line_buffer&& other) = delete;

    virtual ~line_buffer();

    /** @param fd The file descriptor that data should be pulled from. */
    void set_fd(auto_fd& fd);

    /** @return The file descriptor that data should be pulled from. */
    int get_fd() const { return this->lb_fd; }

    time_t get_file_time() const { return this->lb_file_time; }

    /**
     * @return The size of the file or the amount of data pulled from a pipe.
     */
    file_ssize_t get_file_size() const { return this->lb_file_size; }

    bool is_pipe() const { return !this->lb_seekable; }

    bool is_pipe_closed() const
    {
        return !this->lb_seekable && (this->lb_file_size != -1);
    }

    bool is_compressed() const { return this->lb_compressed; }

    bool is_header_utf8() const { return this->lb_is_utf8; }

    bool has_line_metadata() const { return this->lb_line_metadata; }

    file_off_t get_read_offset(file_off_t off) const
    {
        if (this->is_compressed()) {
            return this->lb_compressed_offset;
        }
        return off;
    }

    bool is_data_available(file_off_t off, file_off_t stat_size) const
    {
        if (this->is_compressed()) {
            return (this->lb_file_size == -1 || off < this->lb_file_size);
        }
        return off < stat_size;
    }

    /**
     * Attempt to load the next line into the buffer.
     *
     * @param prev_line The range of the previous line.
     * @return If the read was successful, information about the line.
     *   Otherwise, an error message.
     */
    Result<line_info, std::string> load_next_line(file_range prev_line = {});

    Result<shared_buffer_ref, std::string> read_range(file_range fr);

    file_range get_available();

    bool is_likely_to_flush(file_range prev_line);

    void flush_at(file_off_t off)
    {
        if (this->in_range(off)) {
            this->lb_buffer.resize(off - this->lb_file_offset);
        } else {
            this->lb_buffer.clear();
        }
    }

    /** Release any resources held by this object. */
    void reset()
    {
        this->lb_fd.reset();

        this->lb_file_offset = 0;
        this->lb_file_size = (ssize_t) -1;
        this->lb_buffer.resize(0);
        this->lb_last_line_offset = -1;
    }

    /** Check the invariants for this object. */
    bool invariant() const
    {
        require(this->lb_buffer.size() <= this->lb_buffer.capacity());

        return true;
    }

    void quiesce();

    struct stats {
        bool empty() const
        {
            return this->s_decompressions == 0 && this->s_preads == 0
                && this->s_requested_preloads == 0
                && this->s_used_preloads == 0;
        }

        uint32_t s_decompressions{0};
        uint32_t s_preads{0};
        uint32_t s_requested_preloads{0};
        uint32_t s_used_preloads{0};
        std::array<uint32_t, 10> s_hist{};
    };

    struct stats consume_stats() { return std::exchange(this->lb_stats, {}); }

    size_t get_buffer_size() const { return this->lb_buffer.size(); }

    using file_header_t
        = mapbox::util::variant<lnav::gzip::header, lnav::piper::header>;

    const file_header_t& get_header_data() const { return this->lb_header; }

    void enable_cache();

    file_ssize_t get_piper_header_size() const
    {
        return this->lb_piper_header_size;
    }

    bool is_piper() const { return this->lb_piper_header_size > 0; }

    size_t line_count_guess() const { return this->lb_line_starts.size(); }

    static void cleanup_cache();

private:
    /**
     * @param off The file offset to check for in the buffer.
     * @return True if the given offset is cached in the buffer.
     */
    bool in_range(file_off_t off) const
    {
        return this->lb_file_offset <= off
            && off
            < (this->lb_file_offset + (file_ssize_t) this->lb_buffer.size());
    }

    void resize_buffer(size_t new_max);

    /**
     * Ensure there is enough room in the buffer to cache a range of data from
     * the file.  First, this method will check to see if there is enough room
     * from where 'start' begins in the buffer to the maximum buffer size.  If
     * this is not enough, the currently cached data at 'start' will be moved
     * to the beginning of the buffer, overwriting any cached data earlier in
     * the file.  Finally, if this is still not enough, the buffer will be
     * reallocated to make more room.
     *
     * @param start The file offset of the start of the line.
     * @param max_length The amount of data to be cached in the buffer.
     */
    void ensure_available(file_off_t start, ssize_t max_length);

    /**
     * Fill the buffer with the given range of data from the file.
     *
     * @param start The file offset where data should start to be read from the
     * file.
     * @param max_length The maximum amount of data to read from the file.
     * @return True if any data was read from the file.
     */
    bool fill_range(file_off_t start, ssize_t max_length);

    /**
     * After a successful fill, the cached data can be retrieved with this
     * method.
     *
     * @param start The file offset to retrieve cached data for.
     * @param avail_out On return, the amount of data currently cached at the
     * given offset.
     * @return A pointer to the start of the cached data in the internal
     * buffer.
     */
    const char* get_range(file_off_t start, file_ssize_t& avail_out) const
    {
        size_t buffer_offset = start - this->lb_file_offset;

        require(buffer_offset >= 0);
        require(this->lb_buffer.size() >= buffer_offset);

        const auto* retval = this->lb_buffer.at(buffer_offset);
        avail_out = this->lb_buffer.size() - buffer_offset;

        return retval;
    }

    bool load_next_buffer();

    using safe_gz_indexed = safe::Safe<gz_indexed>;

    shared_buffer lb_share_manager;

    auto_fd lb_fd; /*< The file to read data from. */
    safe_gz_indexed lb_gz_file; /*< File reader for gzipped files. */
    bool lb_bz_file{false}; /*< Flag set for bzip2 compressed files. */
    bool lb_line_metadata{false};
    file_ssize_t lb_piper_header_size{0};

    auto_buffer lb_buffer{auto_buffer::alloc(DEFAULT_LINE_BUFFER_SIZE)};
    nonstd::optional<auto_buffer> lb_alt_buffer;
    std::vector<uint32_t> lb_alt_line_starts;
    std::vector<bool> lb_alt_line_is_utf;
    std::vector<bool> lb_alt_line_has_ansi;
    std::future<bool> lb_loader_future;
    nonstd::optional<file_off_t> lb_loader_file_offset;

    file_off_t lb_compressed_offset{
        0}; /*< The offset into the compressed file. */
    file_ssize_t lb_file_size{
        -1}; /*<
              * The size of the file.  When lb_fd refers to
              * a pipe, this is set to the amount of data
              * read from the pipe when EOF is reached.
              */
    file_off_t lb_file_offset{0}; /*<
                                   * Data cached in the buffer comes from this
                                   * offset in the file.
                                   */
    time_t lb_file_time{0};
    bool lb_seekable{false}; /*< Flag set for seekable file descriptors. */
    bool lb_compressed{false};
    bool lb_is_utf8{true};
    file_off_t lb_last_line_offset{-1}; /*< */

    std::vector<uint32_t> lb_line_starts;
    std::vector<bool> lb_line_is_utf;
    std::vector<bool> lb_line_has_ansi;
    stats lb_stats;

    nonstd::optional<auto_fd> lb_cached_fd;

    file_header_t lb_header{mapbox::util::no_init{}};
};

#endif
