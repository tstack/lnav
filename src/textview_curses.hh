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

#include <vector>

#include "grep_proc.hh"
#include "bookmarks.hh"
#include "listview_curses.hh"
#include "lnav_log.hh"
#include "concise_index.hh"
#include "logfile.hh"

class logfile;
class logline;
class textview_curses;

class logfile_filter_state {
public:
    logfile_filter_state(logfile *lf = NULL) : tfs_logfile(lf) {
        memset(this->tfs_filter_count, 0, sizeof(this->tfs_filter_count));
        this->tfs_mask.reserve(64 * 1024);
    };

    void clear(void) {
        this->tfs_logfile = NULL;
        memset(this->tfs_filter_count, 0, sizeof(this->tfs_filter_count));
        this->tfs_mask.clear();
    };

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

    logfile *tfs_logfile;
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

    type_t get_type(void) const { return this->lf_type; };
    std::string get_id(void) const { return this->lf_id; };
    size_t get_index(void) const { return this->lf_index; };

    bool is_enabled(void) { return this->lf_enabled; };
    void enable(void) { this->lf_enabled = true; };
    void disable(void) { this->lf_enabled = false; };

    void revert_to_last(logfile_filter_state &lfs) {
        this->lf_message_matched = this->lf_last_message_matched;
        this->lf_lines_for_message = this->lf_last_lines_for_message;

        for (size_t lpc = 0; lpc < this->lf_lines_for_message; lpc++) {
            lfs.tfs_filter_count[this->lf_index] -= 1;
            size_t line_number = lfs.tfs_filter_count[this->lf_index];

            lfs.tfs_mask[line_number] &= ~(((uint32_t) 1) << this->lf_index);
        }
        if (this->lf_lines_for_message > 0) {
            this->lf_lines_for_message -= 1;
        }
        if (this->lf_lines_for_message == 0) {
            this->lf_message_matched = false;
        }
    }

    void add_line(logfile_filter_state &lfs, const logfile::const_iterator ll, shared_buffer_ref &line);

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

    virtual std::string to_command(void) = 0;

    bool operator==(const std::string &rhs) {
        return this->lf_id == rhs;
    };

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

class filter_stack {
public:
    typedef std::vector<text_filter *>::iterator iterator;

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
        for (iterator iter = this->begin(); iter != this->end(); ++iter) {
            size_t index = (*iter)->get_index();

            require(used[index] == false);

            used[index] = true;
        }
        for (int lpc = 0; lpc < logfile_filter_state::MAX_FILTERS; lpc++) {
            if (!used[lpc]) {
                return lpc;
            }
        }
        throw "No more filters";
    };

    void add_filter(text_filter *filter) {
        this->fs_filters.push_back(filter);
    };

    void clear_filters(void) {
        while (!this->fs_filters.empty()) {
            text_filter *tf = this->fs_filters.back();

            this->fs_filters.pop_back();
            delete tf;
        }
    };

    void set_filter_enabled(text_filter *filter, bool enabled) {
        if (enabled) {
            filter->enable();
        }
        else {
            filter->disable();
        }
    }

    text_filter *get_filter(std::string id)
    {
        std::vector<text_filter *>::iterator iter;
        text_filter *         retval = NULL;

        for (iter = this->fs_filters.begin();
             iter != this->fs_filters.end() && (*iter)->get_id() != id;
             iter++) { }
        if (iter != this->fs_filters.end()) {
            retval = *iter;
        }

        return retval;
    };

