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
 * @file line_buffer.cc
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

#ifdef HAVE_BZLIB_H
#    include <bzlib.h>
#endif

#include <algorithm>
#include <set>

#ifdef HAVE_X86INTRIN_H
#    include "simdutf8check.h"
#endif

#include "base/auto_pid.hh"
#include "base/fs_util.hh"
#include "base/injector.bind.hh"
#include "base/injector.hh"
#include "base/is_utf8.hh"
#include "base/isc.hh"
#include "base/math_util.hh"
#include "base/paths.hh"
#include "fmtlib/fmt/format.h"
#include "hasher.hh"
#include "line_buffer.hh"
#include "piper.looper.hh"
#include "scn/scn.h"

using namespace std::chrono_literals;

static const ssize_t INITIAL_REQUEST_SIZE = 16 * 1024;
static const ssize_t DEFAULT_INCREMENT = 128 * 1024;
static const ssize_t INITIAL_COMPRESSED_BUFFER_SIZE = 5 * 1024 * 1024;
static const ssize_t MAX_COMPRESSED_BUFFER_SIZE = 32 * 1024 * 1024;

const ssize_t line_buffer::DEFAULT_LINE_BUFFER_SIZE = 256 * 1024;
const ssize_t line_buffer::MAX_LINE_BUFFER_SIZE
    = 4 * 4 * line_buffer::DEFAULT_LINE_BUFFER_SIZE;

class io_looper : public isc::service<io_looper> {};

struct io_looper_tag {};

static auto bound_io = injector::bind_multiple<isc::service_base>()
                           .add_singleton<io_looper, io_looper_tag>();

namespace injector {
template<>
void
force_linking(io_looper_tag anno)
{
}
}  // namespace injector

/*
 * XXX REMOVE ME
 *
 * The stock bzipped file code does not use pread, so we need to use a lock to
 * get exclusive access to the file.  In the future, we should just rewrite
 * the bzipped file code to use pread.
 */
class lock_hack {
public:
    class guard {
    public:
        guard() : g_lock(lock_hack::singleton()) { this->g_lock.lock(); }

        ~guard() { this->g_lock.unlock(); }

    private:
        lock_hack& g_lock;
    };

    static lock_hack& singleton()
    {
        static lock_hack retval;

        return retval;
    }

    void lock() { lockf(this->lh_fd, F_LOCK, 0); }

    void unlock() { lockf(this->lh_fd, F_ULOCK, 0); }

private:
    lock_hack()
    {
        char lockname[64];

        snprintf(lockname, sizeof(lockname), "/tmp/lnav.%d.lck", getpid());
        this->lh_fd = open(lockname, O_CREAT | O_RDWR, 0600);
        log_perror(fcntl(this->lh_fd, F_SETFD, FD_CLOEXEC));
        unlink(lockname);
    }

    auto_fd lh_fd;
};
/* XXX END */

#define Z_BUFSIZE      65536U
#define SYNCPOINT_SIZE (1024 * 1024)
line_buffer::gz_indexed::
gz_indexed()
{
    if ((this->inbuf = auto_mem<Bytef>::malloc(Z_BUFSIZE)) == nullptr) {
        throw std::bad_alloc();
    }
}

void
line_buffer::gz_indexed::close()
{
    // Release old stream, if we were open
    if (*this) {
        inflateEnd(&this->strm);
        ::close(this->gz_fd);
        this->syncpoints.clear();
        this->gz_fd = -1;
    }
}

void
line_buffer::gz_indexed::init_stream()
{
    if (*this) {
        inflateEnd(&this->strm);
    }

    // initialize inflate struct
    int rc = inflateInit2(&this->strm, GZ_HEADER_MODE);
    this->strm.avail_in = 0;
    if (rc != Z_OK) {
        throw(rc);  // FIXME: exception wrapper
    }
}

void
line_buffer::gz_indexed::continue_stream()
{
    // Save our position and output buffer
    auto total_in = this->strm.total_in;
    auto total_out = this->strm.total_out;
    auto avail_out = this->strm.avail_out;
    auto next_out = this->strm.next_out;

    init_stream();

    // Restore position and output buffer
    this->strm.total_in = total_in;
    this->strm.total_out = total_out;
    this->strm.avail_out = avail_out;
    this->strm.next_out = next_out;
}

void
line_buffer::gz_indexed::open(int fd, lnav::gzip::header& hd)
{
    this->close();
    this->init_stream();
    this->gz_fd = fd;

    unsigned char name[1024];
    unsigned char comment[4096];

    name[0] = '\0';
    comment[0] = '\0';

    gz_header gz_hd;
    memset(&gz_hd, 0, sizeof(gz_hd));
    gz_hd.name = name;
    gz_hd.name_max = sizeof(name);
    gz_hd.comment = comment;
    gz_hd.comm_max = sizeof(comment);

    Bytef inbuf[8192];
    Bytef outbuf[8192];
    this->strm.next_out = outbuf;
    this->strm.total_out = 0;
    this->strm.avail_out = sizeof(outbuf);
    this->strm.next_in = inbuf;
    this->strm.total_in = 0;

    if (inflateGetHeader(&this->strm, &gz_hd) == Z_OK) {
        auto rc = pread(fd, inbuf, sizeof(inbuf), 0);
        if (rc >= 0) {
            this->strm.avail_in = rc;

            inflate(&this->strm, Z_BLOCK);
            inflateEnd(&this->strm);
            this->strm.next_out = Z_NULL;
            this->strm.next_in = Z_NULL;
            this->strm.next_in = Z_NULL;
            this->strm.total_in = 0;
            this->strm.avail_in = 0;
            this->init_stream();

            switch (gz_hd.done) {
                case 0:
                    log_debug("%d: no gzip header data", fd);
                    break;
                case 1:
                    hd.h_mtime.tv_sec = gz_hd.time;
                    hd.h_name = std::string((char*) name);
                    hd.h_comment = std::string((char*) comment);
                    log_info(
                        "%d: read gzip header (mtime=%d; name='%s'; "
                        "comment='%s'; crc=%x)",
                        fd,
                        hd.h_mtime.tv_sec,
                        hd.h_name.c_str(),
                        hd.h_comment.c_str(),
                        gz_hd.hcrc);
                    break;
                default:
                    log_error("%d: failed to read gzip header data", fd);
                    break;
            }
        } else {
            log_error("%d: failed to read gzip header from file: %s",
                      fd,
                      strerror(errno));
        }
    } else {
        log_error("%d: unable to get gzip header", fd);
    }
}

