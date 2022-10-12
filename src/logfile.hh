
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
 * @file logfile.hh
 */

#ifndef logfile_hh
#define logfile_hh

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <stdint.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ArenaAlloc/arenaalloc.h"
#include "base/lnav_log.hh"
#include "base/result.h"
#include "bookmarks.hh"
#include "byte_array.hh"
#include "ghc/filesystem.hpp"
#include "line_buffer.hh"
#include "log_format_fwd.hh"
#include "logfile_fwd.hh"
#include "safe/safe.h"
#include "shared_buffer.hh"
#include "text_format.hh"
#include "unique_path.hh"

/**
 * Observer interface for logfile indexing progress.
 *
 * @see logfile
 */
class logfile_observer {
public:
    virtual ~logfile_observer() = default;

    enum class indexing_result {
        CONTINUE,
        BREAK,
    };

    /**
     * @param lf The logfile object that is doing the indexing.
     * @param off The current offset in the file being processed.
     * @param total The total size of the file.
     * @return false
     */
    virtual indexing_result logfile_indexing(const std::shared_ptr<logfile>& lf,
                                             file_off_t off,
                                             file_size_t total)
        = 0;
};

struct logfile_activity {
    int64_t la_polls{0};
    int64_t la_reads{0};
    struct rusage la_initial_index_rusage {};
};

/**
 * Container for the lines in a log file and some metadata.
 */
