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

#ifndef __db_sub_source_hh
#define __db_sub_source_hh

#include <string>
#include <vector>
#include <iterator>
#include <algorithm>

#include "json_ptr.hh"
#include "listview_curses.hh"
#include "hist_source.hh"
#include "log_vtab_impl.hh"

class db_label_source : public text_sub_source {
public:
    db_label_source() { };

    ~db_label_source() { };

    size_t text_line_count() {
        return this->dls_rows.size();
    };

    size_t text_size_for_line(textview_curses &tc, int line, bool raw) {
        return this->text_line_width();
    };

    size_t text_line_width() {
        size_t retval = 0;

        for (std::vector<size_t>::iterator iter = this->dls_column_sizes.begin();
             iter != this->dls_column_sizes.end();
             ++iter) {
            retval += *iter;
        }
        return retval;
    };

    void text_value_for_line(textview_curses &tc,
                             int row,
                             std::string &label_out,
                             bool raw)
    {
        /*
         * start_value is the result rowid, each bucket type is a column value
         * label_out should be the raw text output.
         */

        label_out.clear();
        if (row >= (int)this->dls_rows.size()) {
            return;
        }
        for (int lpc = 0; lpc < (int)this->dls_rows[row].size();
             lpc++) {
            int padding = (this->dls_column_sizes[lpc] -
                           strlen(this->dls_rows[row][lpc]) -
                           1);

            if (this->dls_column_types[lpc] != SQLITE3_TEXT) {
                label_out.append(padding, ' ');
            }
            label_out.append(this->dls_rows[row][lpc]);
            if (this->dls_column_types[lpc] == SQLITE3_TEXT) {
                label_out.append(padding, ' ');
            }
            label_out.append(1, ' ');
        }
    };

    void text_attrs_for_line(textview_curses &tc, int row, string_attrs_t &sa)
    {
        struct line_range lr(0, 0);
        struct line_range lr2(0, -1);

        if (row >= (int)this->dls_rows.size()) {
            return;
        }
        for (size_t lpc = 0; lpc < this->dls_column_sizes.size() - 1; lpc++) {
            if (row % 2 == 0) {
                sa.push_back(string_attr(lr2, &view_curses::VC_STYLE, A_BOLD));
            }
            lr.lr_start += this->dls_column_sizes[lpc] - 1;
            lr.lr_end = lr.lr_start + 1;
            sa.push_back(string_attr(lr, &view_curses::VC_GRAPHIC, ACS_VLINE));
            lr.lr_start += 1;
        }

        int left = 0;
        for (size_t lpc = 0; lpc < this->dls_column_sizes.size(); lpc++) {
            const char *row_value = this->dls_rows[row][lpc];
            size_t row_len = strlen(row_value);

            if (this->dls_headers_to_graph[lpc]) {
                double num_value;

                if (sscanf(row_value, "%lf", &num_value) == 1) {
                    this->dls_chart.chart_attrs_for_value(tc, left, this->dls_headers[lpc], num_value, sa);
                }
            }
            if (row_len > 2 &&
                ((row_value[0] == '{' && row_value[row_len - 1] == '}') ||
                 (row_value[0] == '[' && row_value[row_len - 1] == ']'))) {
                json_ptr_walk jpw;

                if (jpw.parse(row_value, row_len) == yajl_status_ok &&
                    jpw.complete_parse() == yajl_status_ok) {
                    for (json_ptr_walk::walk_list_t::iterator iter = jpw.jpw_values.begin();
                         iter != jpw.jpw_values.end();
                         ++iter) {
                        double num_value;

                        if (iter->wt_type == yajl_t_number &&
                            sscanf(iter->wt_value.c_str(), "%lf", &num_value) == 1) {
                            this->dls_chart.chart_attrs_for_value(tc, left, iter->wt_ptr, num_value, sa);
                        }
                    }
                }
            }
        }
    }