int
line_buffer::gz_indexed::stream_data(void* buf, size_t size)
{
    this->strm.avail_out = size;
    this->strm.next_out = (unsigned char*) buf;

    size_t last = this->syncpoints.empty() ? 0 : this->syncpoints.back().in;
    while (this->strm.avail_out) {
        if (!this->strm.avail_in) {
            int rc = ::pread(
                this->gz_fd, &this->inbuf[0], Z_BUFSIZE, this->strm.total_in);
            if (rc < 0) {
                return rc;
            }
            this->strm.next_in = this->inbuf;
            this->strm.avail_in = rc;
        }
        if (this->strm.avail_in) {
            int flush = last > this->strm.total_in ? Z_SYNC_FLUSH : Z_BLOCK;
            auto err = inflate(&this->strm, flush);
            if (err == Z_STREAM_END) {
                // Reached end of stream; re-init for a possible subsequent
                // stream
                continue_stream();
            } else if (err != Z_OK) {
                log_error(" inflate-error at %d: %d  %s",
                          this->strm.total_in,
                          (int) err,
                          this->strm.msg ? this->strm.msg : "");
                break;
            }

            if (this->strm.total_in >= last + SYNCPOINT_SIZE
                && size > this->strm.avail_out + GZ_WINSIZE
                && (this->strm.data_type & GZ_END_OF_BLOCK_MASK)
                && !(this->strm.data_type & GZ_END_OF_FILE_MASK))
            {
                this->syncpoints.emplace_back(this->strm, size);
                last = this->strm.total_out;
            }
        } else if (this->strm.avail_out) {
            // Processed all the gz file data but didn't fill
            // the output buffer.  We're done, even though we
            // produced fewer bytes than requested.
            break;
        }
    }
    return size - this->strm.avail_out;
}

void
line_buffer::gz_indexed::seek(off_t offset)
{
    if ((size_t) offset == this->strm.total_out) {
        return;
    }

    indexDict* dict = nullptr;
    // Find highest syncpoint not past offset
    // FIXME: Make this a binary-tree search
    for (auto& d : this->syncpoints) {
        if (d.out <= offset) {
            dict = &d;
        } else {
            break;
        }
    }

    // Choose highest available syncpoint, or keep current offset if it's ok
    if ((size_t) offset < this->strm.total_out
        || (dict && this->strm.total_out < (size_t) dict->out))
    {
        // Release the old z_stream
        inflateEnd(&this->strm);
        if (dict) {
            dict->apply(&this->strm);
        } else {
            init_stream();
        }
    }

    // Stream from compressed file until we reach our offset
    unsigned char dummy[Z_BUFSIZE];
    while ((size_t) offset > this->strm.total_out) {
        size_t to_copy
            = std::min(static_cast<size_t>(Z_BUFSIZE),
                       static_cast<size_t>(offset - this->strm.total_out));
        auto bytes = stream_data(dummy, to_copy);
        if (bytes <= 0) {
            break;
        }
    }
}

int
line_buffer::gz_indexed::read(void* buf, size_t offset, size_t size)
{
    if (offset != this->strm.total_out) {
        // log_debug("doing seek!  %d %d", offset, this->strm.total_out);
        this->seek(offset);
    }

    int bytes = stream_data(buf, size);

    return bytes;
}

line_buffer::
line_buffer()
{
    ensure(this->invariant());
}

line_buffer::~
line_buffer()
{
    if (this->lb_loader_future.valid()) {
        this->lb_loader_future.wait();
    }

    auto empty_fd = auto_fd();

    // Make sure any shared refs take ownership of the data.
    this->lb_share_manager.invalidate_refs();
    this->set_fd(empty_fd);
}