    bool delete_filter(std::string id) {
        std::vector<text_filter *>::iterator iter;

        for (iter = this->fs_filters.begin();
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
        for (iterator iter = this->begin(); iter != this->end(); ++iter) {
            text_filter *tf = (*iter);
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
    std::vector<text_filter *> fs_filters;
};

/**
 * Source for the text to be shown in a textview_curses view.
 */
class text_sub_source {
public:
    virtual ~text_sub_source() { };

    virtual void toggle_scrub(void) { };

    /**
     * @return The total number of lines available from the source.
     */
    virtual size_t text_line_count() = 0;

    virtual size_t text_line_width() {
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
                                     bool raw = false) = 0;

    virtual size_t text_size_for_line(textview_curses &tc, int line, bool raw = false) = 0;

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
    virtual void text_mark(bookmark_type_t *bm, int line, bool added) {};

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

private:
    filter_stack tss_filters;
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
      public grep_proc_source,
      public grep_proc_sink {
public:

    typedef view_action<textview_curses> action;

    static bookmark_type_t BM_USER;
    static bookmark_type_t BM_PARTITION;
    static bookmark_type_t BM_SEARCH;

    static string_attr_type SA_ORIGINAL_LINE;
    static string_attr_type SA_BODY;

    struct highlighter {
        highlighter()
            : h_code(NULL),
              h_code_extra(NULL),
              h_multiple(false) { };
        highlighter(pcre *code,
                    bool multiple = false,
                    view_colors::role_t role = view_colors::VCR_NONE)
            : h_code(code),
              h_multiple(multiple)
        {
            const char *errptr;

            if (!multiple) {
                if (role == view_colors::VCR_NONE) {
                    this->h_roles.
                    push_back(view_colors::singleton().next_highlight());
                }
                else {
                    this->h_roles.push_back(role);
                }
            }
            this->h_code_extra = pcre_study(this->h_code, 0, &errptr);
            if (!this->h_code_extra && errptr) {
                log_error("pcre_study error: %s", errptr);
            }
            if (this->h_code_extra != NULL) {
                pcre_extra *extra = this->h_code_extra;

                extra->flags |= (PCRE_EXTRA_MATCH_LIMIT |
                                 PCRE_EXTRA_MATCH_LIMIT_RECURSION);
                extra->match_limit           = 10000;
                extra->match_limit_recursion = 500;
            }
        };

        view_colors::role_t              get_role(unsigned int index)
        {
            view_colors &       vc = view_colors::singleton();
            view_colors::role_t retval;

            if (this->h_multiple) {
                while (index >= this->h_roles.size()) {
                    this->h_roles.push_back(vc.next_highlight());
                }
                retval = this->h_roles[index];
            }
            else {
                retval = this->h_roles[0];
            }

            return retval;
        };

        int                              get_attrs(int index)
        {
            return view_colors::singleton().
                   attrs_for_role(this->get_role(index));
        };

        pcre *                           h_code;
        pcre_extra *                     h_code_extra;
        bool                             h_multiple;
        std::vector<view_colors::role_t> h_roles;
    };

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
        for (vis_line_t curr_line = start_line; curr_line <= end_line;
             ++curr_line) {
            bookmark_vector<vis_line_t> &bv =
                this->tc_bookmarks[bm];
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
            if (this->tc_sub_source != NULL) {
                this->tc_sub_source->text_mark(bm, (int)curr_line, added);
            }
        }
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
        if (this->tc_sub_source != NULL) {
            this->tc_sub_source->text_mark(bm, (int)vl, marked);
        }
    };

    void set_sub_source(text_sub_source *src)
    {
        this->tc_sub_source = src;
        this->reload_data();
    };
    text_sub_source *get_sub_source(void) const { return this->tc_sub_source; };

    void set_delegate(text_delegate *del) {
        this->tc_delegate = del;
    };

    text_delegate *get_delegate(void) const { return this->tc_delegate; };

    void horiz_shift(vis_line_t start, vis_line_t end,
                     int off_start,
                     std::string highlight_name,
                     std::pair<int, int> &range_out)
    {
        highlighter &hl       = this->tc_highlights[highlight_name];
        int          prev_hit = -1, next_hit = INT_MAX;
        std::string  str;

        for (; start < end; ++start) {
            int off;

            this->tc_sub_source->text_value_for_line(*this, start, str);

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

    void grep_end_batch(grep_proc &gp)
    {
        if (this->tc_follow_search &&
            !this->tc_bookmarks[&BM_SEARCH].empty()) {
            vis_line_t first_hit;

            first_hit = this->tc_bookmarks[&BM_SEARCH].
                        next(vis_line_t(this->get_top() - 1));
            if (first_hit != -1) {
                if (first_hit > 0) {
                    --first_hit;
                }
                this->set_top(first_hit);
            }
        }
        this->tc_search_action.invoke(this);
    };
    void grep_end(grep_proc &gp);

    size_t listview_rows(const listview_curses &lv)
    {
        return this->tc_sub_source == NULL ? 0 :
               this->tc_sub_source->text_line_count();
    };

    size_t listview_width(const listview_curses &lv) {
        return this->tc_sub_source == NULL ? 0 :
               this->tc_sub_source->text_line_width();
    };

    void listview_value_for_row(const listview_curses &lv,
                                vis_line_t line,
                                attr_line_t &value_out);

    size_t listview_size_for_row(const listview_curses &lv, vis_line_t row) {
        return this->tc_sub_source->text_size_for_line(*this, row);
    };

    std::string listview_source_name(const listview_curses &lv) {
        return this->tc_sub_source == NULL ? "" :
               this->tc_sub_source->text_source_name(*this);
    };

    bool grep_value_for_line(int line, std::string &value_out)
    {
        bool retval = false;

        if (this->tc_sub_source != NULL &&
            line < (int)this->tc_sub_source->text_line_count()) {
            this->tc_sub_source->text_value_for_line(*this,
                                                     line,
                                                     value_out,
                                                     true);
            retval = true;
        }

        return retval;
    };

    void grep_begin(grep_proc &gp);
    void grep_match(grep_proc &gp,
                    grep_line_t line,
                    int start,
                    int end);

    bool is_searching(void) { return this->tc_searching; };

    void set_follow_search(bool fs) { this->tc_follow_search = fs; };

    size_t get_match_count(void)
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

    bool handle_mouse(mouse_event &me);

    void reload_data(void);

    void do_update(void) {
        this->listview_curses::do_update();
        if (this->tc_delegate != NULL) {
            this->tc_delegate->text_overlay(*this);
        }
    };

protected:
    text_sub_source *tc_sub_source;
    text_delegate *tc_delegate;

    vis_bookmarks tc_bookmarks;

    bool   tc_searching;
    bool   tc_follow_search;
    action tc_search_action;

    highlight_map_t           tc_highlights;
    highlight_map_t::iterator tc_current_highlight;

    vis_line_t tc_selection_start;
    vis_line_t tc_selection_last;
    bool tc_selection_cleared;
};
#endif