    /* TODO: add support for left and right justification... numbers should */
    /* be right justified and strings should be left. */
    void push_column(const char *colstr)
    {
        int index = this->dls_rows.back().size();

        if (colstr == NULL) {
            colstr = NULL_STR;
        }
        else {
            colstr = strdup(colstr);
            if (colstr == NULL) {
                throw "out of memory";
            }
        }

        this->dls_rows.back().push_back(colstr);
        if (this->dls_rows.back().size() > this->dls_column_sizes.size()) {
            this->dls_column_sizes.push_back(1);
        }
        this->dls_column_sizes[index] =
            std::max(this->dls_column_sizes[index], strlen(colstr) + 1);
    };

    void push_header(const std::string &colstr, int type, bool graphable)
    {
        int index = this->dls_headers.size();

        this->dls_headers.push_back(colstr);
        if (this->dls_headers.size() > this->dls_column_sizes.size()) {
            this->dls_column_sizes.push_back(1);
        }
        this->dls_column_sizes[index] =
            std::max(this->dls_column_sizes[index], colstr.length() + 1);
        this->dls_column_types.push_back(type);
        this->dls_headers_to_graph.push_back(graphable);
    }

    void clear(void)
    {
        this->dls_chart.clear();
        this->dls_headers.clear();
        this->dls_headers_to_graph.clear();
        this->dls_column_types.clear();
        for (size_t row = 0; row < this->dls_rows.size(); row++) {
            for (size_t col = 0; col < this->dls_rows[row].size(); col++) {
                if (this->dls_rows[row][col] != NULL_STR) {
                    free((void *)this->dls_rows[row][col]);
                }
            }
        }
        this->dls_rows.clear();
        this->dls_column_sizes.clear();
    }

    long column_name_to_index(const std::string &name) const {
        std::vector<std::string>::const_iterator iter;

        iter = std::find(this->dls_headers.begin(),
                         this->dls_headers.end(),
                         name);
        if (iter == this->dls_headers.end()) {
            return -1;
        }

        return std::distance(this->dls_headers.begin(), iter);
    }

    stacked_bar_chart<std::string> dls_chart;
    std::vector<std::string> dls_headers;
    std::vector<int>         dls_headers_to_graph;
    std::vector<int>         dls_column_types;
    std::vector<std::vector<const char *> > dls_rows;
    std::vector<size_t> dls_column_sizes;

    static const char *NULL_STR;
};

class db_overlay_source : public list_overlay_source {
public:
    db_overlay_source() : dos_active(false), dos_labels(NULL) {

    };

