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
 * @file textview_curses.hh
 */

#ifndef __textview_curses_hh
#define __textview_curses_hh

#include <utility>
#include <vector>

#include "ring_span.hh"
#include "grep_proc.hh"
#include "bookmarks.hh"
#include "listview_curses.hh"
#include "lnav_log.hh"
#include "text_format.hh"
#include "logfile.hh"
#include "highlighter.hh"

class logline;
class textview_curses;

using vis_bookmarks = bookmarks<vis_line_t>::type;

class logfile_filter_state {
public:
    logfile_filter_state(std::shared_ptr<logfile> lf = nullptr) : tfs_logfile(
        std::move(lf)) {
        memset(this->tfs_filter_count, 0, sizeof(this->tfs_filter_count));
        this->tfs_mask.reserve(64 * 1024);
    };

    void clear() {
        this->tfs_logfile = nullptr;
        memset(this->tfs_filter_count, 0, sizeof(this->tfs_filter_count));
        this->tfs_mask.clear();
        this->tfs_index.clear();
    };

    void clear_deleted_filter_state(uint32_t used_mask) {
        for (int lpc = 0; lpc < MAX_FILTERS; lpc++) {
            if (!(used_mask & (1L << lpc))) {
                this->tfs_filter_count[lpc] = 0;
            }
        }
        for (size_t lpc = 0; lpc < this->tfs_mask.size(); lpc++) {
            this->tfs_mask[lpc] &= used_mask;
        }
    }

    void resize(size_t newsize) {
        size_t old_mask_size = this->tfs_mask.size();

        this->tfs_mask.resize(newsize);
        if (newsize > old_mask_size) {
            memset(&this->tfs_mask[old_mask_size],
                    0,
                    sizeof(uint32_t) * (newsize - old_mask_size));
        }
    };

    const static int MAX_FILTERS = 32;

    std::shared_ptr<logfile> tfs_logfile;
    size_t tfs_filter_count[MAX_FILTERS];
    std::vector<uint32_t> tfs_mask;
    std::vector<uint32_t> tfs_index;
};

class text_filter {
public:
    typedef enum {
        MAYBE,
        INCLUDE,
        EXCLUDE,

        LFT__MAX,

        LFT__MASK = (MAYBE|INCLUDE|EXCLUDE)
    } type_t;

    text_filter(type_t type, const std::string id, size_t index)
            : lf_message_matched(false),
              lf_lines_for_message(0),
              lf_last_message_matched(false),
              lf_last_lines_for_message(0),
              lf_enabled(true),
              lf_type(type),
              lf_id(id),
              lf_index(index) { };
    virtual ~text_filter() { };

    type_t get_type() const { return this->lf_type; };
    void set_type(type_t t) { this->lf_type = t; };
    std::string get_id() const { return this->lf_id; };
    size_t get_index() const { return this->lf_index; };

    bool is_enabled() { return this->lf_enabled; };
    void enable() { this->lf_enabled = true; };
    void disable() { this->lf_enabled = false; };
    void set_enabled(bool value) {
        this->lf_enabled = value;
    }

    void revert_to_last(logfile_filter_state &lfs, size_t rollback_size) {
        this->lf_message_matched = this->lf_last_message_matched;
        this->lf_lines_for_message = this->lf_last_lines_for_message;

        for (size_t lpc = 0; lpc < this->lf_lines_for_message; lpc++) {
            lfs.tfs_filter_count[this->lf_index] -= 1;
            size_t line_number = lfs.tfs_filter_count[this->lf_index];

            lfs.tfs_mask[line_number] &= ~(((uint32_t) 1) << this->lf_index);
        }
        if (this->lf_lines_for_message > 0) {
            this->lf_lines_for_message -= rollback_size;

            ensure(this->lf_lines_for_message >= 0);
        }
        if (this->lf_lines_for_message == 0) {
            this->lf_message_matched = false;
        }
    }

    void add_line(logfile_filter_state &lfs, logfile::const_iterator ll, shared_buffer_ref &line);

    void end_of_message(logfile_filter_state &lfs) {
        uint32_t mask = 0;

        mask = ((uint32_t) this->lf_message_matched ? 1U : 0) << this->lf_index;

        for (size_t lpc = 0; lpc < this->lf_lines_for_message; lpc++) {
            size_t line_number = lfs.tfs_filter_count[this->lf_index];

            lfs.tfs_mask[line_number] |= mask;
            lfs.tfs_filter_count[this->lf_index] += 1;
        }
        this->lf_last_message_matched = this->lf_message_matched;
        this->lf_last_lines_for_message = this->lf_lines_for_message;
        this->lf_message_matched = false;
        this->lf_lines_for_message = 0;
    };

