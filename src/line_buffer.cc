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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file line_buffer.cc
 */

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef HAVE_BZLIB_H
#include <bzlib.h>
#endif

#include <set>

#ifdef HAVE_X86INTRIN_H
#include "simdutf8check.h"
#endif

#include "base/math_util.hh"
#include "base/is_utf8.hh"
#include "line_buffer.hh"
#include "fmtlib/fmt/format.h"

using namespace std;

static const ssize_t DEFAULT_INCREMENT          = 128 * 1024;
static const ssize_t MAX_COMPRESSED_BUFFER_SIZE = 32 * 1024 * 1024;

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

        guard() : g_lock(lock_hack::singleton())
        {
            this->g_lock.lock();
        };

        ~guard()
        {
            this->g_lock.unlock();
        };

private:
        lock_hack &g_lock;
    };

    static lock_hack &singleton()
    {
        static lock_hack retval;

        return retval;
    };

    void lock()
    {
        lockf(this->lh_fd, F_LOCK, 0);
    };

    void unlock()
    {
        lockf(this->lh_fd, F_ULOCK, 0);
    };

private:

    lock_hack()
    {
        char lockname[64];

        snprintf(lockname, sizeof(lockname), "/tmp/lnav.%d.lck", getpid());
        this->lh_fd = open(lockname, O_CREAT | O_RDWR, 0600);
        log_perror(fcntl(this->lh_fd, F_SETFD, FD_CLOEXEC));
        unlink(lockname);
    };

    auto_fd lh_fd;
};
/* XXX END */

static int32_t read_le32(const unsigned char *data)
{
    return (
        (data[0] <<  0) |
        (data[1] <<  8) |
        (data[2] << 16) |
        (data[3] << 24));
}

#define Z_BUFSIZE 65536U
#define SYNCPOINT_SIZE (1024 * 1024)
line_buffer::gz_indexed::gz_indexed()
{
    if ((this->inbuf = (Bytef *)malloc(Z_BUFSIZE)) == NULL) {
        throw bad_alloc();
    }
}

void line_buffer::gz_indexed::close()
{
    // Release old stream, if we were open
    if (*this) {
        inflateEnd(&this->strm);
        ::close(this->gz_fd);
        this->syncpoints.clear();
        this->gz_fd = -1;
    }
}

void line_buffer::gz_indexed::init_stream()
{
    if (*this) {
        inflateEnd(&this->strm);
    }

    // initialize inflate struct
    this->strm.zalloc = Z_NULL;
    this->strm.zfree = Z_NULL;
    this->strm.opaque = Z_NULL;
    this->strm.avail_in = 0;
    this->strm.next_in = Z_NULL;
    this->strm.avail_out = 0;
    int rc = inflateInit2(&strm, GZ_HEADER_MODE);
    if (rc != Z_OK) {
        log_error(" inflateInit2: %d  %s", (int)rc, this->strm.msg ? this->strm.msg : "");
    }
}
void line_buffer::gz_indexed::open(int fd)
{
    this->close();
    this->init_stream();
    this->gz_fd = fd;
}

