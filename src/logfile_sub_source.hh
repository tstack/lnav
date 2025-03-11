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
 * @file logfile_sub_source.hh
 */

#ifndef logfile_sub_source_hh
#define logfile_sub_source_hh

#include <array>
#include <utility>
#include <vector>

#include <limits.h>

#include "base/time_util.hh"
#include "big_array.hh"
#include "bookmarks.hh"
#include "document.sections.hh"
#include "filter_observer.hh"
#include "log_format.hh"
#include "logfile.hh"
#include "strong_int.hh"
#include "textview_curses.hh"

STRONG_INT_TYPE(uint64_t, content_line);

struct sqlite3_stmt;
extern "C"
{
int sqlite3_finalize(sqlite3_stmt* pStmt);
}

class logfile_sub_source;

class index_delegate {
public:
    virtual ~index_delegate() = default;

    virtual void index_start(logfile_sub_source& lss) {}

    virtual void index_line(logfile_sub_source& lss,
                            logfile* lf,
                            logfile::iterator ll)
    {
    }

    virtual void index_complete(logfile_sub_source& lss) {}
};

class sql_filter : public text_filter {
public:
    sql_filter(logfile_sub_source& lss,
               std::string stmt_str,
               sqlite3_stmt* stmt)
        : text_filter(EXCLUDE, filter_lang_t::SQL, std::move(stmt_str), 0),
          sf_log_source(lss)
    {
        this->sf_filter_stmt = stmt;
    }

    bool matches(std::optional<line_source> ls,
                 const shared_buffer_ref& line) override;

    std::string to_command() const override;

    auto_mem<sqlite3_stmt> sf_filter_stmt{sqlite3_finalize};
    logfile_sub_source& sf_log_source;
};

class log_location_history : public location_history {
public:
    explicit log_location_history(logfile_sub_source& lss)
        : llh_history(std::begin(this->llh_backing),
                      std::end(this->llh_backing)),
          llh_log_source(lss)
    {
    }

    ~log_location_history() override = default;

    void loc_history_append(vis_line_t top) override;

    std::optional<vis_line_t> loc_history_back(vis_line_t current_top) override;

    std::optional<vis_line_t> loc_history_forward(
        vis_line_t current_top) override;

private:
    nonstd::ring_span<content_line_t> llh_history;
    logfile_sub_source& llh_log_source;
    content_line_t llh_backing[MAX_SIZE];
};

class logline_window {
public:
    logline_window(logfile_sub_source& lss,
                   vis_line_t start_vl,
                   vis_line_t end_vl)
        : lw_source(lss), lw_start_line(start_vl), lw_end_line(end_vl)
    {
    }

    class iterator;

    class logmsg_info {
    public:
        logmsg_info(logfile_sub_source& lss, vis_line_t vl);

        vis_line_t get_vis_line() const { return this->li_line; }

        size_t get_line_count() const;

        uint32_t get_file_line_number() const { return this->li_line_number; }

        logfile* get_file_ptr() const { return this->li_file; }

        logline& get_logline() const { return *this->li_logline; }

        const string_attrs_t& get_attrs() const
        {
            this->load_msg();
            return this->li_string_attrs;
        }

        const logline_value_vector& get_values() const
        {
            this->load_msg();
            return this->li_line_values;
        }

        std::optional<bookmark_metadata*> get_metadata() const;

        Result<auto_buffer, std::string> get_line_hash() const;

        struct metadata_edit_guard {
            ~metadata_edit_guard();

            bookmark_metadata& operator*();

        private:
            friend logmsg_info;

            metadata_edit_guard(logmsg_info& li) : meg_logmsg_info(li) {}
            logmsg_info& meg_logmsg_info;
        };

        metadata_edit_guard edit_metadata()
        {
            return metadata_edit_guard(*this);
        }

        std::string to_string(const struct line_range& lr) const;

    private:
        friend iterator;
        friend metadata_edit_guard;

        void next_msg();
        void prev_msg();
        void load_msg() const;

        logfile_sub_source& li_source;
        vis_line_t li_line;
        uint32_t li_line_number;
        logfile* li_file{nullptr};
        logfile::iterator li_logline;
        mutable string_attrs_t li_string_attrs;
        mutable logline_value_vector li_line_values;
    };

    class iterator {
    public:
        iterator(logfile_sub_source& lss, vis_line_t vl) : i_info(lss, vl) {}