    virtual bool matches(const logfile &lf, const logline &ll, shared_buffer_ref &line) = 0;

    virtual std::string to_command() = 0;

    bool operator==(const std::string &rhs) {
        return this->lf_id == rhs;
    };

    bool lf_deleted{false};
    bool lf_message_matched;
    size_t lf_lines_for_message;
    bool lf_last_message_matched;
    size_t lf_last_lines_for_message;

protected:
    bool        lf_enabled;
    type_t      lf_type;
    std::string lf_id;
    size_t lf_index;
};

class empty_filter : public text_filter {
public:
    empty_filter(type_t type, size_t index)
        : text_filter(type, "", index) {
    }

    bool matches(const logfile &lf, const logline &ll,
                 shared_buffer_ref &line) override;

    std::string to_command() override;
};

class filter_stack {
public:
    typedef std::vector<std::shared_ptr<text_filter>>::iterator iterator;

    iterator begin() {
        return this->fs_filters.begin();
    }

    iterator end() {
        return this->fs_filters.end();
    }

    size_t size() const {
        return this->fs_filters.size();
    }

    bool empty() const {
        return this->fs_filters.empty();
    };

    bool full() const {
        return this->fs_filters.size() == logfile_filter_state::MAX_FILTERS;
    }

    size_t next_index() {
        bool used[32];

        memset(used, 0, sizeof(used));
        for (auto &iter : *this) {
            if (iter->lf_deleted) {
                continue;
            }

            size_t index = iter->get_index();

            require(used[index] == false);

            used[index] = true;
        }
        for (size_t lpc = 0; lpc < logfile_filter_state::MAX_FILTERS; lpc++) {
            if (!used[lpc]) {
                return lpc;
            }
        }
        throw "No more filters";
    };

    void add_filter(const std::shared_ptr<text_filter> &filter) {
        this->fs_filters.push_back(filter);
    };

    void clear_filters() {
        while (!this->fs_filters.empty()) {
            this->fs_filters.pop_back();
        }
    };

    void set_filter_enabled(const std::shared_ptr<text_filter> &filter, bool enabled) {
        if (enabled) {
            filter->enable();
        }
        else {
            filter->disable();
        }
    }

    std::shared_ptr<text_filter> get_filter(const std::string &id)
    {
        auto iter = this->fs_filters.begin();
        std::shared_ptr<text_filter> retval;

        for (;
             iter != this->fs_filters.end() && (*iter)->get_id() != id;
             iter++) { }
        if (iter != this->fs_filters.end()) {
            retval = *iter;
        }

        return retval;
    };

    bool delete_filter(const std::string &id) {
        auto iter = this->fs_filters.begin();

        for (;
             iter != this->fs_filters.end() && (*iter)->get_id() != id;
             iter++) {

        }
        if (iter != this->fs_filters.end()) {
            this->fs_filters.erase(iter);
            return true;
        }

        return false;
    };

    void get_enabled_mask(uint32_t &filter_in_mask, uint32_t &filter_out_mask) {
        filter_in_mask = filter_out_mask = 0;
        for (auto &iter : *this) {
            std::shared_ptr<text_filter> tf = iter;

            if (tf->lf_deleted) {
                continue;
            }
            if (tf->is_enabled()) {
                uint32_t bit = (1UL << tf->get_index());

                switch (tf->get_type()) {
                    case text_filter::EXCLUDE:
                        filter_out_mask |= bit;
                        break;
                    case text_filter::INCLUDE:
                        filter_in_mask |= bit;
                        break;
                    default:
                        ensure(0);
                        break;
                }
            }
        }
    };

private:
    std::vector<std::shared_ptr<text_filter>> fs_filters;
};

class text_time_translator {
public:
    virtual ~text_time_translator() { };

    virtual int row_for_time(struct timeval time_bucket) = 0;

    virtual struct timeval time_for_row(int row) = 0;

    void scroll_invoked(textview_curses *tc);

    void data_reloaded(textview_curses *tc);
protected:
    struct timeval ttt_top_time{0, 0};
};

class location_history {
public:
    virtual ~location_history() = default;

