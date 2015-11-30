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

#include "lnav_util.hh"
#include "line_buffer.hh"

using namespace std;

static const size_t DEFAULT_INCREMENT          = 128 * 1024;
static const size_t MAX_COMPRESSED_BUFFER_SIZE = 32 * 1024 * 1024;

/*
 * XXX REMOVE ME
 *
 * The stock gzipped file code does not use pread, so we need to use a lock to
 * get exclusive access to the file.  In the future, we should just rewrite
 * the gzipped file code to use pread.
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

line_buffer::line_buffer()
    : lb_gz_file(NULL),
      lb_bz_file(false),
      lb_gz_offset(0),
      lb_file_size((size_t)-1),
      lb_file_offset(0),
      lb_file_time(0),
      lb_buffer_size(0),
      lb_buffer_max(DEFAULT_LINE_BUFFER_SIZE),
      lb_seekable(false),
      lb_last_line_offset(-1)
{
    if ((this->lb_buffer = (char *)malloc(this->lb_buffer_max)) == NULL) {
        throw bad_alloc();
    }

    ensure(this->invariant());
}

line_buffer::~line_buffer()
{
    auto_fd fd = -1;

    // Make sure any shared refs take ownership of the data.
    this->lb_share_manager.invalidate_refs();
    this->set_fd(fd);
}

void line_buffer::set_fd(auto_fd &fd)
throw (error)
{
    off_t newoff = 0;

    if (this->lb_gz_file) {
        gzclose(this->lb_gz_file);
        this->lb_gz_file = NULL;
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
                    if ((this->lb_gz_file = gzdopen(gzfd, "r")) == NULL) {
                        close(gzfd);
                        if (errno == 0) {
                            throw bad_alloc();
                        }
                        else{
                            throw error(errno);
                        }
                    }
                    this->lb_file_time = read_le32(
                        (const unsigned char *)&gz_id[4]);
                    if (this->lb_file_time < 0) {
                        this->lb_file_time = 0;
                    }
                    this->lb_gz_offset = lseek(this->lb_fd, 0, SEEK_CUR);
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
                }
#endif
            }
            this->lb_seekable = true;
        }
    }
    this->lb_file_offset = newoff;
    this->lb_buffer_size = 0;
    this->lb_fd          = fd;

    ensure(this->invariant());
}

void line_buffer::resize_buffer(size_t new_max)
throw (error)
{
    char *tmp, *old;

    require(this->lb_bz_file || this->lb_gz_file ||
        new_max <= MAX_LINE_BUFFER_SIZE);

    /* Still need more space, try a realloc. */
    old = this->lb_buffer.release();
    this->lb_share_manager.invalidate_refs();
    tmp = (char *)realloc(old, new_max);
    if (tmp != NULL) {
        this->lb_buffer     = tmp;
        this->lb_buffer_max = new_max;
    }
    else {
        this->lb_buffer = old;

        throw error(ENOMEM);
    }
}

void line_buffer::ensure_available(off_t start, size_t max_length)
throw (error)
{
    size_t prefill, available;

    require(max_length <= MAX_LINE_BUFFER_SIZE);

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
    require(prefill <= (size_t)this->lb_buffer_size);

    available = this->lb_buffer_max - (start - this->lb_file_offset);
    require(available <= (size_t)this->lb_buffer_max);

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
            this->resize_buffer(this->lb_buffer_max +
                                DEFAULT_LINE_BUFFER_SIZE);
        }
    }
}

bool line_buffer::fill_range(off_t start, size_t max_length)
throw (error)
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
                lock_hack::guard guard;

                lseek(this->lb_fd, this->lb_gz_offset, SEEK_SET);
                gzseek(this->lb_gz_file,
                       this->lb_file_offset + this->lb_buffer_size,
                       SEEK_SET);
                rc = gzread(this->lb_gz_file,
                            &this->lb_buffer[this->lb_buffer_size],
                            this->lb_buffer_max - this->lb_buffer_size);
                this->lb_gz_offset = lseek(this->lb_fd, 0, SEEK_CUR);
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

