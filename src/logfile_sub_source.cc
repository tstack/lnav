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
 */

#include "config.h"

#include <algorithm>

#include "k_merge_tree.h"
#include "lnav_util.hh"
#include "logfile_sub_source.hh"


using namespace std;

#if 0
/* XXX */
class logfile_scrub_map {
public:

    static logfile_scrub_map &singleton()
    {
        static logfile_scrub_map s_lsm;

        return s_lsm;
    };

    const pcre *include(logfile::format_t format) const
    {
        map<logfile::format_t, pcre *>::const_iterator iter;
        pcre *retval = NULL;

        if ((iter = this->lsm_include.find(format)) !=
            this->lsm_include.end()) {
            retval = iter->second;
        }

        return retval;
    };

    const pcre *exclude(logfile::format_t format) const
    {
        map<logfile::format_t, pcre *>::const_iterator iter;
        pcre *retval = NULL;

        if ((iter = this->lsm_exclude.find(format)) !=
            this->lsm_exclude.end()) {
            retval = iter->second;
        }

        return retval;
    };

private:

    pcre *build_pcre(const char *pattern)
    {
        const char *errptr;
        pcre *      retval;
        int         eoff;

        retval = pcre_compile(pattern, PCRE_CASELESS, &errptr, &eoff, NULL);
        if (retval == NULL) {
            throw errptr;
        }

        return retval;
    };

    logfile_scrub_map()
    {
        this->lsm_include[logfile::FORMAT_JBOSS] = this->
                                                   build_pcre(
            "\\d+-(\\d+-\\d+ \\d+:\\d+:\\d+),\\d+ [^ ]+( .*)");
        this->lsm_exclude[logfile::FORMAT_JBOSS] = this->
                                                   build_pcre("(?:"
                                                              "\\[((?:[0-9a-zA-Z]+\\.)+)\\w+"
                                                              "|\\[[\\w: ]+ ((?:[0-9a-zA-Z]+\\.)+)\\w+[^ \\]]+"
                                                              "| ((?:[0-9a-zA-Z]+\\.)+)\\w+\\])");

        this->lsm_include[logfile::FORMAT_SYSLOG] = this->
                                                    build_pcre(
            "(\\w+\\s[\\s\\d]\\d \\d+:\\d+:\\d+) \\w+( .*)");
    };

    map<logfile::format_t, pcre *> lsm_include;
    map<logfile::format_t, pcre *> lsm_exclude;
};
#endif

bookmark_type_t logfile_sub_source::BM_ERRORS;
bookmark_type_t logfile_sub_source::BM_WARNINGS;
bookmark_type_t logfile_sub_source::BM_FILES;

logfile_sub_source::logfile_sub_source()
    : lss_flags(0),
      lss_filter_generation(1),
      lss_filtered_count(0)
{}

logfile_sub_source::~logfile_sub_source()
{ }

logfile *logfile_sub_source::find(const char *fn,
                                  content_line_t &line_base)
{
    std::vector<logfile_data>::iterator iter;
    logfile *retval = NULL;

    line_base = content_line_t(0);
    for (iter = this->lss_files.begin();
         iter != this->lss_files.end() && retval == NULL;
         iter++) {
        if (strcmp(iter->ld_file->get_filename().c_str(), fn) == 0) {
            retval = iter->ld_file;
        }
        else {
            line_base += content_line_t(MAX_LINES_PER_FILE);
        }
    }

    return retval;
}

vis_line_t logfile_sub_source::find_from_time(time_t start)
{
    vector<content_line_t>::iterator lb;
    vis_line_t retval(-1);

    lb = lower_bound(this->lss_index.begin(),
                     this->lss_index.end(),
                     start,
                     logline_cmp(*this));
    if (lb != this->lss_index.end()) {
        retval = vis_line_t(lb - this->lss_index.begin());
    }

    return retval;
}