void
line_buffer::set_fd(auto_fd& fd)
{
    file_off_t newoff = 0;

    {
        safe::WriteAccess<safe_gz_indexed> gi(this->lb_gz_file);

        if (*gi) {
            gi->close();
        }
    }

    if (this->lb_bz_file) {
        this->lb_bz_file = false;
    }

    if (fd != -1) {
        /* Sync the fd's offset with the object. */
        newoff = lseek(fd, 0, SEEK_CUR);
        if (newoff == -1) {
            if (errno != ESPIPE) {
                throw error(errno);
            }

            /* It's a pipe, start with a zero offset. */
            newoff = 0;
            this->lb_seekable = false;
        } else {
            char gz_id[2 + 1 + 1 + 4];

            if (pread(fd, gz_id, sizeof(gz_id), 0) == sizeof(gz_id)) {
                auto piper_hdr_opt = lnav::piper::read_header(fd, gz_id);

                if (piper_hdr_opt) {
                    static intern_string_t SRC = intern_string::lookup("piper");

                    auto meta_buf = std::move(piper_hdr_opt.value());

                    auto meta_sf = string_fragment::from_bytes(meta_buf.in(),
                                                               meta_buf.size());
                    auto meta_parse_res
                        = lnav::piper::header_handlers.parser_for(SRC).of(
                            meta_sf);
                    if (meta_parse_res.isErr()) {
                        log_error("failed to parse piper header: %s",
                                  meta_parse_res.unwrapErr()[0]
                                      .to_attr_line()
                                      .get_string()
                                      .c_str());
                        throw error(EINVAL);
                    }

                    this->lb_line_metadata = true;
                    this->lb_file_offset
                        = lnav::piper::HEADER_SIZE + meta_buf.size();
                    this->lb_piper_header_size = this->lb_file_offset;
                    this->lb_header = meta_parse_res.unwrap();
                } else if (gz_id[0] == '\037' && gz_id[1] == '\213') {
                    int gzfd = dup(fd);

                    log_perror(fcntl(gzfd, F_SETFD, FD_CLOEXEC));
                    if (lseek(fd, 0, SEEK_SET) < 0) {
                        close(gzfd);
                        throw error(errno);
                    }
                    lnav::gzip::header hdr;

                    this->lb_gz_file.writeAccess()->open(gzfd, hdr);
                    this->lb_compressed = true;
                    this->lb_file_time = hdr.h_mtime.tv_sec;
                    if (this->lb_file_time < 0) {
                        this->lb_file_time = 0;
                    }
                    this->lb_compressed_offset
                        = lseek(this->lb_fd, 0, SEEK_CUR);
                    if (!hdr.empty()) {
                        this->lb_header = std::move(hdr);
                    }
                    this->resize_buffer(INITIAL_COMPRESSED_BUFFER_SIZE);
                }
#ifdef HAVE_BZLIB_H
                else if (gz_id[0] == 'B' && gz_id[1] == 'Z')
                {
                    if (lseek(fd, 0, SEEK_SET) < 0) {
                        throw error(errno);
                    }
                    this->lb_bz_file = true;
                    this->lb_compressed = true;

                    /*
                     * Loading data from a bzip2 file is pretty slow, so we try
                     * to keep as much in memory as possible.
                     */
                    this->resize_buffer(INITIAL_COMPRESSED_BUFFER_SIZE);

                    this->lb_compressed_offset = 0;
                }
#endif
            }
            this->lb_seekable = true;
        }
    }
    this->lb_file_offset = newoff;
    this->lb_buffer.clear();
    this->lb_fd = std::move(fd);

    ensure(this->invariant());
}

void
line_buffer::resize_buffer(size_t new_max)
{
    if (new_max <= MAX_LINE_BUFFER_SIZE
        && new_max > (size_t) this->lb_buffer.capacity())
    {
        /* Still need more space, try a realloc. */
        this->lb_share_manager.invalidate_refs();
        this->lb_buffer.expand_to(new_max);
    }
}

void
line_buffer::ensure_available(file_off_t start, ssize_t max_length)
{
    ssize_t prefill, available;

    require(this->lb_compressed || max_length <= MAX_LINE_BUFFER_SIZE);

    // log_debug("ensure avail %d %d", start, max_length);

    if (this->lb_file_size != -1) {
        if (start + (file_off_t) max_length > this->lb_file_size) {
            max_length = (this->lb_file_size - start);
        }
    }

    /*
     * Check to see if the start is inside the cached range or immediately
     * after.
     */
    if (start < this->lb_file_offset
        || start > (file_off_t) (this->lb_file_offset + this->lb_buffer.size()))
    {
        /*
         * The request is outside the cached range, need to reload the
         * whole thing.
         */
        this->lb_share_manager.invalidate_refs();
        prefill = 0;
        this->lb_buffer.clear();
        if ((this->lb_file_size != (ssize_t) -1)
            && (start + this->lb_buffer.capacity() > this->lb_file_size))
        {
            require(start <= this->lb_file_size);
            /*
             * If the start is near the end of the file, move the offset back a
             * bit so we can get more of the file in the cache.
             */
            this->lb_file_offset = this->lb_file_size
                - std::min(this->lb_file_size,
                           (file_ssize_t) this->lb_buffer.capacity());
        } else {
            this->lb_file_offset = start;
        }
    } else {
        /* The request is in the cached range.  Record how much extra data is in
         * the buffer before the requested range.
         */
        prefill = start - this->lb_file_offset;
    }
    require(this->lb_file_offset <= start);
    require(prefill <= this->lb_buffer.size());

    available = this->lb_buffer.capacity() - (start - this->lb_file_offset);
    require(available <= this->lb_buffer.capacity());

    if (max_length > available) {
        // log_debug("need more space!");
        /*
         * Need more space, move any existing data to the front of the
         * buffer.
         */
        this->lb_share_manager.invalidate_refs();

        this->lb_buffer.resize_by(-prefill);
        this->lb_file_offset += prefill;
        // log_debug("adjust file offset for prefill %d", this->lb_file_offset);
        memmove(this->lb_buffer.at(0),
                this->lb_buffer.at(prefill),
                this->lb_buffer.size());

        available = this->lb_buffer.capacity() - (start - this->lb_file_offset);
        if (max_length > available) {
            this->resize_buffer(roundup_size(max_length, DEFAULT_INCREMENT));
        }
    }
    this->lb_line_starts.clear();
    this->lb_line_is_utf.clear();
}