int line_buffer::gz_indexed::stream_data(void * buf, size_t size)
{
    this->strm.avail_out = size;
    this->strm.next_out = (unsigned char *) buf;

    size_t last = this->syncpoints.empty() ? 0 :
                    this->syncpoints.back().in;
    while (this->strm.avail_out) {
        if (!this->strm.avail_in) {
            int rc = ::pread(this->gz_fd,
                        &this->inbuf[0],
                        Z_BUFSIZE,
                        this->strm.total_in);
            if (rc < 0) {
                return rc;
            }
            this->strm.next_in = this->inbuf;
            this->strm.avail_in = rc;
        }
        if (this->strm.avail_in) {
            int flush = last > this->strm.total_in
                          ? Z_SYNC_FLUSH : Z_BLOCK;
            auto err = inflate(&this->strm, flush);
            if (err == Z_STREAM_END) {
                break;
            } else if (err != Z_OK) {
                log_error(" inflate-error: %d  %s", (int)err, this->strm.msg ? this->strm.msg : "");
                return 0;
            }

            if (this->strm.total_in >= last + SYNCPOINT_SIZE &&
                size > this->strm.avail_out + GZ_WINSIZE &&
                (this->strm.data_type & GZ_END_OF_BLOCK_MASK) &&
                !(this->strm.data_type & GZ_END_OF_FILE_MASK))
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

void line_buffer::gz_indexed::seek(off_t offset)
{
    if ((size_t) offset == this->strm.total_out) {
        return;
    }

    indexDict * dict = nullptr;
    // Find highest syncpoint not past offset
    // FIXME: Make this a binary-tree search
    for (auto &d : this->syncpoints) {
        if (d.out <= offset) {
            dict = &d;
        } else {
            break;
        }
    }

    // Choose highest available syncpoint, or keep current offset if it's ok
    if ((size_t) offset < this->strm.total_out ||
        (dict && this->strm.total_out < (size_t) dict->out)) {
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
        size_t to_copy = std::min(
            static_cast<size_t>(Z_BUFSIZE),
            static_cast<size_t>(offset - this->strm.total_out));
        auto bytes = stream_data(dummy, to_copy);
        if (bytes <= 0) {
            break;
        }
    }
}

int line_buffer::gz_indexed::read(void * buf, size_t offset, size_t size)
{
    if (offset != this->strm.total_out) {
        this->seek(offset);
    }

    int bytes = stream_data(buf, size);

    return bytes;
}

line_buffer::line_buffer()
    : lb_bz_file(false),
      lb_compressed_offset(0),
      lb_file_size(-1),
      lb_file_offset(0),
      lb_file_time(0),
      lb_buffer_size(0),
      lb_buffer_max(DEFAULT_LINE_BUFFER_SIZE),
      lb_seekable(false),
      lb_last_line_offset(-1)
{
    if ((this->lb_buffer = (char *)malloc(this->lb_buffer_max)) == nullptr) {
        throw bad_alloc();
    }

    ensure(this->invariant());
}

line_buffer::~line_buffer()
{
    auto empty_fd = auto_fd();

    // Make sure any shared refs take ownership of the data.
    this->lb_share_manager.invalidate_refs();
    this->set_fd(empty_fd);
}

void line_buffer::set_fd(auto_fd &fd)
{
    off_t newoff = 0;

    if (this->lb_gz_file) {
        this->lb_gz_file.close();
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
            newoff            = 0;
            this->lb_seekable = false;
        }
        else {
            char gz_id[2 + 1 + 1 + 4];

            if (pread(fd, gz_id, sizeof(gz_id), 0) == sizeof(gz_id)) {
                if (gz_id[0] == '\037' && gz_id[1] == '\213') {
                    int gzfd = dup(fd);

                    log_perror(fcntl(gzfd, F_SETFD, FD_CLOEXEC));
                    if (lseek(fd, 0, SEEK_SET) < 0) {
                        close(gzfd);
                        throw error(errno);
                    }
                    lb_gz_file.open(gzfd);
                    this->lb_file_time = read_le32(
                        (const unsigned char *)&gz_id[4]);
                    if (this->lb_file_time < 0) {
                        this->lb_file_time = 0;
                    }
                    this->lb_compressed_offset = lseek(this->lb_fd, 0, SEEK_CUR);
                }
#ifdef HAVE_BZLIB_H
                else if (gz_id[0] == 'B' && gz_id[1] == 'Z') {
                    if (lseek(fd, 0, SEEK_SET) < 0) {
                        throw error(errno);
                    }
                    this->lb_bz_file = true;

                    /*
                     * Loading data from a bzip2 file is pretty slow, so we try
                     * to keep as much in memory as possible.
                     */
                    this->resize_buffer(MAX_COMPRESSED_BUFFER_SIZE);

                    this->lb_compressed_offset = 0;
                }
#endif
            }
            this->lb_seekable = true;
        }
    }
    this->lb_file_offset = newoff;
    this->lb_buffer_size = 0;
    this->lb_fd          = std::move(fd);

    ensure(this->invariant());
}

void line_buffer::resize_buffer(size_t new_max)
{
    require(this->lb_bz_file || this->lb_gz_file ||
        new_max <= MAX_LINE_BUFFER_SIZE);

    if (new_max > (size_t)this->lb_buffer_max) {
        char *tmp, *old;

        /* Still need more space, try a realloc. */
        old = this->lb_buffer.release();
        this->lb_share_manager.invalidate_refs();
        tmp = (char *) realloc(old, new_max);
        if (tmp != NULL) {
            this->lb_buffer = tmp;
            this->lb_buffer_max = new_max;
        } else {
            this->lb_buffer = old;

            throw error(ENOMEM);
        }
    }
}

void line_buffer::ensure_available(off_t start, ssize_t max_length)
{
    ssize_t prefill, available;

    require(max_length <= MAX_LINE_BUFFER_SIZE);

    if (this->lb_file_size != -1) {
        if (start + (off_t)max_length > this->lb_file_size) {
            max_length = (this->lb_file_size - start);
        }
    }

    /*
     * Check to see if the start is inside the cached range or immediately
     * after.
     */
    if (start < this->lb_file_offset ||
        start > (off_t)(this->lb_file_offset + this->lb_buffer_size)) {
        /*
         * The request is outside the cached range, need to reload the
         * whole thing.
         */
        this->lb_share_manager.invalidate_refs();
        prefill = 0;
        this->lb_buffer_size = 0;
        if ((this->lb_file_size != (ssize_t)-1) &&
            (start + this->lb_buffer_max > this->lb_file_size)) {
            /*
             * If the start is near the end of the file, move the offset back a
             * bit so we can get more of the file in the cache.
             */
            this->lb_file_offset = this->lb_file_size -
                                   std::min(this->lb_file_size,
                                            this->lb_buffer_max);
        }
        else {
            this->lb_file_offset = start;
        }
    }
    else {
        /* The request is in the cached range.  Record how much extra data is in
         * the buffer before the requested range.
         */
        prefill = start - this->lb_file_offset;
    }
    require(this->lb_file_offset <= start);
    require(prefill <= this->lb_buffer_size);

    available = this->lb_buffer_max - (start - this->lb_file_offset);
    require(available <= this->lb_buffer_max);

    if (max_length > available) {
        /*
         * Need more space, move any existing data to the front of the
         * buffer.
         */
        this->lb_share_manager.invalidate_refs();

        this->lb_buffer_size -= prefill;
        this->lb_file_offset += prefill;
        memmove(&this->lb_buffer[0],
                &this->lb_buffer[prefill],
                this->lb_buffer_size);

        available = this->lb_buffer_max - (start - this->lb_file_offset);
        if (max_length > available) {
            this->resize_buffer(
                roundup_size(max_length, DEFAULT_INCREMENT));
        }
    }
}

bool line_buffer::fill_range(off_t start, ssize_t max_length)
{
    bool retval = false;

    require(start >= 0);

    if (this->in_range(start) && this->in_range(start + max_length - 1)) {
        /* Cache already has the data, nothing to do. */
        retval = true;
    }
    else if (this->lb_fd != -1) {
        ssize_t rc;

        /* Make sure there is enough space, then */
        this->ensure_available(start, max_length);

        /* ... read in the new data. */
        if (this->lb_gz_file) {
            if (this->lb_file_size != (ssize_t)-1 &&
                this->in_range(start) &&
                this->in_range(this->lb_file_size - 1)) {
                rc = 0;
            }
            else {
                rc = this->lb_gz_file.read(&this->lb_buffer[this->lb_buffer_size],
                                     this->lb_file_offset + this->lb_buffer_size,
                                     this->lb_buffer_max - this->lb_buffer_size);
                this->lb_compressed_offset = this->lb_gz_file.get_source_offset();
                if (rc != -1 && (
                        rc < (this->lb_buffer_max - this->lb_buffer_size))) {
                    this->lb_file_size = (
                            this->lb_file_offset + this->lb_buffer_size + rc);
                }
            }
        }
#ifdef HAVE_BZLIB_H
        else if (this->lb_bz_file) {
            if (this->lb_file_size != (ssize_t)-1 &&
                (((ssize_t)start >= this->lb_file_size) ||
                 (this->in_range(start) &&
                  this->in_range(this->lb_file_size - 1)))) {
                rc = 0;
            }
            else {
                lock_hack::guard guard;
                char             scratch[32 * 1024];
                BZFILE *         bz_file;
                off_t            seek_to;
                int              bzfd;

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
                        throw bad_alloc();
                    }
                    else{
                        throw error(errno);
                    }
                }

                seek_to = this->lb_file_offset + this->lb_buffer_size;
                while (seek_to > 0) {
                    int count;

                    count = BZ2_bzread(bz_file,
                                       scratch,
                                       std::min((size_t)seek_to,
                                                sizeof(scratch)));
                    seek_to -= count;
                }
                rc = BZ2_bzread(bz_file,
                                &this->lb_buffer[this->lb_buffer_size],
                                this->lb_buffer_max - this->lb_buffer_size);
                this->lb_compressed_offset = lseek(bzfd, 0, SEEK_SET);
                BZ2_bzclose(bz_file);

                if (rc != -1 && (
                    rc < (this->lb_buffer_max - this->lb_buffer_size))) {
                    this->lb_file_size = (
                        this->lb_file_offset + this->lb_buffer_size + rc);
                }
            }
        }
#endif
        else if (this->lb_seekable) {
            rc = pread(this->lb_fd,
                       &this->lb_buffer[this->lb_buffer_size],
                       this->lb_buffer_max - this->lb_buffer_size,
                       this->lb_file_offset + this->lb_buffer_size);
        }
        else {
            rc = read(this->lb_fd,
                      &this->lb_buffer[this->lb_buffer_size],
                      this->lb_buffer_max - this->lb_buffer_size);
        }
        // XXX For some reason, cygwin is giving us a bogus return value when
        // up to the end of the file.
        if (rc > (this->lb_buffer_max - this->lb_buffer_size)) {
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
                this->lb_file_size = this->lb_file_offset + this->lb_buffer_size;
            }
            if (start < (off_t) this->lb_file_size) {
                retval = true;
            }

            if (this->lb_gz_file || this->lb_bz_file) {
                /*
                 * For compressed files, increase the buffer size so we don't
                 * have to spend as much time uncompressing the data.
                 */
                this->resize_buffer(MAX_COMPRESSED_BUFFER_SIZE);
            }
            break;

        case (ssize_t)-1:
            switch (errno) {
#ifdef ENODATA
            /* Cygwin seems to return this when pread reaches the end of the */
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
            this->lb_buffer_size += rc;
            retval = true;
            break;
        }

        ensure(this->lb_buffer_size <= this->lb_buffer_max);
    }

    return retval;
}