void logfile_sub_source::text_value_for_line(textview_curses &tc,
                                             int row,
                                             string &value_out,
                                             bool raw)
{
    content_line_t line(0);

    size_t tab;

    assert(row >= 0);
    assert((size_t)row < this->lss_index.size());

    line = this->lss_index[row];
    this->lss_token_file   = this->find(line);
    this->lss_token_line   = this->lss_token_file->begin() + line;
    this->lss_token_offset = 0;
    this->lss_scrub_len    = 0;

    if (raw) {
        this->lss_token_file->read_line(this->lss_token_line, value_out);
        return;
    }

    this->lss_token_value =
        this->lss_token_file->read_line(this->lss_token_line);

    while ((tab = this->lss_token_value.find('\t')) != string::npos) {
        this->lss_token_value = this->lss_token_value.replace(tab, 1, 8, ' ');
    }

    this->lss_token_date_end = 0;
    value_out = this->lss_token_value;
    if (this->lss_flags & F_SCRUB) {
        log_format *lf = this->lss_token_file->get_format();

        lf->scrub(value_out);
    }

    if (this->lss_token_file->is_time_adjusted() &&
        !(this->lss_token_line->get_level() & logline::LEVEL_CONTINUED)) {
        log_format *format = this->lss_token_file->get_format();

        if (format->lf_date_time.dts_fmt_lock != -1) {
            std::vector<logline_value> line_values;
            struct timeval adjusted_time;
            string_attrs_t sa;
            char buffer[128];
            const char *fmt;

            fmt = std_time_fmt[format->lf_date_time.dts_fmt_lock];
            adjusted_time = this->lss_token_line->get_timeval();
            strftime(buffer, sizeof(buffer),
                     fmt,
                     gmtime(&adjusted_time.tv_sec));

            format->annotate(this->lss_token_value, sa, line_values);

            struct line_range time_range;

            time_range = find_string_attr_range(sa, "timestamp");
            if (time_range.lr_start != -1) {
                const char *last = value_out.c_str();
                int len = strlen(buffer);

                if ((last[time_range.lr_start + len] == '.' ||
                    last[time_range.lr_start + len] == ',') &&
                    len + 4 <= time_range.length()) {
                    snprintf(&buffer[len], sizeof(buffer) - len,
                             ".%03d",
                             this->lss_token_line->get_millis());
                }
                value_out.replace(time_range.lr_start,
                                  strlen(buffer),
                                  string(buffer));
            }
        }
    }

    if (this->lss_flags & F_TIME_OFFSET) {
        long long start_millis, curr_millis;

        vis_line_t prev_mark =
            tc.get_bookmarks()[&textview_curses::BM_USER].prev(vis_line_t(row));
        if (prev_mark == -1) {
            prev_mark = vis_line_t(0);
        }

        logline *first_line = this->find_line(this->at(prev_mark));
        start_millis = first_line->get_time() * 1000 +
                       first_line->get_millis();
        curr_millis = this->lss_token_line->get_time() * 1000 +
                      this->lss_token_line->get_millis();
        long long diff = curr_millis - start_millis;

        /* 24h22m33s111 */

        static struct rel_interval {
            long long   length;
            const char *format;
            const char *symbol;
        } intervals[] = {
            { 1000, "%03qd%s", ""  },
            {   60, "%qd%s",   "s" },
            {   60, "%qd%s",   "m" },
            {    0, "%qd%s",   "h" },
            {    0, NULL }
        };

        struct rel_interval *curr_interval;
        int rel_length = 0;

        value_out = "|" + value_out;
        for (curr_interval = intervals; curr_interval->symbol != NULL;
             curr_interval++) {
            long long amount;
            char      segment[32];

            if (curr_interval->length) {
                amount = diff % curr_interval->length;
                diff   = diff / curr_interval->length;
            }
            else {
                amount = diff;
                diff   = 0;
            }

            if (!amount && !diff) {
                break;
            }

            snprintf(segment, sizeof(segment), curr_interval->format, amount,
                     curr_interval->symbol);
            rel_length += strlen(segment);
            value_out   = segment + value_out;
        }
        if (rel_length < 12) {
            value_out.insert(0, 12 - rel_length, ' ');
        }
    }
}

