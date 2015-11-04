/**
 * Copyright (c) 2015, Timothy Stack
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
 * @file log_search_table.hh
 */

#ifndef _log_search_table_hh
#define _log_search_table_hh

#include <string>
#include <vector>

#include "lnav.hh"
#include "logfile.hh"
#include "sql_util.hh"
#include "data_parser.hh"
#include "column_namer.hh"
#include "log_vtab_impl.hh"

class log_search_table : public log_vtab_impl {
public:

    log_search_table(const char *regex, intern_string_t table_name)
        : log_vtab_impl(table_name),
          lst_regex_string(regex),
          lst_regex(regex, PCRE_CASELESS),
          lst_instance(-1) {
        this->vi_supports_indexes = false;
    };

    void get_columns(std::vector<vtab_column> &cols)
    {
        column_namer cn;

        cols.push_back(vtab_column("log_msg_instance", SQLITE_INTEGER, NULL));
        for (int lpc = 0; lpc < this->lst_regex.get_capture_count(); lpc++) {
            std::vector<pcre_context::capture>::const_iterator iter;
            const char *collator = NULL;
            std::string cap_re, colname;
            int sqlite_type = SQLITE3_TEXT;

            if (this->lst_regex.captures().size() == this->lst_regex.get_capture_count()) {
                iter = this->lst_regex.cap_begin() + lpc;
                cap_re = this->lst_regex_string.substr(iter->c_begin,
                                                       iter->length());
                sqlite_type = guess_type_from_pcre(cap_re, &collator);
                switch (sqlite_type) {
                    case SQLITE_FLOAT:
                        this->lst_column_types.push_back(
                                logline_value::VALUE_FLOAT);
                        break;
                    case SQLITE_INTEGER:
                        this->lst_column_types.push_back(
                                logline_value::VALUE_INTEGER);
                        break;
                    default:
                        this->lst_column_types.push_back(
                                logline_value::VALUE_TEXT);
                        break;
                }
            }
            colname = cn.add_column(this->lst_regex.name_for_capture(lpc));
            cols.push_back(vtab_column(colname, sqlite_type, collator));
        }
    };

    void get_foreign_keys(std::vector<std::string> &keys_inout)
    {
        log_vtab_impl::get_foreign_keys(keys_inout);
        keys_inout.push_back("log_msg_instance");
    };

    bool next(log_cursor &lc, logfile_sub_source &lss)
    {
        if (lc.lc_curr_line == vis_line_t(-1)) {
            this->lst_instance = -1;
        }

        lc.lc_curr_line = lc.lc_curr_line + vis_line_t(1);
        lc.lc_sub_index = 0;

        if (lc.lc_curr_line == (int)lss.text_line_count()) {
            return true;
        }

        content_line_t cl;

        cl = lss.at(lc.lc_curr_line);
        logfile *         lf      = lss.find(cl);
        logfile::iterator lf_iter = lf->begin() + cl;

        if (lf_iter->is_continued()) {
            return false;
        }

        string_attrs_t             sa;
        std::vector<logline_value> line_values;

        lf->read_full_message(lf_iter, this->lst_current_line);
        lf->get_format()->annotate(this->lst_current_line, sa, line_values);
        this->lst_body = find_string_attr_range(sa, &textview_curses::SA_BODY);
        if (this->lst_body.lr_end == -1 || this->lst_body.length() == 0) {
            return false;
        }

        pcre_input pi(&this->lst_current_line.get_data()[this->lst_body.lr_start],
                      0,
                      this->lst_body.length());

        if (!this->lst_regex.match(this->lst_match_context, pi)) {
            return false;
        }

        this->lst_instance += 1;

        return true;
    };

    void extract(logfile *lf,
                 shared_buffer_ref &line,
                 std::vector<logline_value> &values)
    {
        static intern_string_t instance_name = intern_string::lookup("log_msg_instance");
        static intern_string_t empty = intern_string::lookup("", 0);

        pcre_input pi(&this->lst_current_line.get_data()[this->lst_body.lr_start],
                      0,
                      this->lst_body.length());
        int next_column = 0;

        values.push_back(logline_value(instance_name, this->lst_instance));
        values.back().lv_column = next_column++;
        for (int lpc = 0; lpc < this->lst_regex.get_capture_count(); lpc++) {
            pcre_context::capture_t *cap = this->lst_match_context[lpc];
            shared_buffer_ref value_sbr;

            value_sbr.subset(line,
                             this->lst_body.lr_start + cap->c_begin,
                             cap->length());
            values.push_back(logline_value(empty,
                                           this->lst_column_types[lpc],
                                           value_sbr));
            values.back().lv_column = next_column++;
        }
    };

private:
    std::string lst_regex_string;
    pcrepp lst_regex;
    shared_buffer_ref lst_current_line;
    struct line_range lst_body;
    pcre_context_static<128> lst_match_context;
    std::vector<logline_value::kind_t> lst_column_types;
    int64_t lst_instance;
};

#endif