bool
line_buffer::load_next_buffer()
{
    // log_debug("loader here!");
    auto retval = false;
    auto start = this->lb_loader_file_offset.value();
    ssize_t rc = 0;
    safe::WriteAccess<safe_gz_indexed> gi(this->lb_gz_file);

    // log_debug("BEGIN preload read");
    /* ... read in the new data. */
    if (!this->lb_cached_fd && *gi) {
        if (this->lb_file_size != (ssize_t) -1 && this->in_range(start)
            && this->in_range(this->lb_file_size - 1))
        {
            rc = 0;
        } else {
            // log_debug("async decomp start");
            rc = gi->read(this->lb_alt_buffer.value().end(),
                          start + this->lb_alt_buffer.value().size(),
                          this->lb_alt_buffer.value().available());
            this->lb_compressed_offset = gi->get_source_offset();
            if (rc != -1 && (rc < this->lb_alt_buffer.value().available())
                && (start + this->lb_alt_buffer.value().size() + rc
                    > this->lb_file_size))
            {
                this->lb_file_size
                    = (start + this->lb_alt_buffer.value().size() + rc);
            }
#if 0
            log_debug("async decomp end  %d+%d:%d",
                      this->lb_alt_buffer->size(),
                      rc,
                      this->lb_alt_buffer->capacity());
#endif
        }
    }
#ifdef HAVE_BZLIB_H
    else if (!this->lb_cached_fd && this->lb_bz_file)
    {
        if (this->lb_file_size != (ssize_t) -1
            && (((ssize_t) start >= this->lb_file_size)
                || (this->in_range(start)
                    && this->in_range(this->lb_file_size - 1))))
        {
            rc = 0;
        } else {
            lock_hack::guard guard;
            char scratch[32 * 1024];
            BZFILE* bz_file;
            file_off_t seek_to;
            int bzfd;

            /*
             * Unfortunately, there is no bzseek, so we need to reopen the
             * file every time we want to do a read.
             */
            bzfd = dup(this->lb_fd);
            if (lseek(this->lb_fd, 0, SEEK_SET) < 0) {
                close(bzfd);
                throw error(errno);
            }
            if ((bz_file = BZ2_bzdopen(bzfd, "r")) == nullptr) {
                close(bzfd);
                if (errno == 0) {
                    throw std::bad_alloc();
                } else {
                    throw error(errno);
                }
            }

            seek_to = start + this->lb_alt_buffer.value().size();
            while (seek_to > 0) {
                int count;

                count = BZ2_bzread(bz_file,
                                   scratch,
                                   std::min((size_t) seek_to, sizeof(scratch)));
                seek_to -= count;
            }
            rc = BZ2_bzread(bz_file,
                            this->lb_alt_buffer->end(),
                            this->lb_alt_buffer->available());
            this->lb_compressed_offset = lseek(bzfd, 0, SEEK_SET);
            BZ2_bzclose(bz_file);

            if (rc != -1 && (rc < (this->lb_alt_buffer.value().available()))
                && (start + this->lb_alt_buffer.value().size() + rc
                    > this->lb_file_size))
            {
                this->lb_file_size
                    = (start + this->lb_alt_buffer.value().size() + rc);
            }
        }
    }
#endif
    else
    {
        rc = pread(this->lb_cached_fd ? this->lb_cached_fd.value().get()
                                      : this->lb_fd.get(),
                   this->lb_alt_buffer.value().end(),
                   this->lb_alt_buffer.value().available(),
                   start + this->lb_alt_buffer.value().size());
    }
    // XXX For some reason, cygwin is giving us a bogus return value when
    // up to the end of the file.
    if (rc > (this->lb_alt_buffer.value().available())) {
        rc = -1;
#ifdef ENODATA
        errno = ENODATA;
#else
        errno = EAGAIN;
#endif
    }
    switch (rc) {
        case 0:
            if (start < (file_off_t) this->lb_file_size) {
                retval = true;
            }
            break;

        case (ssize_t) -1:
            switch (errno) {
#ifdef ENODATA
                /* Cygwin seems to return this when pread reaches the end of
                 * the file. */
                case ENODATA:
#endif
                case EINTR:
                case EAGAIN:
                    break;

                default:
                    throw error(errno);
            }
            break;

        default:
            this->lb_alt_buffer.value().resize_by(rc);
            retval = true;
            break;
    }
    // log_debug("END preload read");

    if (start > this->lb_last_line_offset) {
        const auto* line_start = this->lb_alt_buffer.value().begin();

        do {
            auto before = line_start - this->lb_alt_buffer->begin();
            auto remaining = this->lb_alt_buffer.value().size() - before;
            auto frag = string_fragment::from_bytes(line_start, remaining);
            auto utf_scan_res = is_utf8(frag, '\n');
            auto lf = utf_scan_res.remaining_ptr(frag);
            this->lb_alt_line_starts.emplace_back(before);
            this->lb_alt_line_is_utf.emplace_back(utf_scan_res.is_valid());
            this->lb_alt_line_has_ansi.emplace_back(utf_scan_res.usr_has_ansi);

            line_start = lf;
        } while (line_start != nullptr
                 && line_start < this->lb_alt_buffer->end());
    }

    return retval;
}

