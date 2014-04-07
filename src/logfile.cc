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
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <time.h>

#include "logfile.hh"
#include "lnav_util.hh"

using namespace std;

static const size_t MAX_UNRECOGNIZED_LINES = 1000;

logfile::logfile(string filename, auto_fd fd)
throw (error)
    : lf_filename(filename),
      lf_index_time(0),
      lf_index_size(0),
      lf_is_closed(false)
{
    int reserve_size = 100;

    require(filename.size() > 0);

    this->lf_time_offset.tv_sec = 0;
    this->lf_time_offset.tv_usec = 0;
    
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

        if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
            throw error(filename, errno);
        }

        this->lf_valid_filename = true;
    }
    else {
        fstat(fd, &this->lf_stat);
        this->lf_valid_filename = false;
    }

    this->lf_content_id = hash_string(this->lf_filename);
    this->lf_line_buffer.set_fd(fd);
    this->lf_index.reserve(reserve_size);

    ensure(this->invariant());
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

void logfile::set_format_base_time(log_format *lf)
{
    time_t file_time = this->lf_line_buffer.get_file_time();

    if (file_time == 0) {
        file_time = this->lf_stat.st_mtime;
    }
    lf->lf_date_time.set_base_time(file_time);
}

void logfile::process_prefix(off_t offset, char *prefix, int len)
{
    bool found = false;

    if (this->lf_format.get() != NULL) {
        /* We've locked onto a format, just use that scanner. */
        this->set_format_base_time(this->lf_format.get());
        found = this->lf_format->scan(this->lf_index, offset, prefix, len);
    }
    else if (this->lf_index.size() < MAX_UNRECOGNIZED_LINES) {
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
            if (!(*iter)->match_name(this->lf_filename)) {
                continue;
            }

            (*iter)->clear();
            this->set_format_base_time(*iter);
            if (!(*iter)->scan(this->lf_index, offset, prefix, len)) {
                log_debug("%s:%d:log format does not match -- %s",
                    this->lf_filename.c_str(),
                    this->lf_index.size(),
                    (*iter)->get_name().c_str());
            }
            else {
#if 0
                require(this->lf_index.size() == 1 ||
                       (this->lf_index[this->lf_index.size() - 2] <
                        this->lf_index[this->lf_index.size() - 1]));
#endif
                log_debug("%s:%d:log format found -- %s",
                    this->lf_filename.c_str(),
                    this->lf_index.size(),
                    (*iter)->get_name().c_str());

                this->lf_format =
                    auto_ptr<log_format>((*iter)->specialized());
                this->lf_content_id = hash_string(string(prefix, len));
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
throw (line_buffer::error, logfile::error)
{
    bool        retval = false;
    struct stat st;

    if (fstat(this->lf_line_buffer.get_fd(), &st) == -1) {
        throw error(this->lf_filename, errno);
    }

    /* Check for new data based on the file size. */
    if (this->lf_index_size < st.st_size) {
        off_t  last_off, off;
        line_value lv;

        if (!this->lf_index.empty()) {
            off = this->lf_index.back().get_offset();

            /*
             * Drop the last line we read since it might have been a partial
             * read.
             */
            while (this->lf_index.back().get_sub_offset() != 0) {
                this->lf_index.pop_back();
            }
            this->lf_index.pop_back();
        }
        else {
            off = 0;
        }
        last_off = off;
        while (this->lf_line_buffer.read_line(off, lv)) {
            char tmp = lv.lv_start[lv.lv_len];

            this->lf_partial_line = lv.lv_partial;
            lv.lv_start[lv.lv_len] = '\0';
            this->process_prefix(last_off, lv.lv_start, lv.lv_len);
            lv.lv_start[lv.lv_len] = tmp;
            last_off = off;

            if (lo != NULL) {
                lo->logfile_indexing(
                    *this,
                    this->lf_line_buffer.get_read_offset(off),
                    st.st_size);
            }
        }

        /*
         * The file can still grow between the above fstat and when we're
         * doing the scanning, so use the line buffer's notion of the file
         * size.
         */
        this->lf_index_size = off;

        // this->lf_line_buffer.invalidate();

        retval = true;
    }

    this->lf_index_time = this->lf_line_buffer.get_file_time();
    if (!this->lf_index_time) {
        this->lf_index_time = st.st_mtime;
    }

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

    string line_value;

    for (size_t lpc = 0; lpc < filters.size(); lpc++) {
        logfile_filter *filter = filters[lpc];
        bool matched;

        if (!filter->is_enabled())
            continue;

        if (line_value.empty())
            this->read_line(ll, line_value);
        matched = filter->matches(*ll, line_value);

        switch (filter->get_type()) {
        case logfile_filter::INCLUDE:
            if (matched) {
                // Always prefer including something.
                retval = logfile_filter::INCLUDE;
            }
            else if (retval == logfile_filter::MAYBE) {
                // Only exclude if we haven't made a decision yet.
                retval = logfile_filter::EXCLUDE;
            }
            break;

        case logfile_filter::EXCLUDE:
            if (matched && retval == logfile_filter::MAYBE) {
                // Only exclude if we haven't decide to include the line.
                retval = logfile_filter::EXCLUDE;
            }
            break;

        default:
            require(0);
            break;
        }
    }

    ll->set_filter_state(generation, retval);

    return retval;
}

void logfile::read_line(logfile::iterator ll, string &line_out)
{
    try {
        off_t       off = ll->get_offset();
        shared_buffer_ref sbr;

        line_out.clear();
        if (this->lf_line_buffer.read_line(off, sbr)) {
            ostringstream stream;

            if (this->lf_format.get() != NULL) {
                this->lf_format->get_subline(*ll, sbr);

            }
            line_out.append(sbr.get_data(), sbr.length());
        }
        else {
            /* XXX */
        }
    }
    catch (line_buffer::error & e) {
        /* ... */
    }
}

bool logfile::read_line(logfile::iterator ll, shared_buffer_ref &sbr)
{
    try {
        off_t       off = ll->get_offset();

        if (this->lf_line_buffer.read_line(off, sbr)) {
            if (this->lf_format.get() != NULL) {
                this->lf_format->get_subline(*ll, sbr);
            }
            return true;
        }
    }
    catch (line_buffer::error & e) {
    }
    return false;
}

void logfile::read_full_message(logfile::iterator ll,
                                string &msg_out,
                                int max_lines)
{
    ostringstream stream;

    do {
        try {
            off_t       off = ll->get_offset();
            shared_buffer_ref sbr;

            if (stream.tellp() > 0) {
                stream.write("\n", 1);
            }
            if (this->lf_line_buffer.read_line(off, sbr)) {
                this->lf_format->get_subline(*ll, sbr);
                stream.write(sbr.get_data(), sbr.length());
            }
            else {
                /* XXX */
            }
        }
        catch (line_buffer::error & e) {
            /* ... */
        }
        ++ll;
        max_lines -= 1;
    } while (ll != this->end() && ll->is_continued() &&
             (max_lines == -1 || max_lines > 0));

    msg_out = stream.str();
}

void logfile::read_full_message(logfile::iterator ll,
                                shared_buffer_ref &msg_out,
                                int max_lines)
{
    require(ll->get_sub_offset() == 0);

    size_t line_len = this->line_length(ll);

    try {
        off_t       off = ll->get_offset();

        if (this->lf_line_buffer.read_range(off, line_len, msg_out)) {
            if (this->lf_format.get() != NULL) {
                this->lf_format->get_subline(*ll, msg_out);
            }
        }
        else {
                /* XXX */
        }
    }
    catch (line_buffer::error & e) {
        /* ... */
    }
}