bool line_buffer::read_line(off_t &offset, line_value &lv, bool include_delim)
throw (error)
{
    size_t request_size = DEFAULT_INCREMENT;
    bool retval = false;

    require(this->lb_fd != -1);

    if (this->lb_last_line_offset != -1 && offset >
        this->lb_last_line_offset) {
        /*
         * Don't return anything past the last known line.  The caller needs
         * to try reading at the offset of the last line again.
         */
        return false;
    }

    lv.lv_len = 0;
    lv.lv_partial = false;
    while (!retval) {
        char *line_start, *lf;

        this->fill_range(offset, request_size);

        /* Find the data in the cache and */
        line_start = this->get_range(offset, lv.lv_len);
        /* ... look for the end-of-line or end-of-file. */
        if (((lf = (char *)memchr(line_start, '\n', lv.lv_len)) != NULL) ||
            (lv.lv_len >= MAX_LINE_BUFFER_SIZE) ||
            (request_size == MAX_LINE_BUFFER_SIZE) ||
            ((request_size > lv.lv_len) && lv.lv_len > 0)) {
            if ((lf != NULL) &&
                ((size_t) (lf - line_start) >= MAX_LINE_BUFFER_SIZE - 1)) {
                lf = NULL;
            }
            if (lf != NULL) {
                lv.lv_partial = false;
                lv.lv_len = lf - line_start;
                if (include_delim) {
                    lv.lv_len += 1;
                }
                else {
                    offset += 1; /* Skip the delimiter. */
                }
                if (offset >= this->lb_last_line_offset) {
                    this->lb_last_line_offset = offset + lv.lv_len;
                }
            }
            else {
                if (lv.lv_len >= MAX_LINE_BUFFER_SIZE) {
                    log_warning("Line exceeded max size: offset=%d",
                                offset);
                    lv.lv_len = MAX_LINE_BUFFER_SIZE - 1;
                    lv.lv_partial = false;
                }
                else {
                    lv.lv_partial = true;
                }
                /*
                 * Be nice and make sure there is room for the caller to
                 * add a NULL-terminator.
                 */
                this->ensure_available(offset, lv.lv_len + 1);
                line_start = this->get_range(offset, lv.lv_len);

                if (lv.lv_len >= MAX_LINE_BUFFER_SIZE) {
                    lv.lv_len = MAX_LINE_BUFFER_SIZE - 1;
                }
                if (lv.lv_partial) {
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
                    this->lb_last_line_offset = offset + lv.lv_len;
                }
            }

            lv.lv_start = line_start;
            offset += lv.lv_len;

            retval = true;
        }
        else {
            request_size += DEFAULT_INCREMENT;
        }

        if (!retval && !this->fill_range(offset, request_size)) {
            break;
        }
    }

    ensure(lv.lv_len <= (size_t)this->lb_buffer_size);
    ensure(!retval ||
           (lv.lv_start >= this->lb_buffer &&
            (lv.lv_start + lv.lv_len) <= (this->lb_buffer + this->lb_buffer_size)));
    ensure(this->invariant());

    return retval;
}

bool line_buffer::read_line(off_t &offset_inout, shared_buffer_ref &sbr, line_value *lv)
    throw (error)
{
    line_value lv_tmp;
    bool retval;

    if (lv == NULL) {
        lv = &lv_tmp;
    }

    // Clear the incoming ref right away so that an invalidate
    // does not cause a wasted malloc/copy.
    sbr.disown();
    if ((retval = this->read_line(offset_inout, *lv))) {
        sbr.share(this->lb_share_manager, lv->lv_start, lv->lv_len);
    }

    return retval;
}

bool line_buffer::read_range(off_t offset, size_t len, shared_buffer_ref &sbr)
    throw (error)
{
    char *line_start;
    size_t avail;

    sbr.disown();

    if (this->lb_last_line_offset != -1 && offset > this->lb_last_line_offset) {
        /*
         * Don't return anything past the last known line.  The caller needs
         * to try reading at the offset of the last line again.
         */
        return false;
    }

    this->fill_range(offset, len);
    line_start = this->get_range(offset, avail);

    sbr.share(this->lb_share_manager, line_start, len);

    return true;
}