        iterator& operator++();
        iterator& operator--();

        bool operator!=(const iterator& rhs) const
        {
            return this->i_info.get_vis_line() != rhs.i_info.get_vis_line();
        }

        bool operator==(const iterator& rhs) const
        {
            return this->i_info.get_vis_line() == rhs.i_info.get_vis_line();
        }

        const logmsg_info& operator*() const { return this->i_info; }

        const logmsg_info* operator->() const { return &this->i_info; }

    private:
        logmsg_info i_info;
    };

    iterator begin();

    iterator end();

private:
    logfile_sub_source& lw_source;
    vis_line_t lw_start_line;
    vis_line_t lw_end_line;
};

/**
 * Delegate class that merges the contents of multiple log files into a single
 * source of data for a text view.
 */
class logfile_sub_source
    : public text_sub_source
    , public text_time_translator
    , public text_accel_source
    , public list_input_delegate
    , public text_anchors
    , public text_delegate
    , public text_detail_provider
    , public lnav_config_listener {
public:
    const static bookmark_type_t BM_FILES;

    virtual void text_filters_changed();

    logfile_sub_source();

    ~logfile_sub_source() = default;

    enum class line_context_t : uint8_t {
        filename,
        basename,
        none,
        time_column,
    };

    void increase_line_context();

    bool decrease_line_context();

    size_t get_filename_offset() const;

    line_context_t get_line_context() const { return this->lss_line_context; }

    log_level_t get_min_log_level() const { return this->lss_min_log_level; }

    void set_force_rebuild() { this->lss_force_rebuild = true; }

    void set_min_log_level(log_level_t level)
    {
        if (this->lss_min_log_level != level) {
            this->lss_min_log_level = level;
            this->text_filters_changed();
        }
    }

    bool list_input_handle_key(listview_curses& lv, const ncinput& ch);

    void set_marked_only(bool val)
    {
        if (this->lss_marked_only != val) {
            this->lss_marked_only = val;
            this->text_filters_changed();
        }
    }

    bool get_marked_only() { return this->lss_marked_only; }

    size_t text_line_count() { return this->lss_filtered_index.size(); }

    size_t text_line_width(textview_curses& curses)
    {
        return this->lss_longest_line;
    }

    size_t file_count() const;

    bool empty() const { return this->lss_filtered_index.empty(); }

    line_info text_value_for_line(textview_curses& tc,
                                  int row,
                                  std::string& value_out,
                                  line_flags_t flags);

    void text_attrs_for_line(textview_curses& tc,
                             int row,
                             string_attrs_t& value_out);

    size_t text_size_for_line(textview_curses& tc, int row, line_flags_t flags);

    void text_mark(const bookmark_type_t* bm, vis_line_t line, bool added);

    void text_clear_marks(const bookmark_type_t* bm);

    bool insert_file(const std::shared_ptr<logfile>& lf);

    void remove_file(std::shared_ptr<logfile> lf);

    enum class rebuild_result {
        rr_no_change,
        rr_appended_lines,
        rr_partial_rebuild,
        rr_full_rebuild,
    };

    rebuild_result rebuild_index(std::optional<ui_clock::time_point> deadline
                                 = std::nullopt);

    void text_update_marks(vis_bookmarks& bm);

    void set_user_mark(const bookmark_type_t* bm, content_line_t cl)
    {
        this->lss_user_marks[bm].insert_once(cl);
    }

    bookmarks<content_line_t>::type& get_user_bookmarks()
    {
        return this->lss_user_marks;
    }

    bookmark_metadata& get_bookmark_metadata(content_line_t cl);

    bookmark_metadata& get_bookmark_metadata(vis_line_t vl)
    {
        return this->get_bookmark_metadata(this->at(vl));
    }

    struct bookmark_metadata_context {
        std::optional<vis_line_t> bmc_current;
        std::optional<bookmark_metadata*> bmc_current_metadata;
        std::optional<vis_line_t> bmc_next_line;
    };

    bookmark_metadata_context get_bookmark_metadata_context(
        vis_line_t vl,
        bookmark_metadata::categories desired
        = bookmark_metadata::categories::any) const;

    std::optional<bookmark_metadata*> find_bookmark_metadata(
        content_line_t cl) const;

    std::optional<bookmark_metadata*> find_bookmark_metadata(
        vis_line_t vl) const
    {
        return this->find_bookmark_metadata(this->at(vl));
    }

    void erase_bookmark_metadata(content_line_t cl);

    void erase_bookmark_metadata(vis_line_t vl)
    {
        this->erase_bookmark_metadata(this->at(vl));
    }

    void clear_bookmark_metadata();

    int get_filtered_count() const
    {
        return this->lss_index.size() - this->lss_filtered_index.size();
    }

    int get_filtered_count_for(size_t filter_index) const;

    Result<void, lnav::console::user_message> set_sql_filter(
        std::string stmt_str, sqlite3_stmt* stmt);

    Result<void, lnav::console::user_message> set_sql_marker(
        std::string stmt_str, sqlite3_stmt* stmt);

    Result<void, lnav::console::user_message> set_preview_sql_filter(
        sqlite3_stmt* stmt);

    std::string get_sql_filter_text()
    {
        auto filt = this->get_sql_filter();

        if (filt) {
            return filt.value()->get_id();
        }
        return "";
    }

    std::optional<std::shared_ptr<text_filter>> get_sql_filter();

    std::string get_sql_marker_text() const
    {
        return this->lss_marker_stmt_text;
    }

    std::shared_ptr<logfile> find(const char* fn, content_line_t& line_base);

    std::shared_ptr<logfile> find(content_line_t& line) const
    {
        std::shared_ptr<logfile> retval;

        retval = this->lss_files[line / MAX_LINES_PER_FILE]->get_file();
        line = content_line_t(line % MAX_LINES_PER_FILE);

        return retval;
    }

    logfile* find_file_ptr(content_line_t& line) const
    {
        auto retval
            = this->lss_files[line / MAX_LINES_PER_FILE]->get_file_ptr();
        line = content_line_t(line % MAX_LINES_PER_FILE);

        return retval;
    }

    logline* find_line(content_line_t line) const
    {
        logline* retval = nullptr;
        auto lf = this->find_file_ptr(line);

        if (lf != nullptr) {
            auto ll_iter = lf->begin() + line;

            retval = &(*ll_iter);
        }

        return retval;
    }

    std::optional<std::pair<std::shared_ptr<logfile>, logfile::iterator>>
    find_line_with_file(content_line_t line) const
    {
        std::shared_ptr<logfile> lf = this->find(line);

        if (lf != nullptr) {
            auto ll_iter = lf->begin() + line;

            return std::make_pair(lf, ll_iter);
        }

        return std::nullopt;
    }

    std::optional<std::pair<std::shared_ptr<logfile>, logfile::iterator>>
    find_line_with_file(vis_line_t vl) const
    {
        if (vl >= 0_vl && vl <= vis_line_t(this->lss_filtered_index.size())) {
            return this->find_line_with_file(this->at(vl));
        }

        return std::nullopt;
    }

    std::optional<vis_line_t> find_from_time(const struct timeval& start) const;

    std::optional<vis_line_t> find_from_time(time_t start) const
    {
        struct timeval tv = {start, 0};

        return this->find_from_time(tv);
    }

    std::optional<vis_line_t> find_from_time(const exttm& etm) const
    {
        return this->find_from_time(etm.to_timeval());
    }

    std::optional<vis_line_t> find_from_content(content_line_t cl);

    std::optional<row_info> time_for_row(vis_line_t row)
    {
        if (row >= 0_vl && row < (ssize_t) this->lss_filtered_index.size()) {
            auto cl = this->at(row);
            return row_info{
                this->find_line(cl)->get_timeval(),
                (int64_t) cl,
            };
        }
        return std::nullopt;
    }

    std::optional<vis_line_t> row_for(const row_info& ri);

    std::optional<vis_line_t> row_for_time(struct timeval time_bucket)
    {
        return this->find_from_time(time_bucket);
    }

    content_line_t at(vis_line_t vl) const
    {
        return this->lss_index[this->lss_filtered_index[vl]];
    }

    content_line_t at_base(vis_line_t vl)
    {
        while (this->find_line(this->at(vl))->get_sub_offset() != 0) {
            --vl;
        }

        return this->at(vl);
    }

    logline_window window_at(vis_line_t start_vl, vis_line_t end_vl)
    {
        return logline_window(*this, start_vl, end_vl);
    }

    logline_window window_at(vis_line_t start_vl)
    {
        return logline_window(*this, start_vl, start_vl + 1_vl);
    }

    logline_window window_to_end(vis_line_t start_vl)
    {
        return logline_window(
            *this, start_vl, vis_line_t(this->text_line_count()));
    }

    /**
     * Container for logfile references that keeps of how many lines in the
     * logfile have been indexed.
     */
    struct logfile_data {
        logfile_data(size_t index,
                     filter_stack& fs,
                     const std::shared_ptr<logfile>& lf)
            : ld_file_index(index), ld_filter_state(fs, lf),
              ld_visible(lf->is_indexing())
        {
            lf->set_logline_observer(&this->ld_filter_state);
        }

        void clear() { this->ld_filter_state.lfo_filter_state.clear(); }

        void set_file(const std::shared_ptr<logfile>& lf)
        {
            this->ld_filter_state.lfo_filter_state.tfs_logfile = lf;
            lf->set_logline_observer(&this->ld_filter_state);
        }

        std::shared_ptr<logfile> get_file() const
        {
            return this->ld_filter_state.lfo_filter_state.tfs_logfile;
        }

        logfile* get_file_ptr() const
        {
            return this->ld_filter_state.lfo_filter_state.tfs_logfile.get();
        }

        bool is_visible() const
        {
            return this->get_file_ptr() != nullptr && this->ld_visible;
        }

        void set_visibility(bool vis) { this->ld_visible = vis; }

        size_t ld_file_index;
        line_filter_observer ld_filter_state;
        size_t ld_lines_indexed{0};
        size_t ld_lines_watched{0};
        bool ld_visible;
    };

    using iterator = std::vector<std::unique_ptr<logfile_data>>::iterator;
    using const_iterator
        = std::vector<std::unique_ptr<logfile_data>>::const_iterator;

    iterator begin() { return this->lss_files.begin(); }

    iterator end() { return this->lss_files.end(); }

    const_iterator cbegin() const { return this->lss_files.begin(); }

    const_iterator cend() const { return this->lss_files.end(); }

    iterator find_data(content_line_t& line)
    {
        auto retval = this->lss_files.begin();
        std::advance(retval, line / MAX_LINES_PER_FILE);
        line = content_line_t(line % MAX_LINES_PER_FILE);

        return retval;
    }

    iterator find_data(content_line_t line, uint64_t& offset_out)
    {
        auto retval = this->lss_files.begin();
        std::advance(retval, line / MAX_LINES_PER_FILE);
        offset_out = line % MAX_LINES_PER_FILE;

        return retval;
    }

    std::optional<logfile_data*> find_data(const std::shared_ptr<logfile>& lf)
    {
        for (auto& ld : *this) {
            if (ld->ld_filter_state.lfo_filter_state.tfs_logfile == lf) {
                return ld.get();
            }
        }
        return std::nullopt;
    }

    iterator find_data_i(const std::shared_ptr<const logfile>& lf)
    {
        for (auto iter = this->begin(); iter != this->end(); ++iter) {
            if ((*iter)->ld_filter_state.lfo_filter_state.tfs_logfile == lf) {
                return iter;
            }
        }

        return this->end();
    }

    content_line_t get_file_base_content_line(iterator iter)
    {
        ssize_t index = std::distance(this->begin(), iter);

        return content_line_t(index * MAX_LINES_PER_FILE);
    }

    void set_index_delegate(index_delegate* id)
    {
        if (id != this->lss_index_delegate) {
            this->lss_index_delegate = id;
            this->reload_index_delegate();
        }
    }

    index_delegate* get_index_delegate() const
    {
        return this->lss_index_delegate;
    }

    void reload_index_delegate();

    class meta_grepper
        : public grep_proc_source<vis_line_t>
        , public grep_proc_sink<vis_line_t> {
    public:
        meta_grepper(logfile_sub_source& source) : lmg_source(source) {}

        std::optional<line_info> grep_value_for_line(
            vis_line_t line, std::string& value_out) override;

        vis_line_t grep_initial_line(vis_line_t start,
                                     vis_line_t highest) override;

        void grep_next_line(vis_line_t& line) override;

        void grep_begin(grep_proc<vis_line_t>& gp,
                        vis_line_t start,
                        vis_line_t stop) override;

        void grep_end(grep_proc<vis_line_t>& gp) override;

        void grep_match(grep_proc<vis_line_t>& gp, vis_line_t line) override;

        logfile_sub_source& lmg_source;
        bool lmg_done{false};
    };

    std::optional<
        std::pair<grep_proc_source<vis_line_t>*, grep_proc_sink<vis_line_t>*>>
    get_grepper();

    std::optional<location_history*> get_location_history()
    {
        return &this->lss_location_history;
    }

    void text_crumbs_for_line(int line, std::vector<breadcrumb::crumb>& crumbs);

    bool text_handle_mouse(textview_curses& tc,
                           const listview_curses::display_line_content_t&,
                           mouse_event& me);

    Result<bool, lnav::console::user_message> eval_sql_filter(
        sqlite3_stmt* stmt, iterator ld, logfile::const_iterator ll);

    void invalidate_sql_filter();

    void set_line_meta_changed() { this->lss_line_meta_changed = true; }

    bool is_line_meta_changed() const { return this->lss_line_meta_changed; }

    void set_exec_context(exec_context* ec) { this->lss_exec_context = ec; }

    exec_context* get_exec_context() const { return this->lss_exec_context; }

    static const uint64_t MAX_CONTENT_LINES = (1ULL << 40) - 1;
    static const uint64_t MAX_LINES_PER_FILE = 256 * 1024 * 1024;
    static const uint64_t MAX_FILES = (MAX_CONTENT_LINES / MAX_LINES_PER_FILE);

    std::function<void(logfile_sub_source&, file_off_t, file_size_t)>
        lss_sorting_observer;

    uint32_t lss_index_generation{0};

    void quiesce();

    struct __attribute__((__packed__)) indexed_content {
        indexed_content() = default;

        indexed_content(content_line_t cl) : ic_value(cl) {}

        operator content_line_t() const
        {
            return content_line_t(this->ic_value);
        }

        uint64_t ic_value : 40;
    };

    big_array<indexed_content> lss_index;

    std::optional<vis_line_t> row_for_anchor(const std::string& id);

    std::optional<vis_line_t> adjacent_anchor(vis_line_t vl, direction dir);

    std::optional<std::string> anchor_for_row(vis_line_t vl);

    std::unordered_set<std::string> get_anchors();

    std::optional<json_string> text_row_details(const textview_curses& tc);

    void reload_config(error_reporter &reporter);

protected:
    void text_accel_display_changed() { this->clear_line_size_cache(); }

    logline* text_accel_get_line(vis_line_t vl)
    {
        return this->find_line(this->at(vl));
    }

private:
    static const size_t LINE_SIZE_CACHE_SIZE = 512;

    void clear_line_size_cache()
    {
        this->lss_line_size_cache.fill(std::make_pair(0, 0));
        this->lss_line_size_cache[0].first = -1;
    }

    bool check_extra_filters(iterator ld, logfile::iterator ll);

    size_t lss_basename_width = 0;
    size_t lss_filename_width = 0;
    line_context_t lss_line_context{line_context_t::none};
    bool lss_force_rebuild{false};
    std::vector<std::unique_ptr<logfile_data>> lss_files;
    unsigned int lss_all_timestamp_flags{0};

    std::vector<uint32_t> lss_filtered_index;
    auto_mem<sqlite3_stmt> lss_preview_filter_stmt{sqlite3_finalize};

    bookmarks<content_line_t>::type lss_user_marks;
    auto_mem<sqlite3_stmt> lss_marker_stmt{sqlite3_finalize};
    std::string lss_marker_stmt_text;

    line_flags_t lss_token_flags{0};
    iterator lss_token_file_data;
    std::shared_ptr<logfile> lss_token_file;
    std::string lss_token_value;
    string_attrs_t lss_token_attrs;
    lnav::document::metadata lss_token_meta;
    int lss_token_meta_line{-1};
    int lss_token_meta_size{0};
    size_t lss_time_column_size{0};
    size_t lss_time_column_padding{0};
    logline_value_vector lss_token_values;
    int lss_token_shift_start{0};
    int lss_token_shift_size{0};
    shared_buffer lss_share_manager;
    logfile::iterator lss_token_line;
    std::array<std::pair<int, size_t>, LINE_SIZE_CACHE_SIZE>
        lss_line_size_cache;
    log_level_t lss_min_log_level{LEVEL_UNKNOWN};
    bool lss_marked_only{false};
    index_delegate* lss_index_delegate{nullptr};
    size_t lss_longest_line{0};
    meta_grepper lss_meta_grepper;
    log_location_history lss_location_history;
    exec_context* lss_exec_context{nullptr};

    bool lss_in_value_for_line{false};
    bool lss_line_meta_changed{false};

    bool lss_indexing_in_progress{false};
};

#endif