    virtual void loc_history_append(vis_line_t top) = 0;

    virtual nonstd::optional<vis_line_t>
    loc_history_back(vis_line_t current_top) = 0;

    virtual nonstd::optional<vis_line_t>
    loc_history_forward(vis_line_t current_top) = 0;

    const static int MAX_SIZE = 100;
protected:
    int lh_history_position{0};
};

/**
 * Source for the text to be shown in a textview_curses view.
 */
class text_sub_source {
public:
    virtual ~text_sub_source() { };

    enum {
        RB_RAW,
        RB_FULL,
        RB_REWRITE,
    };

    enum {
        RF_RAW = (1UL << RB_RAW),
        RF_FULL = (1UL << RB_FULL),
        RF_REWRITE = (1UL << RB_REWRITE),
    };

    typedef long line_flags_t;

    void register_view(textview_curses *tc) {
        this->tss_view = tc;
    };

    /**
     * @return The total number of lines available from the source.
     */
    virtual size_t text_line_count() = 0;

    virtual size_t text_line_width(textview_curses &curses) {
        return INT_MAX;
    };

    /**
     * Get the value for a line.
     *
     * @param tc The textview_curses object that is delegating control.
     * @param line The line number to retrieve.
     * @param value_out The string object that should be set to the line
     *   contents.
     * @param raw Indicates that the raw contents of the line should be returned
     *   without any post processing.
     */
    virtual void text_value_for_line(textview_curses &tc,
                                     int line,
                                     std::string &value_out,
                                     line_flags_t flags = 0) = 0;

    virtual size_t text_size_for_line(textview_curses &tc, int line, line_flags_t raw = 0) = 0;

    /**
     * Inform the source that the given line has been marked/unmarked.  This
     * callback function can be used to translate between between visible line
     * numbers and content line numbers.  For example, when viewing a log file
     * with filters being applied, we want the bookmarked lines to be stable
     * across changes in the filters.
     *
     * @param bm    The type of bookmark.
     * @param line  The line that has been marked/unmarked.
     * @param added True if the line was bookmarked and false if it was
     *   unmarked.
     */
    virtual void text_mark(bookmark_type_t *bm, vis_line_t line, bool added) {};

    /**
     * Clear the bookmarks for a particular type in the text source.
     *
     * @param bm The type of bookmarks to clear.
     */
    virtual void text_clear_marks(bookmark_type_t *bm) {};

    /**
     * Get the attributes for a line of text.
     *
     * @param tc The textview_curses object that is delegating control.
     * @param line The line number to retrieve.
     * @param value_out A string_attrs_t object that should be updated with the
     *   attributes for the line.
     */
    virtual void text_attrs_for_line(textview_curses &tc,
                                     int line,
                                     string_attrs_t &value_out) {};

    /**
     * Update the bookmarks used by the text view based on the bookmarks
     * maintained by the text source.
     *
     * @param bm The bookmarks data structure used by the text view.
     */
    virtual void text_update_marks(vis_bookmarks &bm) { };

    virtual std::string text_source_name(const textview_curses &tv) {
        return "";
    };

    filter_stack &get_filters() {
        return this->tss_filters;
    };

    virtual void text_filters_changed() {

    };

    virtual int get_filtered_count() const {
        return 0;
    };

    virtual text_format_t get_text_format() const {
        return TF_UNKNOWN;
    };

    virtual nonstd::optional<std::pair<grep_proc_source<vis_line_t> *, grep_proc_sink<vis_line_t> *>> get_grepper() {
        return nonstd::nullopt;
    }

    virtual nonstd::optional<location_history *> get_location_history() {
        return nonstd::nullopt;
    }

    bool tss_supports_filtering{false};
protected:
    textview_curses *tss_view{nullptr};
    filter_stack tss_filters;
};

class vis_location_history : public location_history {
public:
    vis_location_history()
        : vlh_history(std::begin(this->vlh_backing), std::end(this->vlh_backing))
    {
    }

    void loc_history_append(vis_line_t top) override {
        auto iter = this->vlh_history.begin();
        iter += this->vlh_history.size() - this->lh_history_position;
        this->vlh_history.erase_from(iter);
        this->lh_history_position = 0;
        this->vlh_history.push_back(top);
    }

