#include <utility>

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
 * @file logfile.hh
 */

#ifndef __logfile_hh
#define __logfile_hh

#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>

#include <string>
#include <vector>
#include <algorithm>

#include "lnav_log.hh"
#include "byte_array.hh"
#include "line_buffer.hh"
#include "log_format.hh"
#include "unique_path.hh"
#include "text_format.hh"
#include "shared_buffer.hh"
#include "filesystem/path.h"

class logfile;
class logline_observer;

/**
 * Observer interface for logfile indexing progress.
 *
 * @see logfile
 */
class logfile_observer {
public:
    virtual ~logfile_observer() { };

    /**
     * @param lf The logfile object that is doing the indexing.
     * @param off The current offset in the file being processed.
     * @param total The total size of the file.
     */
    virtual void logfile_indexing(logfile &lf, off_t off, size_t total) = 0;
};

struct logfile_open_options {
    logfile_open_options() : loo_detect_format(true) {
    };

    logfile_open_options &with_fd(auto_fd fd) {
        this->loo_fd = fd;

        return *this;
    };

    logfile_open_options &with_detect_format(bool val) {
        this->loo_detect_format = val;

        return *this;
    };

    auto_fd loo_fd;
    bool loo_detect_format;
};

struct logfile_activity {
    logfile_activity() {
        memset(this, 0, sizeof(*this));
    };

    int64_t la_polls;
    int64_t la_reads;
    struct rusage la_initial_index_rusage;
};

/**
 * Container for the lines in a log file and some metadata.
 */
class logfile : public unique_path_source {
public:

    class error {
public:
        error(std::string filename, int err)
            : e_filename(std::move(filename)),
              e_err(err) { };

        std::string e_filename;
        int         e_err;
    };

    typedef std::vector<logline>::iterator       iterator;
    typedef std::vector<logline>::const_iterator const_iterator;

    /**
     * Construct a logfile with the given arguments.
     *
     * @param filename The name of the log file.
     * @param fd The file descriptor for accessing the file or -1 if the
     * constructor should open the file specified by 'filename'.  The
     * descriptor needs to be seekable.
     */
    logfile(const std::string &filename, logfile_open_options &loo);

    virtual ~logfile();

    const logfile_activity &get_activity() const {
        return this->lf_activity;
    };

    /** @return The filename as given in the constructor. */
    const std::string &get_filename() const { return this->lf_filename; };

    /** @return The filename as given in the constructor, excluding the path prefix. */
    const std::string &get_basename() const { return this->lf_basename; };

    int get_fd() const { return this->lf_line_buffer.get_fd(); };

    /** @param filename The new filename for this log file. */
    void set_filename(const std::string &filename)
    {
        this->lf_filename = filename;
        filesystem::path p(filename);
        this->lf_basename = p.filename();
    };

    const std::string &get_content_id() const { return this->lf_content_id; };

    /** @return The inode for this log file. */
    const struct stat &get_stat() const { return this->lf_stat; };

    size_t get_longest_line_length() const {
        return this->lf_longest_line;
    }

    bool is_compressed() const {
        return this->lf_line_buffer.is_compressed();
    };

    bool is_valid_filename() const {
        return this->lf_valid_filename;
    };

    /**
     * @return The detected format, rebuild_index() must be called before this
     * will return a value other than NULL.
     */
    log_format *get_format() const { return this->lf_format.get(); };

    text_format_t get_text_format() const {
        return this->lf_text_format;
    }

    /**
     * @return The last modified time of the file when the file was last
     * indexed.
     */
    time_t get_modified_time() const { return this->lf_index_time; };

    int get_time_offset_line() const {
        return this->lf_time_offset_line;
    };

    const struct timeval &get_time_offset() const {
        return this->lf_time_offset;
    };

    void adjust_content_time(int line,
                             const struct timeval &tv,
                             bool abs_offset=true) {
        struct timeval old_time = this->lf_time_offset;

        this->lf_time_offset_line = line;
        if (abs_offset) {
            this->lf_time_offset = tv;
        }
        else {
            timeradd(&old_time, &tv, &this->lf_time_offset);
        }
        for (auto &iter : *this) {
            struct timeval curr, diff, new_time;

            curr = iter.get_timeval();
            timersub(&curr, &old_time, &diff);
            timeradd(&diff, &this->lf_time_offset, &new_time);
            iter.set_time(new_time);
        }
        this->lf_sort_needed = true;
    };

    void clear_time_offset() {
        struct timeval tv = { 0, 0 };

        this->adjust_content_time(-1, tv);
    };

    bool is_time_adjusted() const {
        return (this->lf_time_offset.tv_sec != 0 ||
                this->lf_time_offset.tv_usec != 0);
    }

    iterator begin() { return this->lf_index.begin(); }

    const_iterator begin() const { return this->lf_index.begin(); }

    iterator end() { return this->lf_index.end(); }

    const_iterator end() const { return this->lf_index.end(); }

    /** @return The number of lines in the index. */
    size_t size() const { return this->lf_index.size(); }

    logline &operator[](int index) { return this->lf_index[index]; };