bool
line_buffer::fill_range(file_off_t start, ssize_t max_length)
{
    bool retval = false;

    require(start >= 0);

    // log_debug("fill range %d %d", start, max_length);
#if 0
    log_debug("(%p) fill range %d %d (%d) %d",
              this,
              start,
              max_length,
              this->lb_file_offset,
              this->lb_buffer.size());
#endif
    if (!lnav::pid::in_child && this->lb_loader_future.valid()
        && start >= this->lb_loader_file_offset.value())
    {
#if 0
        log_debug("getting preload! %d %d",
                  start,
                  this->lb_loader_file_offset.value());
#endif
        std::optional<std::chrono::system_clock::time_point> wait_start;

        if (this->lb_loader_future.wait_for(std::chrono::seconds(0))
            != std::future_status::ready)
        {
            wait_start = std::make_optional(std::chrono::system_clock::now());
        }
        retval = this->lb_loader_future.get();
        if (false && wait_start) {
            auto diff = std::chrono::system_clock::now() - wait_start.value();
            log_debug("wait done! %d", diff.count());
        }
        // log_debug("got preload");
        this->lb_loader_future = {};
        this->lb_share_manager.invalidate_refs();
        this->lb_file_offset = this->lb_loader_file_offset.value();
        this->lb_loader_file_offset = std::nullopt;
        this->lb_buffer.swap(this->lb_alt_buffer.value());
        this->lb_alt_buffer.value().clear();
        this->lb_line_starts = std::move(this->lb_alt_line_starts);
        this->lb_alt_line_starts.clear();
        this->lb_line_is_utf = std::move(this->lb_alt_line_is_utf);
        this->lb_alt_line_is_utf.clear();
        this->lb_line_has_ansi = std::move(this->lb_alt_line_has_ansi);
        this->lb_alt_line_has_ansi.clear();
        this->lb_stats.s_used_preloads += 1;
    }
    if (this->in_range(start)
        && (max_length == 0 || this->in_range(start + max_length - 1)))
    {
        /* Cache already has the data, nothing to do. */
        retval = true;
        if (!lnav::pid::in_child && this->lb_seekable && this->lb_buffer.full()
            && !this->lb_loader_file_offset)
        {
            // log_debug("loader available start=%d", start);
            auto last_lf_iter = std::find(
                this->lb_buffer.rbegin(), this->lb_buffer.rend(), '\n');
            if (last_lf_iter != this->lb_buffer.rend()) {
                auto usable_size
                    = std::distance(last_lf_iter, this->lb_buffer.rend());
                // log_debug("found linefeed %d", usable_size);
                if (!this->lb_alt_buffer) {
                    // log_debug("allocating new buffer!");
                    this->lb_alt_buffer
                        = auto_buffer::alloc(this->lb_buffer.capacity());
                }
                this->lb_alt_buffer->resize(this->lb_buffer.size()
                                            - usable_size);
                memcpy(this->lb_alt_buffer.value().begin(),
                       this->lb_buffer.at(usable_size),
                       this->lb_alt_buffer->size());
                this->lb_loader_file_offset
                    = this->lb_file_offset + usable_size;
#if 0
                log_debug("load offset %d",
                          this->lb_loader_file_offset.value());
                log_debug("launch loader");
#endif
                auto prom = std::make_shared<std::promise<bool>>();
                this->lb_loader_future = prom->get_future();
                this->lb_stats.s_requested_preloads += 1;
                isc::to<io_looper&, io_looper_tag>().send(
                    [this, prom](auto& ioloop) mutable {
                        prom->set_value(this->load_next_buffer());
                    });
            }
        }
    } else if (this->lb_fd != -1) {
        ssize_t rc;

        /* Make sure there is enough space, then */
        this->ensure_available(start, max_length);

        safe::WriteAccess<safe_gz_indexed> gi(this->lb_gz_file);

        /* ... read in the new data. */
        if (!this->lb_cached_fd && *gi) {
            // log_debug("old decomp start");
            if (this->lb_file_size != (ssize_t) -1 && this->in_range(start)
                && this->in_range(this->lb_file_size - 1))
            {
                rc = 0;
            } else {
                this->lb_stats.s_decompressions += 1;
                if (false && this->lb_last_line_offset > 0) {
                    this->lb_stats.s_hist[(this->lb_file_offset * 10)
                                          / this->lb_last_line_offset]
                        += 1;
                }
                rc = gi->read(this->lb_buffer.end(),
                              this->lb_file_offset + this->lb_buffer.size(),
                              this->lb_buffer.available());
                this->lb_compressed_offset = gi->get_source_offset();
                if (rc != -1 && (rc < this->lb_buffer.available())) {
                    this->lb_file_size
                        = (this->lb_file_offset + this->lb_buffer.size() + rc);
                }
            }
#if 0
            log_debug("old decomp end -- %d+%d:%d",
                      this->lb_buffer.size(),
                      rc,
                      this->lb_buffer.capacity());
#endif
        }
#ifdef HAVE_BZLIB_H
        else if (!this->lb_cached_fd && this->lb_bz_file)
        {
            if (this->lb_file_size != (ssize_t) -1
                && (((ssize_t) start >= this->lb_file_size)
                    || (this->in_range(start)
                        && this->in_range(this->lb_file_size - 1))))
            {
                rc = 0;
            } else {
                lock_hack::guard guard;
                char scratch[32 * 1024];
                BZFILE* bz_file;
                file_off_t seek_to;
                int bzfd;

                /*
                 * Unfortunately, there is no bzseek, so we need to reopen the
                 * file every time we want to do a read.
                 */
                bzfd = dup(this->lb_fd);
                if (lseek(this->lb_fd, 0, SEEK_SET) < 0) {
                    close(bzfd);
                    throw error(errno);
                }
                if ((bz_file = BZ2_bzdopen(bzfd, "r")) == NULL) {
                    close(bzfd);
                    if (errno == 0) {
                        throw std::bad_alloc();
                    } else {
                        throw error(errno);
                    }
                }

                seek_to = this->lb_file_offset + this->lb_buffer.size();
                while (seek_to > 0) {
                    int count;

                    count = BZ2_bzread(
                        bz_file,
                        scratch,
                        std::min((size_t) seek_to, sizeof(scratch)));
                    seek_to -= count;
                }
                rc = BZ2_bzread(bz_file,
                                this->lb_buffer.end(),
                                this->lb_buffer.available());
                this->lb_compressed_offset = lseek(bzfd, 0, SEEK_SET);
                BZ2_bzclose(bz_file);

                if (rc != -1 && (rc < (this->lb_buffer.available()))) {
                    this->lb_file_size
                        = (this->lb_file_offset + this->lb_buffer.size() + rc);
                }
            }
        }
#endif
        else if (this->lb_seekable)
        {
            this->lb_stats.s_preads += 1;
            if (false && this->lb_last_line_offset > 0) {
                this->lb_stats.s_hist[(this->lb_file_offset * 10)
                                      / this->lb_last_line_offset]
                    += 1;
            }
#if 0
            log_debug("%d: pread %lld",
                      this->lb_fd.get(),
                      this->lb_file_offset + this->lb_buffer.size());
#endif
            rc = pread(this->lb_cached_fd ? this->lb_cached_fd.value().get()
                                          : this->lb_fd.get(),
                       this->lb_buffer.end(),
                       this->lb_buffer.available(),
                       this->lb_file_offset + this->lb_buffer.size());
            // log_debug("pread rc %d", rc);
        } else {
            rc = read(this->lb_fd,
                      this->lb_buffer.end(),
                      this->lb_buffer.available());
        }
        // XXX For some reason, cygwin is giving us a bogus return value when
        // up to the end of the file.
        if (rc > (this->lb_buffer.available())) {
            rc = -1;
#ifdef ENODATA
            errno = ENODATA;
#else
            errno = EAGAIN;
#endif
        }
        switch (rc) {
            case 0:
                if (!this->lb_seekable) {
                    this->lb_file_size
                        = this->lb_file_offset + this->lb_buffer.size();
                }
                if (start < (file_off_t) this->lb_file_size) {
                    retval = true;
                }

                if (this->lb_compressed) {
                    /*
                     * For compressed files, increase the buffer size so we
                     * don't have to spend as much time uncompressing the data.
                     */
                    this->resize_buffer(MAX_COMPRESSED_BUFFER_SIZE);
                }
                break;

            case (ssize_t) -1:
                switch (errno) {
#ifdef ENODATA
                    /* Cygwin seems to return this when pread reaches the end of
                     * the */
                    /* file. */
                    case ENODATA:
#endif
                    case EINTR:
                    case EAGAIN:
                        break;

                    default:
                        throw error(errno);
                }
                break;

            default:
                this->lb_buffer.resize_by(rc);
                retval = true;
                break;
        }

        if (!lnav::pid::in_child && this->lb_seekable && this->lb_buffer.full()
            && !this->lb_loader_file_offset)
        {
            // log_debug("loader available2 start=%d", start);
            auto last_lf_iter = std::find(
                this->lb_buffer.rbegin(), this->lb_buffer.rend(), '\n');
            if (last_lf_iter != this->lb_buffer.rend()) {
                auto usable_size
                    = std::distance(last_lf_iter, this->lb_buffer.rend());
                // log_debug("found linefeed %d", usable_size);
                if (!this->lb_alt_buffer) {
                    // log_debug("allocating new buffer!");
                    this->lb_alt_buffer
                        = auto_buffer::alloc(this->lb_buffer.capacity());
                } else if (this->lb_alt_buffer->capacity()
                           < this->lb_buffer.capacity())
                {
                    this->lb_alt_buffer->expand_to(this->lb_buffer.capacity());
                }
                this->lb_alt_buffer->resize(this->lb_buffer.size()
                                            - usable_size);
                memcpy(this->lb_alt_buffer->begin(),
                       this->lb_buffer.at(usable_size),
                       this->lb_alt_buffer->size());
                this->lb_loader_file_offset
                    = this->lb_file_offset + usable_size;
#if 0
                log_debug("load offset %d",
                          this->lb_loader_file_offset.value());
                log_debug("launch loader");
#endif
                auto prom = std::make_shared<std::promise<bool>>();
                this->lb_loader_future = prom->get_future();
                this->lb_stats.s_requested_preloads += 1;
                isc::to<io_looper&, io_looper_tag>().send(
                    [this, prom](auto& ioloop) mutable {
                        prom->set_value(this->load_next_buffer());
                    });
            }
        }
        ensure(this->lb_buffer.size() <= this->lb_buffer.capacity());
    }

    return retval;
}