    nonstd::optional<vis_line_t>
    loc_history_back(vis_line_t current_top) override {
        if (this->lh_history_position == 0) {
            vis_line_t history_top = this->current_position();
            if (history_top != current_top) {
                return history_top;
            }
        }

        if (this->lh_history_position + 1 >= this->vlh_history.size()) {
            return nonstd::nullopt;
        }

        this->lh_history_position += 1;

        return this->current_position();
    }

    nonstd::optional<vis_line_t>
    loc_history_forward(vis_line_t current_top) override {
        if (this->lh_history_position == 0) {
            return nonstd::nullopt;
        }

        this->lh_history_position -= 1;

        return this->current_position();
    }

    nonstd::ring_span<vis_line_t> vlh_history;
private:
    vis_line_t current_position() {
        auto iter = this->vlh_history.rbegin();

        iter += this->lh_history_position;

        return *iter;
    }

    vis_line_t vlh_backing[MAX_SIZE];
};

class text_delegate {
public:
    virtual ~text_delegate() { };
    
    virtual void text_overlay(textview_curses &tc) { };

    virtual bool text_handle_mouse(textview_curses &tc, mouse_event &me) {
        return false;
    };
};

/**
 * The textview_curses class adds user bookmarks and searching to the standard
 * list view interface.
 */