void logfile_sub_source::text_attrs_for_line(textview_curses &lv,
                                             int row,
                                             string_attrs_t &value_out)
{
    view_colors &     vc        = view_colors::singleton();
    logline *         next_line = NULL;
    struct line_range lr;
    int time_offset_end = 0;
    int attrs           = 0;

    switch (this->lss_token_line->get_level() & ~logline::LEVEL__FLAGS) {
    case logline::LEVEL_FATAL:
    case logline::LEVEL_CRITICAL:
    case logline::LEVEL_ERROR:
        attrs = vc.attrs_for_role(view_colors::VCR_ERROR);
        break;

    case logline::LEVEL_WARNING:
        attrs = vc.attrs_for_role(view_colors::VCR_WARNING);
        break;

    default:
        attrs = vc.attrs_for_role(view_colors::VCR_TEXT);
        break;
    }

    if ((row + 1) < (int)this->lss_index.size()) {
        next_line = this->find_line(this->lss_index[row + 1]);
    }

    if (next_line != NULL &&
        (day_num(next_line->get_time()) >
         day_num(this->lss_token_line->get_time()))) {
        attrs |= A_UNDERLINE;
    }

    log_format *format = this->lss_token_file->get_format();
    std::vector<logline_value> line_values;
    
    if (!(this->lss_token_line->get_level() & logline::LEVEL_CONTINUED)) {
        format->annotate(this->lss_token_value, value_out, line_values);
    }

    lr.lr_start = time_offset_end + this->lss_token_date_end;
    lr.lr_end   = -1;

    value_out[lr].insert(make_string_attr("style", attrs));

    if (this->lss_flags & F_TIME_OFFSET) {
        time_offset_end = 13;
        lr.lr_start     = 0;
        lr.lr_end       = time_offset_end;

        for (string_attrs_t::iterator iter = value_out.begin();
             iter != value_out.end();
             ++iter) {
            struct line_range *existing_lr = (line_range *)&iter->first;

            existing_lr->lr_start += time_offset_end;
            if (existing_lr->lr_end != -1)
                existing_lr->lr_end += time_offset_end;
        }

        attrs = vc.attrs_for_role(view_colors::VCR_OK);
        value_out[lr].insert(make_string_attr("style", attrs));
    }

    lr.lr_start = 0;
    lr.lr_end   = -1;
    value_out[lr].insert(make_string_attr("file", this->lss_token_file));

    {
        bookmark_vector<vis_line_t> &bv = lv.get_bookmarks()[&textview_curses::BM_USER];
        bookmark_vector<vis_line_t>::iterator bv_iter;

        bv_iter = lower_bound(bv.begin(), bv.end(), vis_line_t(row + 1));
        if (bv_iter != bv.begin()) {
            --bv_iter;
            content_line_t part_start_line = this->at(*bv_iter);
            std::map<content_line_t, bookmark_metadata>::iterator bm_iter;

            if ((bm_iter = this->lss_user_mark_metadata.find(part_start_line))
                != this->lss_user_mark_metadata.end()) {
                lr.lr_start = 0;
                lr.lr_end   = -1;
                value_out[lr].insert(make_string_attr("partition", &bm_iter->second));
            }
        }
    }

    if (this->lss_token_file->is_time_adjusted()) {
        struct line_range time_range = find_string_attr_range(value_out,
                                                              "timestamp");

        if (time_range.lr_end != -1) {
            attrs = vc.attrs_for_role(view_colors::VCR_ADJUSTED_TIME);
            value_out[time_range].insert(make_string_attr("style", attrs));
        }
    }
    else if ((((this->lss_token_line->get_time() / (5 * 60)) % 2) == 0) &&
             !(this->lss_token_line->get_level() & logline::LEVEL_CONTINUED)) {
        struct line_range time_range = find_string_attr_range(value_out,
                                                              "timestamp");

        if (time_range.lr_end != -1) {
            time_range.lr_start += time_offset_end;
            time_range.lr_end   += time_offset_end;
            attrs = vc.attrs_for_role(view_colors::VCR_ALT_ROW);
            value_out[time_range].insert(make_string_attr("style", attrs));
        }
    }
}

bool logfile_sub_source::rebuild_index(observer *obs, bool force)
{
    std::vector<logfile_data>::iterator iter;
    bool retval = force;
    int file_count = 0;

    for (iter = this->lss_files.begin();
         iter != this->lss_files.end();
         iter++) {
        if (iter->ld_file == NULL) {
            if (iter->ld_lines_indexed > 0) {
                force  = true;
                retval = true;
            }
        }
        else {
            if (iter->ld_file->rebuild_index(obs)) {
                retval = true;
            }
            file_count += 1;
        }
    }
    if (force) {
        for (iter = this->lss_files.begin();
             iter != this->lss_files.end();
             iter++) {
            iter->ld_lines_indexed = 0;
        }
    }

    if (retval || force) {
        size_t index_size = 0;

        if (force) {
            this->lss_index.clear();
            this->lss_filtered_count = 0;
        }

        kmerge_tree_c<logline, logfile_data, logfile::iterator> merge(file_count);

        for (iter = this->lss_files.begin();
             iter != this->lss_files.end();
             iter++) {
            if (iter->ld_file == NULL)
                continue;

            merge.add(&(*iter),
                      iter->ld_file->begin() + iter->ld_lines_indexed,
                      iter->ld_file->end());
            index_size += iter->ld_file->size();
        }

        this->lss_index.reserve(index_size);

        logfile_filter::type_t action_for_prev_line = logfile_filter::MAYBE;
        logfile_data *last_owner = NULL;

        merge.execute();
        for (;;) {
            logfile::iterator lf_iter;
            logfile_data *ld;

            if (!merge.get_top(ld, lf_iter)) {
                break;
            }

            int file_index = ld - &(*this->lss_files.begin());
            int line_index = lf_iter - ld->ld_file->begin();

            content_line_t con_line(file_index * MAX_LINES_PER_FILE +
                                    line_index);

            if (obs != NULL) {
                obs->logfile_sub_source_filtering(
                    *this,
                    content_line_t(con_line % MAX_LINES_PER_FILE),
                    ld->ld_file->size());
            }

            if (!(lf_iter->get_level() & logline::LEVEL_CONTINUED)) {
                if (action_for_prev_line == logfile_filter::INCLUDE) {
                    while (last_owner->ld_indexing.ld_start <=
                           last_owner->ld_indexing.ld_last) {
                        this->lss_index.push_back(last_owner->ld_indexing.ld_start);
                        ++last_owner->ld_indexing.ld_start;
                    }
                }
                else if (action_for_prev_line == logfile_filter::EXCLUDE) {
                    this->lss_filtered_count += 1;
                }
                ld->ld_indexing.ld_start = con_line;
            }

            ld->ld_indexing.ld_last = con_line;
            action_for_prev_line = ld->ld_file->check_filter(
                lf_iter, this->lss_filter_generation, this->lss_filters);

            last_owner = ld;

            merge.next();
        }

        if (action_for_prev_line == logfile_filter::INCLUDE) {
            while (last_owner->ld_indexing.ld_start <=
                   last_owner->ld_indexing.ld_last) {
                this->lss_index.push_back(last_owner->ld_indexing.ld_start);
                ++last_owner->ld_indexing.ld_start;
            }
        }
        else if (action_for_prev_line == logfile_filter::EXCLUDE) {
            this->lss_filtered_count += 1;
        }

        for (iter = this->lss_files.begin();
             iter != this->lss_files.end();
             iter++) {
            if (iter->ld_file == NULL)
                continue;

            iter->ld_lines_indexed = iter->ld_file->size();
        }
    }

    return retval;
}