Result<line_info, std::string>
line_buffer::load_next_line(file_range prev_line)
{
    const char* line_start = nullptr;
    bool done = false;
    line_info retval;

    require(this->lb_fd != -1);

    if (this->lb_line_metadata && prev_line.fr_offset == 0) {
        prev_line.fr_offset = this->lb_piper_header_size;
    }

    auto offset = prev_line.next_offset();
    ssize_t request_size = INITIAL_REQUEST_SIZE;
    retval.li_file_range.fr_offset = offset;
    if (this->lb_buffer.empty() || !this->in_range(offset)) {
        this->fill_range(offset, this->lb_buffer.capacity());
    } else if (offset == this->lb_file_offset + this->lb_buffer.size()) {
        if (!this->fill_range(offset, INITIAL_REQUEST_SIZE)) {
            retval.li_file_range.fr_offset = offset;
            retval.li_file_range.fr_size = 0;
            if (this->is_pipe()) {
                retval.li_partial = !this->is_pipe_closed();
            } else {
                retval.li_partial = true;
            }
            return Ok(retval);
        }
    }
    if (prev_line.next_offset() == 0) {
        auto is_utf_res = is_utf8(string_fragment::from_bytes(
            this->lb_buffer.begin(), this->lb_buffer.size()));
        this->lb_is_utf8 = is_utf_res.is_valid();
        if (!this->lb_is_utf8) {
            log_warning("input is not utf8 -- %s", is_utf_res.usr_message);
        }
    }
    while (!done) {
        auto old_retval_size = retval.li_file_range.fr_size;
        const char* lf = nullptr;

        /* Find the data in the cache and */
        line_start = this->get_range(offset, retval.li_file_range.fr_size);
        /* ... look for the end-of-line or end-of-file. */
        ssize_t utf8_end = -1;

        bool found_in_cache = false;
        if (!this->lb_line_starts.empty()) {
            auto buffer_offset = offset - this->lb_file_offset;

            auto start_iter = std::lower_bound(this->lb_line_starts.begin(),
                                               this->lb_line_starts.end(),
                                               buffer_offset);
            if (start_iter != this->lb_line_starts.end()) {
                auto next_line_iter = start_iter + 1;

                // log_debug("found offset %d %d", buffer_offset, *start_iter);
                if (next_line_iter != this->lb_line_starts.end()) {
                    utf8_end = *next_line_iter - 1 - *start_iter;
                    found_in_cache = true;
                    lf = line_start + utf8_end;
                } else {
                    // log_debug("no next iter");
                }
            } else {
                // log_debug("no buffer_offset found");
            }
        }

        if (!found_in_cache) {
            auto frag = string_fragment::from_bytes(
                line_start, retval.li_file_range.fr_size);
            auto scan_res = is_utf8(frag, '\n');
            lf = scan_res.remaining_ptr(frag);
            if (lf != nullptr) {
                lf -= 1;
            }
            retval.li_utf8_scan_result = scan_res;
        }

        auto got_new_data = old_retval_size != retval.li_file_range.fr_size;
#if 0
        log_debug("load next loop %p reqsize %d lsize %d",
                  lf,
                  request_size,
                  retval.li_file_range.fr_size);
#endif
        if (lf != nullptr
            || (retval.li_file_range.fr_size >= MAX_LINE_BUFFER_SIZE)
            || (request_size >= MAX_LINE_BUFFER_SIZE)
            || (!got_new_data
                && (!this->is_pipe() || request_size > DEFAULT_INCREMENT)))
        {
            if ((lf != nullptr)
                && ((size_t) (lf - line_start) >= MAX_LINE_BUFFER_SIZE - 1))
            {
                lf = nullptr;
            }
            if (lf != nullptr) {
                retval.li_partial = false;
                retval.li_file_range.fr_size = lf - line_start;
                // delim
                retval.li_file_range.fr_size += 1;
                if (offset >= this->lb_last_line_offset) {
                    this->lb_last_line_offset
                        = offset + retval.li_file_range.fr_size;
                }
            } else {
                if (retval.li_file_range.fr_size >= MAX_LINE_BUFFER_SIZE) {
                    log_warning("Line exceeded max size: offset=%d", offset);
                    retval.li_file_range.fr_size = MAX_LINE_BUFFER_SIZE - 1;
                    retval.li_partial = false;
                } else {
                    retval.li_partial = true;
                }
                this->ensure_available(offset, retval.li_file_range.fr_size);

                if (retval.li_file_range.fr_size >= MAX_LINE_BUFFER_SIZE) {
                    retval.li_file_range.fr_size = MAX_LINE_BUFFER_SIZE - 1;
                }
                if (retval.li_partial) {
                    /*
                     * Since no delimiter was seen, we need to remember the
                     * offset of the last line in the file so we don't
                     * mistakenly return two partial lines to the caller.
                     *
                     *   1. read_line() - returns partial line
                     *   2. file is written
                     *   3. read_line() - returns the middle of partial line.
                     */
                    this->lb_last_line_offset = offset;
                } else if (offset >= this->lb_last_line_offset) {
                    this->lb_last_line_offset
                        = offset + retval.li_file_range.fr_size;
                }
            }

            offset += retval.li_file_range.fr_size;

            done = true;
        } else {
            if (!this->is_pipe() || !this->is_pipe_closed()) {
                retval.li_partial = true;
            }
            request_size
                = std::min<ssize_t>(this->lb_buffer.size() + DEFAULT_INCREMENT,
                                    MAX_LINE_BUFFER_SIZE);
        }

        if (!done
            && !this->fill_range(
                offset,
                std::max(request_size, (ssize_t) this->lb_buffer.available())))
        {
            break;
        }
    }

    ensure(retval.li_file_range.fr_size <= this->lb_buffer.size());
    ensure(this->invariant());
#if 0
    log_debug("got line part %d %d",
              retval.li_file_range.fr_offset,
              (int) retval.li_partial);
#endif

    retval.li_file_range.fr_metadata.m_has_ansi
        = retval.li_utf8_scan_result.usr_has_ansi;
    retval.li_file_range.fr_metadata.m_valid_utf
        = retval.li_utf8_scan_result.is_valid();

    if (this->lb_line_metadata) {
        auto sv = scn::string_view{
            line_start,
            (size_t) retval.li_file_range.fr_size,
        };

        char level = '\0';
        auto scan_res = scn::scan(sv,
                                  "{}.{}:{};",
                                  retval.li_timestamp.tv_sec,
                                  retval.li_timestamp.tv_usec,
                                  level);
        if (scan_res) {
            retval.li_timestamp.tv_sec
                = lnav::to_local_time(date::sys_seconds{std::chrono::seconds{
                                          retval.li_timestamp.tv_sec}})
                      .time_since_epoch()
                      .count();
            retval.li_level = abbrev2level(&level, 1);
        }
    }

    return Ok(retval);
}

