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
#include <algorithm>

#include "listview_curses.hh"
#include "hist_source.hh"
#include "log_vtab_impl.hh"

class db_label_source : public hist_source::label_source {
public:
    db_label_source() { };

    ~db_label_source() { };

    void hist_label_for_group(int group, std::string &label_out)
    {
        label_out.clear();
    };

    void hist_label_for_bucket(int bucket_start_value,
                               const hist_source::bucket_t &bucket,
                               std::string &label_out)
    {
        /*
         * start_value is the result rowid, each bucket type is a column value
         * label_out should be the raw text output.
         */

        label_out.clear();
        if (bucket_start_value >= (int)this->dls_rows.size()) {
            return;
        }
        for (int lpc = 0; lpc < (int)this->dls_rows[bucket_start_value].size();
             lpc++) {
            int padding = (this->dls_column_sizes[lpc] -
                           this->dls_rows[bucket_start_value][lpc].length() -
                           1);

            if (this->dls_column_types[lpc] != SQLITE3_TEXT) {
                label_out.append(padding, ' ');
            }
            label_out.append(this->dls_rows[bucket_start_value][lpc]);
            if (this->dls_column_types[lpc] == SQLITE3_TEXT) {
                label_out.append(padding, ' ');
            }
            label_out.append(1, ' ');
        }
    };

    void hist_attrs_for_bucket(int bucket_start_value,
                               const hist_source::bucket_t &bucket,
                               string_attrs_t &sa)
    {
        struct line_range lr(0, 0);
        struct line_range lr2(0, -1);

        if (bucket_start_value >= (int)this->dls_rows.size()) {
            return;
        }
        for (size_t lpc = 0; lpc < this->dls_column_sizes.size() - 1; lpc++) {
            if (bucket_start_value % 2 == 0) {
                sa[lr2].insert(make_string_attr("style", A_BOLD));
            }
            lr.lr_start += this->dls_column_sizes[lpc] - 1;
            lr.lr_end    = lr.lr_start + 1;
            sa[lr].insert(make_string_attr("graphic", ACS_VLINE));
            lr.lr_start += 1;
        }
    }

    /* TODO: add support for left and right justification... numbers should */
    /* be right justified and strings should be left. */
    void push_column(const char *colstr)
    {
        int index = this->dls_rows.back().size();

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
        this->dls_headers.clear();
        this->dls_headers_to_graph.clear();
        this->dls_column_types.clear();
        this->dls_rows.clear();
        this->dls_column_sizes.clear();
    }

    std::vector<std::string> dls_headers;
    std::vector<bool>        dls_headers_to_graph;
    std::vector<int>         dls_column_types;
    std::vector<std::vector<std::string> > dls_rows;
    std::vector<size_t> dls_column_sizes;
};

class db_overlay_source : public list_overlay_source {
public:
    db_overlay_source() : dos_labels(NULL) { };

    size_t list_overlay_count(const listview_curses &lv)
    {
        return 1;
    };

    bool list_value_for_overlay(const listview_curses &lv,
                                vis_line_t y,
                                attr_line_t &value_out)
    {
        view_colors &vc = view_colors::singleton();

        if (y != 0) {
            return false;
        }

        std::string &    line = value_out.get_string();
        db_label_source *dls  = this->dos_labels;

        for (size_t lpc = 0;
             lpc < this->dos_labels->dls_column_sizes.size();
             lpc++) {
            int before, total_fill =
                dls->dls_column_sizes[lpc] - dls->dls_headers[lpc].length();

            struct line_range header_range(line.length(),
                                           line.length() + dls->dls_column_sizes[lpc]);

            int attrs =
                vc.attrs_for_role(this->dos_hist_source->get_role_for_type(
                                      bucket_type_t(
                                          lpc)))
                | A_UNDERLINE;
            if (!this->dos_labels->dls_headers_to_graph[lpc]) {
                attrs = A_UNDERLINE;
            }
            value_out.get_attrs()[header_range].insert(make_string_attr("style",
                                                                        attrs));

            before      = total_fill / 2;
            total_fill -= before;
            line.append(before, ' ');
            line.append(dls->dls_headers[lpc]);
            line.append(total_fill, ' ');
        }

        struct line_range lr(0);

        value_out.get_attrs()[lr].insert(make_string_attr("style", A_BOLD |
                                                          A_UNDERLINE));

        return true;
    };

    db_label_source *dos_labels;
    hist_source *    dos_hist_source;
};
#endif