    logline &back() {
        return this->lf_index.back();
    };

    /** @return True if this log file still exists. */
    bool exists(void) const;

    void close() {
        this->lf_is_closed = true;
    };

    bool is_closed() const {
        return this->lf_is_closed;
    };

    struct timeval original_line_time(iterator ll) {
        if (this->is_time_adjusted()) {
            struct timeval line_time = ll->get_timeval();
            struct timeval retval;

            timersub(&line_time, &this->lf_time_offset, &retval);
            return retval;
        }

        return ll->get_timeval();
    };

    /**
     * Read a line from the file.
     *
     * @param ll The line to read.
     * @param line_out Storage to hold the line itself.
     */
    void read_line(iterator ll, std::string &line_out);

    bool read_line(iterator ll, shared_buffer_ref &sbr);

    /**
     * Read a line from the file.
     *
     * @param ll The line to read.
     * @return The contents of the line as a string.
     */
    std::string read_line(iterator ll)
    {
        std::string retval;

        this->read_line(ll, retval);

        return retval;
    };

    iterator line_base(iterator ll) {
        iterator retval = ll;

        while (retval != this->begin() && retval->get_sub_offset() != 0) {
            --retval;
        }

        return retval;
    };

    iterator message_start(iterator ll) {
        iterator retval = ll;

        while (retval != this->begin() &&
                (retval->get_sub_offset() != 0 || retval->is_continued())) {
            --retval;
        }

        return retval;
    }

    size_t line_length(iterator ll, bool include_continues = true) {
        iterator next_line = ll;
        size_t retval;

        do {
            ++next_line;
        } while ((next_line != this->end()) &&
                 include_continues &&
                 ((ll->get_offset() == next_line->get_offset()) ||
                  next_line->is_continued()));

        if (next_line == this->end()) {
            retval = this->lf_index_size - ll->get_offset();
            if (retval > 0 && !this->lf_partial_line) {
                retval -= 1;
            }
        }
        else {
            retval = next_line->get_offset() - ll->get_offset() - 1;
        }

        if (retval > line_buffer::MAX_LINE_BUFFER_SIZE / 2) {
            retval = line_buffer::MAX_LINE_BUFFER_SIZE / 2;
        }

        return retval;
    };

    void read_full_message(iterator ll, std::string &msg_out, int max_lines=50);

    void read_full_message(iterator ll, shared_buffer_ref &msg_out, int max_lines=50);

    enum rebuild_result_t {
        RR_NO_NEW_LINES,
        RR_NEW_LINES,
        RR_NEW_ORDER,
    };

    /**
     * Index any new data in the log file.
     *
     * @param lo The observer object that will be called regularly during
     * indexing.
     * @return True if any new lines were indexed.
     */
    rebuild_result_t rebuild_index();

    void reobserve_from(iterator iter);

    void set_logfile_observer(logfile_observer *lo) {
        this->lf_logfile_observer = lo;
    };

    void set_logline_observer(logline_observer *llo);

    logline_observer *get_logline_observer() const {
        return this->lf_logline_observer;
    };

    bool operator<(const logfile &rhs) const
    {
        bool retval;

        if (this->lf_index.empty()) {
            retval = true;
        }
        else if (rhs.lf_index.empty()) {
            retval = false;
        }
        else {
            retval = this->lf_index[0].get_time() < rhs.lf_index[0].get_time();
        }

        return retval;
    };

    /** Check the invariants for this object. */
    bool invariant()
    {
        require(!this->lf_filename.empty());

        return true;
    }

    filesystem::path get_path() const override;

protected:

    /**
     * Process a line from the file.
     *
     * @param offset The offset of the line in the file.
     * @param prefix The contents of the line.
     * @param len The length of the 'prefix' string.
     */
    bool process_prefix(off_t offset, shared_buffer_ref &sbr);

    void set_format_base_time(log_format *lf);

    logfile_open_options lf_options;
    logfile_activity lf_activity;
    bool        lf_valid_filename;
    std::string lf_filename;
    std::string lf_basename;
    std::string lf_content_id;
    struct stat lf_stat;
    std::unique_ptr<log_format> lf_format;
    std::vector<logline>      lf_index;
    time_t      lf_index_time{0};
    off_t       lf_index_size{0};
    bool lf_sort_needed{false};
    line_buffer lf_line_buffer;
    int lf_time_offset_line{0};
    struct timeval lf_time_offset{0, 0};
    bool lf_is_closed{false};
    bool lf_partial_line{false};
    logline_observer *lf_logline_observer{nullptr};
    logfile_observer *lf_logfile_observer{nullptr};
    size_t lf_longest_line{0};
    text_format_t lf_text_format{TF_UNKNOWN};
    uint32_t lf_out_of_time_order_count{0};
};

class logline_observer {
public:
    virtual ~logline_observer() { };

    virtual void logline_restart(const logfile &lf) = 0;

    virtual void logline_new_line(const logfile &lf, logfile::const_iterator ll, shared_buffer_ref &sbr) = 0;

    virtual void logline_eof(const logfile &lf) = 0;
};

#endif
