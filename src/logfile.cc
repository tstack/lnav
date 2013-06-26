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
 * @file logfile.cc
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <time.h>

#include "logfile.hh"

using namespace std;

logfile::logfile(string filename, auto_fd fd)
throw (error)
    : lf_filename(filename),
      lf_index_time(0),
      lf_index_size(0)
{
    int reserve_size = 100;

    assert(filename.size() > 0);

    memset(&this->lf_stat, 0, sizeof(this->lf_stat));
    if (fd == -1) {
        char resolved_path[PATH_MAX];

        errno = 0;
        if (realpath(filename.c_str(), resolved_path) == NULL) {
            throw error(resolved_path, errno);
        }
        filename = resolved_path;

        if (stat(filename.c_str(), &this->lf_stat) == -1) {
            throw error(filename, errno);
        }
        reserve_size = this->lf_stat.st_size / 100;

        if (!S_ISREG(this->lf_stat.st_mode)) {
            throw error(filename, EINVAL);
        }

        if ((fd = open(filename.c_str(), O_RDONLY)) == -1) {
            throw error(filename, errno);
        }

        this->lf_valid_filename = true;
    }
    else {
        fstat(fd, &this->lf_stat);
        this->lf_valid_filename = false;
    }

    this->lf_line_buffer.set_fd(fd);
    this->lf_index.reserve(reserve_size);

    assert(this->invariant());
}

logfile::~logfile()
{ }

bool logfile::exists(void) const
{
    struct stat st;

    if (!this->lf_valid_filename) {
        return true;
    }

    if (::stat(this->lf_filename.c_str(), &st) == -1) {
        return false;
    }

    return this->lf_stat.st_dev == st.st_dev &&
           this->lf_stat.st_ino == st.st_ino;
}

void logfile::process_prefix(off_t offset, char *prefix, int len)
{
    bool found = false;

    if (this->lf_format.get() != NULL) {
        /* We've locked onto a format, just use that scanner. */
        found = this->lf_format->scan(this->lf_index, offset, prefix, len);
    }
    else {
        vector<log_format *> &root_formats =
            log_format::get_root_formats();
        vector<log_format *>::iterator iter;

        /*
         * Try each scanner until we get a match.  Fortunately, all the formats
         * are sufficiently different that there are no ambiguities...
         */
        for (iter = root_formats.begin();
             iter != root_formats.end() && !found;
             ++iter) {
            (*iter)->clear();
            if ((*iter)->scan(this->lf_index, offset, prefix, len)) {
#if 0
                assert(this->lf_index.size() == 1 ||
                       (this->lf_index[this->lf_index.size() - 2] <
                        this->lf_index[this->lf_index.size() - 1]));
#endif

                this->lf_format =
                    auto_ptr<log_format>((*iter)->specialized());
                found = true;

                /*
                 * We'll go ahead and assume that any previous lines were
                 * written out at the same time as the last one, so we need to
                 * go back and update everything.
                 */
                logline &last_line = this->lf_index[this->lf_index.size() - 1];

                for (size_t lpc = 0; lpc < this->lf_index.size() - 1; lpc++) {
                    this->lf_index[lpc].set_time(last_line.get_time());
                    this->lf_index[lpc].set_millis(last_line.get_millis());
                }
            }
        }
    }

    if (found) {
        if (this->lf_index.size() >= 2) {
            logline &second_to_last = this->lf_index[this->lf_index.size() - 2];
            logline &latest = this->lf_index.back();

            if (latest < second_to_last) {
                latest.set_time(second_to_last.get_time());
                latest.set_millis(second_to_last.get_millis());
            }
        }
    }

    /* If the scanner didn't match, than we need to add it. */
    if (!found) {
        logline::level_t last_level  = logline::LEVEL_UNKNOWN;
        time_t           last_time   = this->lf_index_time;
        short            last_millis = 0;

        if (!this->lf_index.empty()) {
            logline &ll = this->lf_index.back();

            /*
             * Assume this line is part of the previous one(s) and copy the
             * metadata over.
             */
            ll.set_multiline();
            last_time   = ll.get_time();
            last_millis = ll.get_millis();
            if (this->lf_format.get() != NULL) {
                last_level = (logline::level_t)
                             (ll.get_level() | logline::LEVEL_CONTINUED);
            }
        }
        this->lf_index.push_back(logline(offset,
                                         last_time,
                                         last_millis,
                                         last_level));
    }
}

bool logfile::rebuild_index(logfile_observer *lo)
throw (line_buffer::error)
{
    bool        retval = false;
    struct stat st;

    if (fstat(this->lf_line_buffer.get_fd(), &st) == -1) {
        throw error(this->lf_filename, errno);
    }

    /* Check for new data based on the file size. */
    if (this->lf_index_size < st.st_size) {
        off_t  last_off, off;
        char * line;
        size_t len;

        if (!this->lf_index.empty()) {
            off = this->lf_index.back().get_offset();

            /*
             * Drop the last line we read since it might have been a partial
             * read.
             */
            this->lf_index.pop_back();
        }
        else {
            off = 0;
        }
        last_off = off;
        while ((line = this->lf_line_buffer.read_line(off, len)) != NULL) {
            line[len] = '\0';
            this->process_prefix(last_off, line, len);
            last_off = off;

            if (lo != NULL) {
                lo->logfile_indexing(*this,
                                     this->lf_line_buffer.get_read_offset(off),
                                     st.st_size);
            }
        }

        /*
         * The file can still grow between the above fstat and when we're
         * doing the scanning, so use the line buffer's notion of the file
         * size.
         */
        this->lf_index_size = this->lf_line_buffer.get_file_size();

        this->lf_line_buffer.invalidate();

        retval = true;
    }

    this->lf_index_time = st.st_mtime;

    return retval;
}

logfile_filter::type_t logfile::check_filter(iterator ll,
                                             uint8_t generation,
                                             const filter_stack_t &filters)
{
    logfile_filter::type_t retval;
    uint8_t this_generation = ll->get_filter_generation();

    if (this_generation == generation) {
        return ll->get_filter_state();
    }
    else {
        retval = logfile_filter::MAYBE;
    }

    if (retval == logfile_filter::MAYBE) {
        string line_value;

        for (size_t lpc = 0; lpc < filters.size(); lpc++) {
            logfile_filter *filter = filters[lpc];
            bool matched;

            if (!filter->is_enabled())
                continue;

            if (line_value.empty())
                this->read_line(ll, line_value);
            matched = filter->matches(line_value);

            switch (filter->get_type()) {
            case logfile_filter::INCLUDE:
                if (matched) {
                    retval = logfile_filter::INCLUDE;
                }
                else if (retval == logfile_filter::MAYBE) {
                    retval = logfile_filter::EXCLUDE;
                }
                break;

            case logfile_filter::EXCLUDE:
                if (matched) {
                    retval = logfile_filter::EXCLUDE;
                }
                break;

            default:
                assert(0);
                break;
            }
        }
    }

    if (retval == logfile_filter::MAYBE) {
        retval = logfile_filter::INCLUDE;
    }

    ll->set_filter_state(generation, retval);

    return retval;
}

void logfile::read_line(logfile::iterator ll, string &line_out)
{
    try {
        off_t       off = ll->get_offset();
        const char *line;
        size_t      len;

        line_out.clear();
        if ((line = this->lf_line_buffer.read_line(off, len)) != NULL) {
            line_out.append(line, len);
        }
        else {
            /* XXX */
        }
    }
    catch (line_buffer::error & e) {
        /* ... */
    }
}