Result<line_info, string> line_buffer::load_next_line(file_range prev_line)
{
    ssize_t request_size = DEFAULT_INCREMENT;
    bool done = false;
    line_info retval;

    require(this->lb_fd != -1);

    auto offset = prev_line.next_offset();
    retval.li_file_range.fr_offset = offset;
    while (!done) {
        char *line_start, *lf;

        this->fill_range(offset, request_size);

        /* Find the data in the cache and */
        line_start = this->get_range(offset, retval.li_file_range.fr_size);
        /* ... look for the end-of-line or end-of-file. */
        ssize_t utf8_end = -1;

#ifdef HAVE_X86INTRIN_H
        if (!validate_utf8_fast(line_start, retval.li_file_range.fr_size, &utf8_end)) {
            retval.li_valid_utf = false;
        }
#else
        {
            const char *msg;
            int faulty_bytes;

            utf8_end = is_utf8((unsigned char *) line_start, retval.li_file_range.fr_size, &msg, &faulty_bytes);
            if (msg != nullptr) {
                lf = (char *) memchr(line_start, '\n', retval.li_file_range.fr_size);
                utf8_end = lf - line_start;
                retval.li_valid_utf = false;
            }
        }
#endif
        if (utf8_end >= 0) {
            lf = line_start + utf8_end;
        } else {
            lf = nullptr;
        }

        if (lf != nullptr ||
            (retval.li_file_range.fr_size >= MAX_LINE_BUFFER_SIZE) ||
            (request_size == MAX_LINE_BUFFER_SIZE) ||
            ((request_size > retval.li_file_range.fr_size) &&
             (retval.li_file_range.fr_size > 0) &&
             (!this->is_pipe() || request_size > DEFAULT_INCREMENT))) {
            if ((lf != nullptr) &&
                ((size_t) (lf - line_start) >= MAX_LINE_BUFFER_SIZE - 1)) {
                lf = nullptr;
            }
            if (lf != nullptr) {
                retval.li_partial = false;
                retval.li_file_range.fr_size = lf - line_start;
                // delim
                retval.li_file_range.fr_size += 1;
                if (offset >= this->lb_last_line_offset) {
                    this->lb_last_line_offset = offset + retval.li_file_range.fr_size;
                }
            }
            else {
                if (retval.li_file_range.fr_size >= MAX_LINE_BUFFER_SIZE) {
                    log_warning("Line exceeded max size: offset=%d",
                                offset);
                    retval.li_file_range.fr_size = MAX_LINE_BUFFER_SIZE - 1;
                    retval.li_partial = false;
                }
                else {
                    retval.li_partial = true;
                }
                this->ensure_available(offset, retval.li_file_range.fr_size);

                if (retval.li_file_range.fr_size >= MAX_LINE_BUFFER_SIZE) {
                    retval.li_file_range.fr_size = MAX_LINE_BUFFER_SIZE - 1;
                }
                if (retval.li_partial) {
                    /*
                     * Since no delimiter was seen, we need to remember the offset
                     * of the last line in the file so we don't mistakenly return
                     * two partial lines to the caller.
                     *
                     *   1. read_line() - returns partial line
                     *   2. file is written
                     *   3. read_line() - returns the middle of partial line.
                     */
                    this->lb_last_line_offset = offset;
                }
                else if (offset >= this->lb_last_line_offset) {
                    this->lb_last_line_offset = offset + retval.li_file_range.fr_size;
                }
            }

            offset += retval.li_file_range.fr_size;

            done = true;
        }
        else {
            if (!this->is_pipe() || !this->is_pipe_closed()) {
                retval.li_partial = true;
            }
            request_size += DEFAULT_INCREMENT;
        }

        if (!done && !this->fill_range(offset, request_size)) {
            break;
        }
    }

    ensure(retval.li_file_range.fr_size <= this->lb_buffer_size);
    ensure(this->invariant());

    return Ok(retval);
}

Result<shared_buffer_ref, std::string> line_buffer::read_range(const file_range fr)
{
    shared_buffer_ref retval;
    char *line_start;
    ssize_t avail;

    if (this->lb_last_line_offset != -1 &&
        fr.fr_offset > this->lb_last_line_offset) {
        /*
         * Don't return anything past the last known line.  The caller needs
         * to try reading at the offset of the last line again.
         */
        return Err(string("out-of-bounds"));
    }

    this->fill_range(fr.fr_offset, fr.fr_size);
    line_start = this->get_range(fr.fr_offset, avail);

    if (fr.fr_size > avail) {
        return Err(fmt::format("short-read (need: {}; avail: {})",
            fr.fr_size, avail));
    }
    retval.share(this->lb_share_manager, line_start, fr.fr_size);

    return Ok(retval);
}

file_range line_buffer::get_available()
{
    return {this->lb_file_offset, this->lb_buffer_size};
}