Result<shared_buffer_ref, std::string>
line_buffer::read_range(file_range fr)
{
    shared_buffer_ref retval;
    const char* line_start;
    file_ssize_t avail;

#if 0
    if (this->lb_last_line_offset != -1
        && fr.fr_offset > this->lb_last_line_offset)
    {
        /*
         * Don't return anything past the last known line.  The caller needs
         * to try reading at the offset of the last line again.
         */
        return Err(
            fmt::format(FMT_STRING("attempt to read past the known end of the "
                                   "file: read-offset={}; last_line_offset={}"),
                        fr.fr_offset,
                        this->lb_last_line_offset));
    }
#endif

    if (!(this->in_range(fr.fr_offset)
          && this->in_range(fr.fr_offset + fr.fr_size - 1)))
    {
        if (!this->fill_range(fr.fr_offset, fr.fr_size)) {
            return Err(std::string("unable to read file"));
        }
    }
    line_start = this->get_range(fr.fr_offset, avail);

    if (fr.fr_size > avail) {
        return Err(fmt::format(
            FMT_STRING("short-read (need: {}; avail: {})"), fr.fr_size, avail));
    }
    if (this->lb_line_metadata) {
        auto new_start
            = static_cast<const char*>(memchr(line_start, ';', fr.fr_size));
        if (new_start) {
            auto offset = new_start - line_start + 1;
            line_start += offset;
            fr.fr_size -= offset;
        }
    }
    retval.share(this->lb_share_manager, line_start, fr.fr_size);
    retval.get_metadata() = fr.fr_metadata;

    return Ok(std::move(retval));
}

file_range
line_buffer::get_available()
{
    return {this->lb_file_offset,
            static_cast<file_ssize_t>(this->lb_buffer.size())};
}