    size_t list_overlay_count(const listview_curses &lv)
    {
        size_t retval = 1;

        if (!this->dos_active || lv.get_inner_height() == 0) {
            return retval;
        }

        view_colors &vc = view_colors::singleton();
        vis_line_t top = lv.get_top();
        const std::vector<const char *> &cols = this->dos_labels->dls_rows[top];
        unsigned long width;
        vis_line_t height;

        lv.get_dimensions(height, width);

        this->dos_lines.clear();
        for (size_t col = 0; col < cols.size(); col++) {
            const char *col_value = cols[col];
            size_t col_len = strlen(col_value);

            if (!(col_len >= 2 &&
                  ((col_value[0] == '{' && col_value[col_len - 1] == '}') ||
                   (col_value[0] == '[' && col_value[col_len - 1] == ']')))) {
                continue;
            }

            json_ptr_walk jpw;

            if (jpw.parse(col_value, col_len) == yajl_status_ok &&
                jpw.complete_parse() == yajl_status_ok) {

                {
                    const std::string &header = this->dos_labels->dls_headers[col];
                    this->dos_lines.push_back(" JSON Column: " + header);

                    retval += 1;
                }

                stacked_bar_chart<std::string> chart;
                int start_line = this->dos_lines.size();

                chart.with_stacking_enabled(false)
                        .with_margins(3, 0);

                for (json_ptr_walk::walk_list_t::iterator iter = jpw.jpw_values.begin();
                     iter != jpw.jpw_values.end();
                     ++iter) {
                    this->dos_lines.push_back("   " + iter->wt_ptr + " = " + iter->wt_value);

                    string_attrs_t &sa = this->dos_lines.back().get_attrs();
                    struct line_range lr(1, 2);

                    sa.push_back(string_attr(lr, &view_curses::VC_GRAPHIC, ACS_LTEE));
                    lr.lr_start = 3 + iter->wt_ptr.size() + 3;
                    lr.lr_end = -1;
                    sa.push_back(string_attr(lr, &view_curses::VC_STYLE, A_BOLD));

                    double num_value = 0.0;

                    if (iter->wt_type == yajl_t_number &&
                        sscanf(iter->wt_value.c_str(), "%lf", &num_value) == 1) {
                        int attrs = vc.attrs_for_ident(iter->wt_ptr);

                        chart.add_value(iter->wt_ptr, num_value);
                        chart.with_attrs_for_ident(iter->wt_ptr, attrs);
                    }

                    retval += 1;
                }

                int curr_line = start_line;
                for (json_ptr_walk::walk_list_t::iterator iter = jpw.jpw_values.begin();
                     iter != jpw.jpw_values.end();
                     ++iter, curr_line++) {
                    double num_value = 0.0;

                    if (iter->wt_type == yajl_t_number &&
                        sscanf(iter->wt_value.c_str(), "%lf", &num_value) == 1) {
                        string_attrs_t &sa = this->dos_lines[curr_line].get_attrs();
                        int left = 3;

                        chart.chart_attrs_for_value(lv, left, iter->wt_ptr, num_value, sa);
                    }
                }
            }
        }

        if (retval > 1) {
            this->dos_lines.push_back("");

            string_attrs_t &sa = this->dos_lines.back().get_attrs();
            struct line_range lr(1, 2);

            sa.push_back(string_attr(lr, &view_curses::VC_GRAPHIC, ACS_LLCORNER));
            lr.lr_start = 2;
            lr.lr_end = -1;
            sa.push_back(string_attr(lr, &view_curses::VC_GRAPHIC, ACS_HLINE));

            retval += 1;
        }

        return retval;
    };

    bool list_value_for_overlay(const listview_curses &lv,
                                vis_line_t y,
                                attr_line_t &value_out)
    {
        view_colors &vc = view_colors::singleton();

        if (y == 0) {
            std::string &line = value_out.get_string();
            db_label_source *dls = this->dos_labels;
            string_attrs_t &sa = value_out.get_attrs();

            for (size_t lpc = 0;
                 lpc < this->dos_labels->dls_column_sizes.size();
                 lpc++) {
                int before, total_fill =
                        dls->dls_column_sizes[lpc] -
                        dls->dls_headers[lpc].length();

                struct line_range header_range(line.length(),
                                               line.length() +
                                               dls->dls_column_sizes[lpc]);

                int attrs =
                        vc.attrs_for_ident(dls->dls_headers[lpc]) | A_UNDERLINE;
                if (!this->dos_labels->dls_headers_to_graph[lpc]) {
                    attrs = A_UNDERLINE;
                }
                sa.push_back(string_attr(header_range, &view_curses::VC_STYLE,
                                         attrs));

                before = total_fill / 2;
                total_fill -= before;
                line.append(before, ' ');
                line.append(dls->dls_headers[lpc]);
                line.append(total_fill, ' ');
            }

            struct line_range lr(0);

            sa.push_back(string_attr(lr, &view_curses::VC_STYLE,
                                     A_BOLD | A_UNDERLINE));
            return true;
        }
        else if (this->dos_active && y >= 2 && y < (this->dos_lines.size() + 2)) {
            value_out = this->dos_lines[y - 2];
            return true;
        }

        return false;
    };

    bool dos_active;
    db_label_source *dos_labels;
    std::vector<attr_line_t> dos_lines;
};
#endif
