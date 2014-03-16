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
 * @file logfile_sub_source.hh
 */

#ifndef __logfile_sub_source_hh
#define __logfile_sub_source_hh

#include <limits.h>

#include <map>
#include <list>
#include <sstream>
#include <vector>
#include <algorithm>

#include "lnav_log.hh"
#include "log_accel.hh"
#include "strong_int.hh"
#include "logfile.hh"
#include "bookmarks.hh"
#include "textview_curses.hh"

STRONG_INT_TYPE(uint64_t, content_line);

/**
 * Delegate class that merges the contents of multiple log files into a single
 * source of data for a text view.
 */
class logfile_sub_source
    : public text_sub_source {
public:

    class observer
        : public logfile_observer {
public:
        virtual void logfile_sub_source_filtering(logfile_sub_source &lss,
                                                  vis_line_t cl,
                                                  size_t total) = 0;
    };

    static bookmark_type_t BM_ERRORS;
    static bookmark_type_t BM_WARNINGS;
    static bookmark_type_t BM_FILES;

    logfile_sub_source();
    virtual ~logfile_sub_source();

    const filter_stack_t &get_filters(void) const
    {
        return this->lss_filters;
    };

    void add_filter(logfile_filter *filter) {
        this->lss_filters.push_back(filter);
        this->lss_filter_generation += 1;
    };

    void clear_filters(void) { 
        this->lss_filters.clear(); 
        this->lss_filter_generation += 1;
    };

    void filter_changed(void) {
        this->lss_filter_generation += 1;
    };

    void set_filter_enabled(logfile_filter *filter, bool enabled) {
        if (enabled) {
            filter->enable();
        }
        else {
            filter->disable();
        }
        this->lss_filter_generation += 1;
    }

    logfile_filter *get_filter(std::string id)
    {
        filter_stack_t::iterator iter;
        logfile_filter *         retval = NULL;

        for (iter = this->lss_filters.begin();
             iter != this->lss_filters.end() && (*iter)->get_id() != id;
             iter++) { }
        if (iter != this->lss_filters.end()) {
            retval = *iter;
        }

        return retval;
    };

    void toggle_scrub(void) {
        this->lss_flags ^= F_SCRUB;
        this->clear_line_size_cache();
    };

    void toggle_time_offset(void) {
        this->lss_flags ^= F_TIME_OFFSET;
        this->clear_line_size_cache();
    };

    void set_time_offset(bool enabled) {
        if (enabled)
            this->lss_flags |= F_TIME_OFFSET;
        else
            this->lss_flags &= ~F_TIME_OFFSET;
        this->clear_line_size_cache();
    };

    bool is_time_offset_enabled(void) const {
        return this->lss_flags & F_TIME_OFFSET;
    };

    size_t text_line_count()
    {
        return this->lss_index.size();
    };

    bool empty() const { return this->lss_index.empty(); };

    void text_value_for_line(textview_curses &tc,
                             int row,
                             std::string &value_out,
                             bool raw);

    void text_attrs_for_line(textview_curses &tc,
                             int row,
                             string_attrs_t &value_out);

    size_t text_size_for_line(textview_curses &tc, int row, bool raw) {
        size_t index = row % LINE_SIZE_CACHE_SIZE;

        if (this->lss_line_size_cache[index].first != row) {
            std::string value;

            this->text_value_for_line(tc, row, value, raw);
            this->lss_line_size_cache[index].second = value.size();
            this->lss_line_size_cache[index].first = row;
        }
        return this->lss_line_size_cache[index].second;
    };

    void text_mark(bookmark_type_t *bm, int line, bool added)
    {
        content_line_t cl = this->at(vis_line_t(line));
        std::vector<content_line_t>::iterator lb;

        if (bm == &textview_curses::BM_USER) {
            logline *ll = this->find_line(cl);

            ll->set_mark(added);
        }
        lb = std::lower_bound(this->lss_user_marks[bm].begin(),
                              this->lss_user_marks[bm].end(),
                              cl);
        if (added) {
            if (lb == this->lss_user_marks[bm].end() || *lb != cl) {
                this->lss_user_marks[bm].insert(lb, cl);
            }
        }
        else if (lb != this->lss_user_marks[bm].end() && *lb == cl) {
            require(lb != this->lss_user_marks[bm].end());

            this->lss_user_marks[bm].erase(lb);
        }
    };

    void text_clear_marks(bookmark_type_t *bm)
    {
        std::vector<content_line_t>::iterator iter;

        if (bm == &textview_curses::BM_USER) {
            for (iter = this->lss_user_marks[bm].begin();
                 iter != this->lss_user_marks[bm].end();
                 ++iter) {
                this->find_line(*iter)->set_mark(false);
            }
        }
        this->lss_user_marks[bm].clear();
    };

    bool insert_file(logfile *lf)
    {
        std::vector<logfile_data>::iterator existing;

        require(lf->size() < MAX_LINES_PER_FILE);

        existing = std::find_if(this->lss_files.begin(),
                                this->lss_files.end(),
                                logfile_data_eq(NULL));
        if (existing == this->lss_files.end()) {
            if (this->lss_files.size() >= MAX_FILES) {
                return false;
            }

            this->lss_files.push_back(logfile_data(lf));
            this->lss_index.clear();
        }
        else {
            existing->ld_file = lf;
        }

        return true;
    };

    void remove_file(logfile *lf)
    {
        std::vector<logfile_data>::iterator iter;

        iter = std::find_if(this->lss_files.begin(),
                            this->lss_files.end(),
                            logfile_data_eq(lf));
        if (iter != this->lss_files.end()) {
            bookmarks<content_line_t>::type::iterator mark_iter;
            int file_index = iter - this->lss_files.begin();

            iter->clear();
            for (mark_iter = this->lss_user_marks.begin();
                 mark_iter != this->lss_user_marks.end();
                 ++mark_iter) {
                content_line_t mark_curr = content_line_t(
                    file_index * MAX_LINES_PER_FILE);
                content_line_t mark_end = content_line_t(
                    (file_index + 1) * MAX_LINES_PER_FILE);
                bookmark_vector<content_line_t>::iterator bv_iter;
                bookmark_vector<content_line_t> &         bv =
                    mark_iter->second;

                while ((bv_iter =
                            std::lower_bound(bv.begin(), bv.end(),
                                             mark_curr)) != bv.end()) {
                    if (*bv_iter >= mark_end) {
                        break;
                    }
                    mark_iter->second.erase(bv_iter);
                }
            }
        }
    };

    bool rebuild_index(observer *obs = NULL, bool force = false);

    void text_update_marks(vis_bookmarks &bm);

    void set_user_mark(bookmark_type_t *bm, content_line_t cl)
    {
        this->lss_user_marks[bm].insert_once(cl);
    };

    bookmarks<content_line_t>::type &get_user_bookmarks(void)
    {
        return this->lss_user_marks;
    };

    std::map<content_line_t, bookmark_metadata> &get_user_bookmark_metadata(void) {
        return this->lss_user_mark_metadata;
    };

    int get_filtered_count() const { return this->lss_filtered_count; };

    logfile *find(const char *fn, content_line_t &line_base);

    logfile *find(content_line_t &line)
    {
        logfile *retval;

        retval = this->lss_files[line / MAX_LINES_PER_FILE].ld_file;
        line   = content_line_t(line % MAX_LINES_PER_FILE);

        return retval;
    };

    logline *find_line(content_line_t line)
    {
        logline *retval = NULL;
        logfile *lf     = this->find(line);

        if (lf != NULL) {
            logfile::iterator ll_iter = lf->begin() + line;

            retval = &(*ll_iter);
        }

        return retval;
    };

    vis_line_t find_from_time(const struct timeval &start);

    vis_line_t find_from_time(time_t start) {
        struct timeval tv = { start, 0 };

        return this->find_from_time(tv);
    };

    content_line_t at(vis_line_t vl) {
        return this->lss_index[vl];
    };

    content_line_t at_base(vis_line_t vl) {
        while (this->find_line(this->at(vl))->get_sub_offset() != 0) {
            --vl;
        }

        return this->at(vl);
    };

    log_accel::direction_t get_line_accel_direction(vis_line_t vl);

    /**
     * Container for logfile references that keeps of how many lines in the
     * logfile have been indexed.
     */
    struct logfile_data {
        logfile_data(logfile *lf = NULL)
            : ld_file(lf),
              ld_lines_indexed(0) { };

        bool operator<(const logfile_data &rhs)
        {
            if (this->ld_file == rhs.ld_file) {
                return false;
            }
            if (this->ld_file == NULL) {
                return true;
            }
            if (this->ld_file != NULL) {
                return true;
            }
            return (*this->ld_file) < (*rhs.ld_file);
        };

        void     clear(void)
        {
            this->ld_file = NULL;
        };

        logfile *ld_file;
        size_t   ld_lines_indexed;
        struct {
            content_line_t ld_start;
            content_line_t ld_last;
        } ld_indexing;
    };

    typedef std::vector<logfile_data>::iterator iterator;

    iterator begin()
    {
        return this->lss_files.begin();
    };

    iterator end()
    {
        return this->lss_files.end();
    };

    content_line_t get_file_base_content_line(iterator iter) {
        int index = std::distance(this->begin(), iter);

        return content_line_t(index * MAX_LINES_PER_FILE);
    };

    static const uint64_t MAX_CONTENT_LINES = (1ULL << 40) - 1;
    static const uint64_t MAX_LINES_PER_FILE = 256 * 1024 * 1024;
    static const uint64_t MAX_FILES          = (
        MAX_CONTENT_LINES / MAX_LINES_PER_FILE);

private:
    static const size_t LINE_SIZE_CACHE_SIZE = 512;

    enum {
        B_SCRUB,
        B_TIME_OFFSET,
    };

    enum {
        F_SCRUB       = (1L << B_SCRUB),
        F_TIME_OFFSET = (1L << B_TIME_OFFSET),
    };

    struct __attribute__((__packed__)) indexed_content {
        indexed_content(content_line_t cl) : ic_value(cl) {

        };

        operator content_line_t () {
            return content_line_t(this->ic_value);
        };

        uint64_t ic_value : 40;
    };

    struct logline_cmp {
        logline_cmp(logfile_sub_source & lc)
            : llss_controller(lc) { };
        bool operator()(const content_line_t &lhs, const content_line_t &rhs)
        {
            logline *ll_lhs = this->llss_controller.find_line(lhs);
            logline *ll_rhs = this->llss_controller.find_line(rhs);

            return (*ll_lhs) < (*ll_rhs);
        };
#if 0
        bool operator()(const indexed_content &lhs, const indexed_content &rhs)
        {
            logline *ll_lhs = this->llss_controller.find_line(lhs.ic_value);
            logline *ll_rhs = this->llss_controller.find_line(rhs.ic_value);

            return (*ll_lhs) < (*ll_rhs);
        };
#endif
        bool operator()(const content_line_t &lhs, const time_t &rhs)
        {
            logline *ll_lhs = this->llss_controller.find_line(lhs);

            return *ll_lhs < rhs;
        };
        bool operator()(const content_line_t &lhs, const struct timeval &rhs)
        {
            logline *ll_lhs = this->llss_controller.find_line(lhs);

            return *ll_lhs < rhs;
        };

        logfile_sub_source & llss_controller;
    };

    /**
     * Functor for comparing the ld_file field of the logfile_data struct.
     */
    struct logfile_data_eq {
        logfile_data_eq(logfile *lf) : lde_file(lf) { };

        bool operator()(const logfile_data &ld)
        {
            return this->lde_file == ld.ld_file;
        }

        logfile *lde_file;
    };

    void clear_line_size_cache(void) {
        memset(this->lss_line_size_cache, 0, sizeof(this->lss_line_size_cache));
        this->lss_line_size_cache[0].first = -1;
    };

    unsigned long             lss_flags;
    std::vector<logfile_data> lss_files;

    filter_stack_t lss_filters;
    uint8_t        lss_filter_generation;
    int            lss_filtered_count;

    std::vector<indexed_content> lss_index;

    bookmarks<content_line_t>::type lss_user_marks;
    std::map<content_line_t, bookmark_metadata> lss_user_mark_metadata;

    logfile *         lss_token_file;
    std::string       lss_token_value;
    shared_buffer     lss_share_manager;
    int               lss_scrub_len;
    int               lss_token_offset;
    int               lss_token_date_end;
    logfile::iterator lss_token_line;
    std::pair<int, size_t> lss_line_size_cache[LINE_SIZE_CACHE_SIZE];
};
#endif