class textview_curses
    : public listview_curses,
      public list_data_source,
      public grep_proc_source<vis_line_t>,
      public grep_proc_sink<vis_line_t> {
public:

    typedef view_action<textview_curses> action;

    static bookmark_type_t BM_USER;
    static bookmark_type_t BM_SEARCH;
    static bookmark_type_t BM_META;

    static string_attr_type SA_ORIGINAL_LINE;
    static string_attr_type SA_BODY;
    static string_attr_type SA_HIDDEN;
    static string_attr_type SA_FORMAT;

    textview_curses();
    virtual ~textview_curses();

    vis_bookmarks &get_bookmarks(void) { return this->tc_bookmarks; };

    void toggle_user_mark(bookmark_type_t *bm,
                          vis_line_t start_line,
                          vis_line_t end_line = vis_line_t(-1))
    {
        if (end_line == -1) {
            end_line = start_line;
        }
        if (start_line > end_line) {
            std::swap(start_line, end_line);
        }

        if (start_line >= this->get_inner_height()) {
            return;
        }
        if (end_line >= this->get_inner_height()) {
            end_line = vis_line_t(this->get_inner_height() - 1);
        }
        for (vis_line_t curr_line = start_line; curr_line <= end_line;
             ++curr_line) {
            bookmark_vector<vis_line_t> &bv = this->tc_bookmarks[bm];
            bookmark_vector<vis_line_t>::iterator iter;
            bool added;

            iter = bv.insert_once(curr_line);
            if (iter == bv.end()) {
                added = true;
            }
            else {
                bv.erase(iter);
                added = false;
            }
            if (this->tc_sub_source) {
                this->tc_sub_source->text_mark(bm, curr_line, added);
            }
        }
        this->search_range(start_line, end_line + 1_vl);
        this->search_new_data();
    };

    void set_user_mark(bookmark_type_t *bm, vis_line_t vl, bool marked) {
        bookmark_vector<vis_line_t> &bv = this->tc_bookmarks[bm];
        bookmark_vector<vis_line_t>::iterator iter;

        if (marked) {
            bv.insert_once(vl);
        }
        else {
            iter = std::lower_bound(bv.begin(), bv.end(), vl);
            if (iter != bv.end() && *iter == vl) {
                bv.erase(iter);
            }
        }
        if (this->tc_sub_source) {
            this->tc_sub_source->text_mark(bm, vl, marked);
        }

        this->search_range(vl, vl + 1_vl);
        this->search_new_data();
        this->set_needs_update();
    };

    textview_curses &set_sub_source(text_sub_source *src) {
        this->tc_sub_source = src;
        src->register_view(this);
        this->reload_data();
        return *this;
    };

    text_sub_source *get_sub_source() const { return this->tc_sub_source; };

    textview_curses &set_delegate(text_delegate *del) {
        this->tc_delegate = del;

        return *this;
    };

    text_delegate *get_delegate() const { return this->tc_delegate; };

    void horiz_shift(vis_line_t start, vis_line_t end,
                     int off_start,
                     const std::string &highlight_name,
                     std::pair<int, int> &range_out)
    {
        highlighter &hl       = this->tc_highlights[highlight_name];
        int          prev_hit = -1, next_hit = INT_MAX;

        for (; start < end; ++start) {
            std::vector<attr_line_t> rows(1);
            int off;

            this->listview_value_for_rows(*this, start, rows);

            const std::string &str = rows[0].get_string();
            for (off = 0; off < (int)str.size(); ) {
                int rc, matches[128];

                rc = pcre_exec(hl.h_code,
                               hl.h_code_extra,
                               str.c_str(),
                               str.size(),
                               off,
                               0,
                               matches,
                               128);
                if (rc > 0) {
                    struct line_range lr;

                    if (rc == 2) {
                        lr.lr_start = matches[2];
                        lr.lr_end   = matches[3];
                    }
                    else {
                        lr.lr_start = matches[0];
                        lr.lr_end   = matches[1];
                    }

                    if (lr.lr_start < off_start) {
                        prev_hit = std::max(prev_hit, lr.lr_start);
                    }
                    else if (lr.lr_start > off_start) {
                        next_hit = std::min(next_hit, lr.lr_start);
                    }
                    if (lr.lr_end > lr.lr_start) {
                        off = matches[1];
                    }
                    else {
                        off += 1;
                    }
                }
                else {
                    off = str.size();
                }
            }
        }

        range_out = std::make_pair(prev_hit, next_hit);
    };

    void set_search_action(action sa) { this->tc_search_action = sa; };

    template<class _Receiver>
    void set_search_action(action::mem_functor_t<_Receiver> *mf)
    {
        this->tc_search_action = action(mf);
    };

    void grep_end_batch(grep_proc<vis_line_t> &gp)
    {
        if (this->tc_follow_deadline.tv_sec) {
            struct timeval now;

            gettimeofday(&now, nullptr);
            if (this->tc_follow_deadline < now) {
                this->tc_follow_deadline.tv_sec = 0;
                this->tc_follow_deadline.tv_usec = 0;
            } else if (!this->tc_bookmarks[&BM_SEARCH].empty()) {
                vis_line_t first_hit;

                first_hit = this->tc_bookmarks[&BM_SEARCH].next(
                    vis_line_t(this->get_top() - 1));
                if (first_hit != -1) {
                    if (first_hit > 0) {
                        --first_hit;
                    }
                    if (first_hit > this->get_top()) {
                        this->set_top(first_hit);
                    }
                }
            }
        }
        this->tc_search_action.invoke(this);
    };
    void grep_end(grep_proc<vis_line_t> &gp);

    size_t listview_rows(const listview_curses &lv)
    {
        return this->tc_sub_source == nullptr ? 0 :
               this->tc_sub_source->text_line_count();
    };

    size_t listview_width(const listview_curses &lv) {
        return this->tc_sub_source == nullptr ? 0 :
               this->tc_sub_source->text_line_width(*this);
    };

    void listview_value_for_rows(const listview_curses &lv,
                                 vis_line_t line,
                                 std::vector<attr_line_t> &rows_out);

    void textview_value_for_row(vis_line_t line, attr_line_t &value_out);

    size_t listview_size_for_row(const listview_curses &lv, vis_line_t row) {
        return this->tc_sub_source->text_size_for_line(*this, row);
    };

    std::string listview_source_name(const listview_curses &lv) {
        return this->tc_sub_source == nullptr ? "" :
               this->tc_sub_source->text_source_name(*this);
    };

    bool grep_value_for_line(vis_line_t line, std::string &value_out)
    {
        bool retval = false;

        if (this->tc_sub_source &&
            line < (int)this->tc_sub_source->text_line_count()) {
            this->tc_sub_source->text_value_for_line(*this,
                                                     line,
                                                     value_out,
                                                     text_sub_source::RF_RAW);
            retval = true;
        }

        return retval;
    };

    void grep_begin(grep_proc<vis_line_t> &gp, vis_line_t start, vis_line_t stop);
    void grep_match(grep_proc<vis_line_t> &gp,
                    vis_line_t line,
                    int start,
                    int end);

    bool is_searching(void) { return this->tc_searching > 0; };

    void set_follow_search_for(int64_t ms_to_deadline) {
        struct timeval now, tv;

        tv.tv_sec = ms_to_deadline / 1000;
        tv.tv_usec = (ms_to_deadline % 1000) * 1000;
        gettimeofday(&now, nullptr);
        timeradd(&now, &tv, &this->tc_follow_deadline);
    };

    size_t get_match_count()
    {
        return this->tc_bookmarks[&BM_SEARCH].size();
    };

    void match_reset()
    {
        this->tc_bookmarks[&BM_SEARCH].clear();
        if (this->tc_sub_source != NULL) {
            this->tc_sub_source->text_clear_marks(&BM_SEARCH);
        }
    };

    typedef std::map<std::string, highlighter> highlight_map_t;

    highlight_map_t &get_highlights() { return this->tc_highlights; };

    const highlight_map_t &get_highlights() const { return this->tc_highlights; };

    bool handle_mouse(mouse_event &me);

    void reload_data();

    void do_update() {
        this->listview_curses::do_update();
        if (this->tc_delegate != NULL) {
            this->tc_delegate->text_overlay(*this);
        }
    };

    bool toggle_hide_fields() {
        bool retval = this->tc_hide_fields;

        this->tc_hide_fields = !this->tc_hide_fields;

        return retval;
    };

    void execute_search(const std::string &regex_orig);

    void redo_search() {
        if (this->tc_search_child) {
            grep_proc<vis_line_t> *gp = this->tc_search_child->get_grep_proc();

            gp->invalidate();
            this->match_reset();
            gp->queue_request(0_vl)
              .start();

            if (this->tc_source_search_child) {
                this->tc_source_search_child->invalidate()
                    .queue_request(0_vl)
                    .start();
            }
        }
    };

    void search_range(vis_line_t start, vis_line_t stop = -1_vl) {
        if (this->tc_search_child) {
            this->tc_search_child->get_grep_proc()->queue_request(start, stop);
        }
        if (this->tc_source_search_child) {
            this->tc_source_search_child->queue_request(start, stop);
        }
    }

    void search_new_data() {
        this->search_range(-1_vl);
        if (this->tc_search_child) {
            this->tc_search_child->get_grep_proc()->start();
        }
        if (this->tc_source_search_child) {
            this->tc_source_search_child->start();
        }
    }

    void update_poll_set(std::vector<struct pollfd> &pollfds) {
        if (this->tc_search_child) {
            this->tc_search_child->get_grep_proc()->update_poll_set(pollfds);
        }
        if (this->tc_source_search_child) {
            this->tc_source_search_child->update_poll_set(pollfds);
        }
    }

    void check_poll_set(const std::vector<struct pollfd> &pollfds) {
        if (this->tc_search_child) {
            this->tc_search_child->get_grep_proc()->check_poll_set(pollfds);
        }
        if (this->tc_source_search_child) {
            this->tc_source_search_child->check_poll_set(pollfds);
        }
    }

    std::string get_last_search() const {
        return this->tc_last_search;
    }

    void invoke_scroll() {
        if (this->tc_sub_source != nullptr) {
            auto ttt = dynamic_cast<text_time_translator *>(this->tc_sub_source);

            if (ttt != nullptr) {
                ttt->scroll_invoked(this);
            }
        }

        listview_curses::invoke_scroll();
    }

protected:

    class grep_highlighter {
    public:
        grep_highlighter(std::unique_ptr<grep_proc<vis_line_t>> &gp,
                         std::string hl_name,
                         textview_curses::highlight_map_t &hl_map)
            : gh_grep_proc(std::move(gp)),
              gh_hl_name(std::move(hl_name)),
              gh_hl_map(hl_map) { };

        ~grep_highlighter()
        {
            this->gh_hl_map.erase(this->gh_hl_map.find(this->gh_hl_name));
        };

        grep_proc<vis_line_t> *get_grep_proc() { return this->gh_grep_proc.get(); };

    private:
        std::unique_ptr<grep_proc<vis_line_t>> gh_grep_proc;
        std::string gh_hl_name;
        textview_curses::highlight_map_t &gh_hl_map;
    };

    text_sub_source *tc_sub_source;
    text_delegate *tc_delegate;

    vis_bookmarks tc_bookmarks;

    int tc_searching{0};
    struct timeval tc_follow_deadline;
    action tc_search_action;

    highlight_map_t           tc_highlights;

    vis_line_t tc_selection_start;
    vis_line_t tc_selection_last;
    bool tc_selection_cleared;
    bool tc_hide_fields;

    std::string tc_last_search;
    std::unique_ptr<grep_highlighter> tc_search_child;
    std::shared_ptr<grep_proc<vis_line_t>> tc_source_search_child;
};

#endif