class logfile
    : public unique_path_source
    , public std::enable_shared_from_this<logfile> {
public:
    using iterator = std::vector<logline>::iterator;
    using const_iterator = std::vector<logline>::const_iterator;

    struct metadata {
        text_format_t m_format;
        std::string m_value;
    };

    /**
     * Construct a logfile with the given arguments.
     *
     * @param filename The name of the log file.
     * @param fd The file descriptor for accessing the file or -1 if the
     * constructor should open the file specified by 'filename'.  The
     * descriptor needs to be seekable.
     */
    static Result<std::shared_ptr<logfile>, std::string> open(
        std::string filename, logfile_open_options& loo);

    ~logfile() override;

    const logfile_activity& get_activity() const { return this->lf_activity; }

    nonstd::optional<ghc::filesystem::path> get_actual_path() const
    {
        return this->lf_actual_path;
    }

    /** @return The filename as given in the constructor. */
    const std::string& get_filename() const { return this->lf_filename; }

    /** @return The filename as given in the constructor, excluding the path
     * prefix. */
    const std::string& get_basename() const { return this->lf_basename; }

    int get_fd() const { return this->lf_line_buffer.get_fd(); }

    /** @param filename The new filename for this log file. */
    void set_filename(const std::string& filename);

    const std::string& get_content_id() const { return this->lf_content_id; }

    /** @return The inode for this log file. */
    const struct stat& get_stat() const { return this->lf_stat; }

    size_t get_longest_line_length() const { return this->lf_longest_line; }

    bool is_compressed() const { return this->lf_line_buffer.is_compressed(); }

    bool is_valid_filename() const { return this->lf_valid_filename; }

    file_off_t get_index_size() const { return this->lf_index_size; }

    nonstd::optional<const_iterator> line_for_offset(file_off_t off) const;

    /**
     * @return The detected format, rebuild_index() must be called before this
     * will return a value other than NULL.
     */
    std::shared_ptr<log_format> get_format() const { return this->lf_format; }

    log_format* get_format_ptr() const { return this->lf_format.get(); }

    intern_string_t get_format_name() const;

    text_format_t get_text_format() const { return this->lf_text_format; }

    /**
     * @return The last modified time of the file when the file was last
     * indexed.
     */
    time_t get_modified_time() const { return this->lf_index_time; }

    int get_time_offset_line() const { return this->lf_time_offset_line; }

    const struct timeval& get_time_offset() const
    {
        return this->lf_time_offset;
    }

    void adjust_content_time(int line,
                             const struct timeval& tv,
                             bool abs_offset = true);

    void clear_time_offset()
    {
        struct timeval tv = {0, 0};

        this->adjust_content_time(-1, tv);
    }

    void mark_as_duplicate(const std::string& name);

    const logfile_open_options& get_open_options() const
    {
        return this->lf_options;
    }

    void reset_state();

    bool is_time_adjusted() const
    {
        return (this->lf_time_offset.tv_sec != 0
                || this->lf_time_offset.tv_usec != 0);
    }

    iterator begin() { return this->lf_index.begin(); }

    const_iterator begin() const { return this->lf_index.begin(); }

    const_iterator cbegin() const { return this->lf_index.begin(); }

    iterator end() { return this->lf_index.end(); }

    const_iterator end() const { return this->lf_index.end(); }

    const_iterator cend() const { return this->lf_index.end(); }

    /** @return The number of lines in the index. */
    size_t size() const { return this->lf_index.size(); }

    nonstd::optional<const_iterator> find_from_time(
        const struct timeval& tv) const;

    logline& operator[](int index) { return this->lf_index[index]; }

    logline& front() { return this->lf_index.front(); }

    logline& back() { return this->lf_index.back(); }

    /** @return True if this log file still exists. */
    bool exists() const;

    void close() { this->lf_is_closed = true; }

    bool is_closed() const { return this->lf_is_closed; }

    struct timeval original_line_time(iterator ll);

    Result<shared_buffer_ref, std::string> read_line(iterator ll);

    Result<std::string, std::string> read_file();

    iterator line_base(iterator ll)
    {
        auto retval = ll;

        while (retval != this->begin() && retval->get_sub_offset() != 0) {
            --retval;
        }

        return retval;
    }

    iterator message_start(iterator ll)
    {
        auto retval = ll;

        while (retval != this->begin()
               && (retval->get_sub_offset() != 0 || !retval->is_message()))
        {
            --retval;
        }

        return retval;
    }

    struct message_length_result {
        file_ssize_t mlr_length;
        file_range::metadata mlr_metadata;
    };

    message_length_result message_byte_length(const_iterator ll,
                                              bool include_continues = true);

    file_range get_file_range(const_iterator ll, bool include_continues = true)
    {
        auto mlr = this->message_byte_length(ll, include_continues);

        return {
            ll->get_offset(),
            mlr.mlr_length,
            mlr.mlr_metadata,
        };
    }

    void read_full_message(const_iterator ll,
                           shared_buffer_ref& msg_out,
                           int max_lines = 50);

    Result<shared_buffer_ref, std::string> read_raw_message(const_iterator ll);

    enum class rebuild_result_t {
        INVALID,
        NO_NEW_LINES,
        NEW_LINES,
        NEW_ORDER,
    };

    /**
     * Index any new data in the log file.
     *
     * @param lo The observer object that will be called regularly during
     * indexing.
     * @return True if any new lines were indexed.
     */
    rebuild_result_t rebuild_index(
        nonstd::optional<ui_clock::time_point> deadline = nonstd::nullopt);

    void reobserve_from(iterator iter);

    void set_logfile_observer(logfile_observer* lo)
    {
        this->lf_logfile_observer = lo;
    }

    void set_logline_observer(logline_observer* llo);

    logline_observer* get_logline_observer() const
    {
        return this->lf_logline_observer;
    }

    bool operator<(const logfile& rhs) const
    {
        bool retval;

        if (this->lf_index.empty()) {
            retval = true;
        } else if (rhs.lf_index.empty()) {
            retval = false;
        } else {
            retval = this->lf_index[0].get_time() < rhs.lf_index[0].get_time();
        }

        return retval;
    }

    bool is_indexing() const { return this->lf_indexing; }

    /** Check the invariants for this object. */
    bool invariant()
    {
        require(!this->lf_filename.empty());

        return true;
    }

    ghc::filesystem::path get_path() const override;

    enum class note_type {
        indexing_disabled,
        duplicate,
        not_utf,
    };

    using note_map = std::map<note_type, std::string>;
    using safe_notes = safe::Safe<note_map>;

    note_map get_notes() const { return *this->lf_notes.readAccess(); }

    using safe_opid_map = safe::Safe<log_opid_map>;

    safe_opid_map& get_opids() { return this->lf_opids; }

    void quiesce() { this->lf_line_buffer.quiesce(); }

    void enable_cache() { this->lf_line_buffer.enable_cache(); }

    void dump_stats();

    robin_hood::unordered_map<uint32_t, bookmark_metadata>&
    get_bookmark_metadata()
    {
        return this->lf_bookmark_metadata;
    }

    std::map<std::string, metadata>& get_embedded_metadata()
    {
        return this->lf_embedded_metadata;
    }

    const std::map<std::string, metadata>& get_embedded_metadata() const
    {
        return this->lf_embedded_metadata;
    }

protected:
    /**
     * Process a line from the file.
     *
     * @param offset The offset of the line in the file.
     * @param prefix The contents of the line.
     * @param len The length of the 'prefix' string.
     */
    bool process_prefix(shared_buffer_ref& sbr,
                        const line_info& li,
                        scan_batch_context& sbc);

    void set_format_base_time(log_format* lf);

private:
    logfile(std::string filename, logfile_open_options& loo);

    std::string lf_filename;
    logfile_open_options lf_options;
    logfile_activity lf_activity;
    bool lf_named_file{true};
    bool lf_valid_filename{true};
    nonstd::optional<ghc::filesystem::path> lf_actual_path;
    std::string lf_basename;
    std::string lf_content_id;
    struct stat lf_stat {};
    std::shared_ptr<log_format> lf_format;
    std::vector<logline> lf_index;
    time_t lf_index_time{0};
    file_off_t lf_index_size{0};
    bool lf_sort_needed{false};
    line_buffer lf_line_buffer;
    int lf_time_offset_line{0};
    struct timeval lf_time_offset {
        0, 0
    };
    bool lf_is_closed{false};
    bool lf_indexing{true};
    bool lf_partial_line{false};
    logline_observer* lf_logline_observer{nullptr};
    logfile_observer* lf_logfile_observer{nullptr};
    size_t lf_longest_line{0};
    text_format_t lf_text_format{text_format_t::TF_UNKNOWN};
    uint32_t lf_out_of_time_order_count{0};
    safe_notes lf_notes;
    safe_opid_map lf_opids;
    size_t lf_watch_count{0};
    ArenaAlloc::Alloc<char> lf_allocator{64 * 1024};
    nonstd::optional<time_t> lf_cached_base_time;
    nonstd::optional<tm> lf_cached_base_tm;

    nonstd::optional<std::pair<file_off_t, size_t>> lf_next_line_cache;
    std::set<intern_string_t> lf_mismatched_formats;
    robin_hood::unordered_map<uint32_t, bookmark_metadata> lf_bookmark_metadata;

    std::vector<std::shared_ptr<format_tag_def>> lf_applicable_taggers;
    std::map<std::string, metadata> lf_embedded_metadata;
};

class logline_observer {
public:
    virtual ~logline_observer() = default;

    virtual void logline_restart(const logfile& lf, file_size_t rollback_size)
        = 0;

    virtual void logline_new_lines(const logfile& lf,
                                   logfile::const_iterator ll_begin,
                                   logfile::const_iterator ll_end,
                                   shared_buffer_ref& sbr)
        = 0;

    virtual void logline_eof(const logfile& lf) = 0;
};

#endif
