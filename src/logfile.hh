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

#include <string>
#include <vector>
#include <algorithm>

#include "line_buffer.hh"
#include "log_format.hh"

class logfile;

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

/**
 * Container for the lines in a log file and some metadata.
 */
class logfile {
public:

    class error {
public:
        error(std::string filename, int err)
            : e_filename(filename),
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
    logfile(std::string filename, auto_fd fd = -1) throw (error);

    virtual ~logfile();

    /** @return The filename as given in the constructor. */
    const std::string &get_filename() const { return this->lf_filename; };

    /** @param filename The new filename for this log file. */
    void set_filename(const std::string &filename)
    {
        this->lf_filename =
            filename;
    };

    /** @return The inode for this log file. */
    const struct stat &get_stat() const { return this->lf_stat; };

    /**
     * @return The detected format, rebuild_index() must be called before this
     * will return a value other than NULL.
     */
    log_format *get_format() { return this->lf_format.get(); };

    /**
     * @return The last modified time of the file when the file was last
     * indexed.
     */
    time_t get_modified_time() const { return this->lf_index_time; };

    iterator begin() { return this->lf_index.begin(); }

    const_iterator begin() const { return this->lf_index.begin(); }

    iterator end() { return this->lf_index.end(); }

    const_iterator end() const { return this->lf_index.end(); }

    /** @return The number of lines in the index. */
    size_t size() { return this->lf_index.size(); }

    logline &operator[](int index) { return this->lf_index[index]; };

    /** @return True if this log file still exists. */
    bool exists() const;

    /**
     * Read a line from the file.
     *
     * @param ll The line to read.
     * @param line_out Storage to hold the line itself.
     */
    void read_line(iterator ll, std::string &line_out);

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

    /**
     * Index any new data in the log file.
     *
     * @param lo The observer object that will be called regularly during
     * indexing.
     * @return True if any new lines were indexed.
     */
    bool rebuild_index(logfile_observer *lo = NULL) throw (line_buffer::error);

    /**
     * Check a line to see if it should be filtered out or not.
     *
     * XXX This code doesn't really belong here, it's something more
     * approprtiate for the logfile_sub_source.  The reason it is here right
     * now is because we cache the result of the check in the logline object.
     * 
     * @param  ll         The log line to check.
     * @param  generation The "generation" that the given filters belong to.
     *                    If the cached value for this check is from the same
     *                    filter generation, then we just return the cached
     *                    value.
     * @param  filters    The filters to apply.
     * @return            Whether or not the line should be included in the
     *                    view.
     */
    logfile_filter::type_t check_filter(iterator ll,
                                        uint8_t generation,
                                        const filter_stack_t &filters);

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
    bool invariant(void)
    {
        assert(this->lf_filename.size() > 0);

        return true;
    };

protected:

    /**
     * Process a line from the file.
     *
     * @param offset The offset of the line in the file.
     * @param prefix The contents of the line.
     * @param len The length of the 'prefix' string.
     */
    void process_prefix(off_t offset, char *prefix, int len);

    bool        lf_valid_filename;
    std::string lf_filename;
    struct stat lf_stat;
    std::auto_ptr<log_format> lf_format;
    std::vector<logline>      lf_index;
    time_t      lf_index_time;
    off_t       lf_index_size;
    line_buffer lf_line_buffer;
};
#endif