line_buffer::gz_indexed::indexDict::
indexDict(const z_stream& s, const file_size_t size)
{
    assert((s.data_type & GZ_END_OF_BLOCK_MASK));
    assert(!(s.data_type & GZ_END_OF_FILE_MASK));
    assert(size >= s.avail_out + GZ_WINSIZE);
    this->bits = s.data_type & GZ_BORROW_BITS_MASK;
    this->in = s.total_in;
    this->out = s.total_out;
    auto last_byte_in = s.next_in[-1];
    this->in_bits = last_byte_in >> (8 - this->bits);
    // Copy the last 32k uncompressed data (sliding window) to our
    // index
    memcpy(this->index, s.next_out - GZ_WINSIZE, GZ_WINSIZE);
}

int
line_buffer::gz_indexed::indexDict::apply(z_streamp s)
{
    s->zalloc = Z_NULL;
    s->zfree = Z_NULL;
    s->opaque = Z_NULL;
    s->avail_in = 0;
    s->next_in = Z_NULL;
    auto ret = inflateInit2(s, GZ_RAW_MODE);
    if (ret != Z_OK) {
        return ret;
    }
    if (this->bits) {
        inflatePrime(s, this->bits, this->in_bits);
    }
    s->total_in = this->in;
    s->total_out = this->out;
    inflateSetDictionary(s, this->index, GZ_WINSIZE);
    return ret;
}

bool
line_buffer::is_likely_to_flush(file_range prev_line)
{
    auto avail = this->get_available();

    if (prev_line.fr_offset < avail.fr_offset) {
        return true;
    }
    auto prev_line_end = prev_line.fr_offset + prev_line.fr_size;
    auto avail_end = avail.fr_offset + avail.fr_size;
    if (avail_end < prev_line_end) {
        return true;
    }
    auto remaining = avail_end - prev_line_end;
    return remaining < INITIAL_REQUEST_SIZE;
}

void
line_buffer::quiesce()
{
    if (this->lb_loader_future.valid()) {
        this->lb_loader_future.wait();
    }
}

static std::filesystem::path
line_buffer_cache_path()
{
    return lnav::paths::workdir() / "buffer-cache";
}

void
line_buffer::enable_cache()
{
    if (!this->lb_compressed || this->lb_cached_fd) {
        log_info("%d: skipping cache request (compressed=%d already-cached=%d)",
                 this->lb_fd.get(),
                 this->lb_compressed,
                 (bool) this->lb_cached_fd);
        return;
    }

    struct stat st;

    if (fstat(this->lb_fd, &st) == -1) {
        log_error("failed to fstat(%d) - %d", this->lb_fd.get(), errno);
        return;
    }

    auto cached_base_name = hasher()
                                .update(st.st_dev)
                                .update(st.st_ino)
                                .update(st.st_size)
                                .to_string();
    auto cache_dir = line_buffer_cache_path() / cached_base_name.substr(0, 2);

    std::filesystem::create_directories(cache_dir);

    auto cached_file_name = fmt::format(FMT_STRING("{}.bin"), cached_base_name);
    auto cached_file_path = cache_dir / cached_file_name;
    auto cached_done_path
        = cache_dir / fmt::format(FMT_STRING("{}.done"), cached_base_name);

    log_info(
        "%d:cache file path: %s", this->lb_fd.get(), cached_file_path.c_str());

    auto fl = lnav::filesystem::file_lock(cached_file_path);
    auto guard = lnav::filesystem::file_lock::guard(&fl);

    if (std::filesystem::exists(cached_done_path)) {
        log_info("%d:using existing cache file", this->lb_fd.get());
        auto open_res = lnav::filesystem::open_file(cached_file_path, O_RDWR);
        if (open_res.isOk()) {
            this->lb_cached_fd = open_res.unwrap();
            return;
        }
        std::filesystem::remove(cached_done_path);
    }

    auto create_res = lnav::filesystem::create_file(
        cached_file_path, O_RDWR | O_TRUNC, 0600);
    if (create_res.isErr()) {
        log_error("failed to create cache file: %s -- %s",
                  cached_file_path.c_str(),
                  create_res.unwrapErr().c_str());
        return;
    }

    auto write_fd = create_res.unwrap();
    auto done = false;

    static constexpr ssize_t FILL_LENGTH = 1024 * 1024;
    auto off = file_off_t{0};
    while (!done) {
        log_debug("%d: caching file content at %d", this->lb_fd.get(), off);
        if (!this->fill_range(off, FILL_LENGTH)) {
            log_debug("%d: caching finished", this->lb_fd.get());
            done = true;
        } else {
            file_ssize_t avail;

            const auto* data = this->get_range(off, avail);
            auto rc = write(write_fd, data, avail);
            if (rc != avail) {
                log_error("%d: short write!", this->lb_fd.get());
                return;
            }

            off += avail;
        }
    }

    lnav::filesystem::create_file(cached_done_path, O_WRONLY, 0600);

    this->lb_cached_fd = std::move(write_fd);
}

void
line_buffer::cleanup_cache()
{
    (void) std::async(std::launch::async, []() {
        auto now = std::filesystem::file_time_type::clock::now();
        auto cache_path = line_buffer_cache_path();
        std::vector<std::filesystem::path> to_remove;
        std::error_code ec;

        for (const auto& cache_subdir :
             std::filesystem::directory_iterator(cache_path, ec))
        {
            for (const auto& entry :
                 std::filesystem::directory_iterator(cache_subdir, ec))
            {
                auto mtime = std::filesystem::last_write_time(entry.path());
                auto exp_time = mtime + 1h;
                if (now < exp_time) {
                    continue;
                }

                to_remove.emplace_back(entry.path());
            }
        }

        for (auto& entry : to_remove) {
            log_debug("removing compressed file cache: %s", entry.c_str());
            std::filesystem::remove_all(entry, ec);
        }
    });
}