void logfile_sub_source::text_update_marks(vis_bookmarks &bm)
{
    logfile *  last_file = NULL;
    vis_line_t vl;

    bm[&BM_WARNINGS].clear();
    bm[&BM_ERRORS].clear();
    bm[&BM_FILES].clear();

    for (bookmarks<content_line_t>::type::iterator iter =
             this->lss_user_marks.begin();
         iter != this->lss_user_marks.end();
         ++iter) {
        bm[iter->first].clear();
    }

    for (; vl < (int)this->lss_index.size(); ++vl) {
        content_line_t cl = this->lss_index[vl];
        logfile *      lf;

        for (bookmarks<content_line_t>::type::iterator iter =
                 this->lss_user_marks.begin();
             iter != this->lss_user_marks.end();
             ++iter) {
            if (binary_search(iter->second.begin(), iter->second.end(), cl)) {
                bm[iter->first].insert_once(vl);
            }
        }

        lf = this->find(cl);

        if (lf != last_file) {
            bm[&BM_FILES].insert_once(vl);
        }

        switch ((*lf)[cl].get_level() & ~logline::LEVEL_MULTILINE) {
        case logline::LEVEL_WARNING:
            bm[&BM_WARNINGS].insert_once(vl);
            break;

        case logline::LEVEL_FATAL:
        case logline::LEVEL_ERROR:
        case logline::LEVEL_CRITICAL:
            bm[&BM_ERRORS].insert_once(vl);
            break;

        default:
            break;
        }

        last_file = lf;
    }
}

#if 0
void logfile_sub_source::handle_scroll(listview_curses *lc)
{
    printf("hello, world!\n");
    return;

    vis_line_t top = lc->get_top();

    if (this->lss_index.empty()) {
        this->lss_top_time    = -1;
        this->lss_bottom_time = -1;
    }
    else {
        time_t     top_day, bottom_day, today, yesterday, now = time(NULL);
        vis_line_t bottom(0), height(0);

        unsigned long width;
        char          status[32];
        logline *     ll;

        today     = day_num(now);
        yesterday = today - 1;

        lc->get_dimensions(height, width);

        ll = this->find_line(this->lss_index[top]);
        this->lss_top_time = ll->get_time();

        bottom = min(top + height - vis_line_t(1),
                     vis_line_t(this->lss_index.size() - 1));

        ll = this->find_line(this->lss_index[bottom]);
        this->lss_bottom_time = ll->get_time();

        top_day    = day_num(this->lss_top_time);
        bottom_day = day_num(this->lss_bottom_time);
        if (top_day == today) {
            snprintf(status, sizeof(status), "Today");
        }
        else if (top_day == yesterday) {
            snprintf(status, sizeof(status), "Yesterday");
        }
        else {
            strftime(status, sizeof(status),
                     "%a %b %d",
                     gmtime(&this->lss_top_time));
        }
        if (top_day != bottom_day) {
            int len = strlen(status);

            if (bottom_day == today) {
                snprintf(&status[len], sizeof(status) - len, " - Today");
            }
            else if (bottom_day == yesterday) {
                snprintf(&status[len], sizeof(status) - len, " - Yesterday");
            }
            else {
                strftime(&status[len], sizeof(status) - len,
                         " - %b %d",
                         gmtime(&this->lss_bottom_time));
            }
        }

        this->lss_date_field.set_value(string(status));
    }
}
#endif
