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
 * @file log_data_table.hh
 */

#ifndef _log_data_table_hh
#define _log_data_table_hh

#include <string>
#include <vector>

#include "logfile.hh"
#include "logfile_sub_source.hh"
#include "data_parser.hh"
#include "column_namer.hh"
#include "log_vtab_impl.hh"

class log_data_table : public log_vtab_impl {
public:

    log_data_table(logfile_sub_source &lss,
                   log_vtab_manager &lvm,
                   content_line_t template_line,
                   intern_string_t table_name)
        : log_vtab_impl(table_name),
          ldt_log_source(lss),
          ldt_template_line(template_line),
          ldt_parent_column_count(0),
          ldt_instance(-1) {
        std::shared_ptr<logfile> lf = lss.find(template_line);
        log_format *format = lf->get_format();

        this->vi_supports_indexes = false;
        this->ldt_format_impl = lvm.lookup_impl(format->get_name());
        this->get_columns_int(this->ldt_cols);
    };

    void get_columns_int(std::vector<vtab_column> &cols)
    {
        content_line_t cl_copy = this->ldt_template_line;
        std::shared_ptr<logfile> lf = this->ldt_log_source.find(cl_copy);
        struct line_range          body;
        string_attrs_t             sa;
        std::vector<logline_value> line_values;
        log_format *format = lf->get_format();
        shared_buffer_ref line;

        if (this->ldt_format_impl != NULL) {
            this->ldt_format_impl->get_columns(cols);
        }
        this->ldt_parent_column_count = cols.size();
        lf->read_full_message(lf->begin() + cl_copy, line);
        format->annotate(line, sa, line_values);
        body = find_string_attr_range(sa, &textview_curses::SA_BODY);
        if (body.lr_end == -1) {
            this->ldt_schema_id.clear();
            return;
        }

        data_scanner ds(line, body.lr_start, body.lr_end);
        data_parser  dp(&ds);
        column_namer cn;

        dp.parse();

        cols.emplace_back("log_msg_instance", SQLITE_INTEGER, nullptr);
        for (auto pair_iter = dp.dp_pairs.begin();
             pair_iter != dp.dp_pairs.end();
             ++pair_iter) {
            std::string key_str = dp.get_element_string(
                pair_iter->e_sub_elements->front());
            std::string colname  = cn.add_column(key_str);
            int         sql_type = SQLITE3_TEXT;
            const char *collator = NULL;

            switch (pair_iter->e_sub_elements->back().value_token()) {
            case DT_IPV4_ADDRESS:
            case DT_IPV6_ADDRESS:
                collator = "ipaddress";
                break;

            case DT_NUMBER:
                sql_type = SQLITE_FLOAT;
                break;

            default:
                collator = "naturalnocase";
                break;
            }
            cols.emplace_back(colname, sql_type, collator);
        }
        this->ldt_schema_id = dp.dp_schema_id;
    };

    void get_columns(std::vector<vtab_column> &cols) const {
        cols = this->ldt_cols;
    };

    void get_foreign_keys(std::vector<std::string> &keys_inout) const
    {
        log_vtab_impl::get_foreign_keys(keys_inout);
        keys_inout.emplace_back("log_msg_instance");
    };

    bool next(log_cursor &lc, logfile_sub_source &lss)
    {
        if (lc.lc_curr_line == vis_line_t(-1)) {
            this->ldt_instance = -1;
        }

        lc.lc_curr_line = lc.lc_curr_line + vis_line_t(1);
        lc.lc_sub_index = 0;

        if (lc.lc_curr_line == (int)lss.text_line_count()) {
            return true;
        }

        content_line_t cl;

        cl = lss.at(lc.lc_curr_line);
        std::shared_ptr<logfile> lf = lss.find(cl);
        auto lf_iter = lf->begin() + cl;

        if (lf_iter->is_continued()) {
            return false;
        }

        if (lf_iter->has_schema() &&
            !lf_iter->match_schema(this->ldt_schema_id)) {
            return false;
        }

        string_attrs_t             sa;
        struct line_range          body;
        std::vector<logline_value> line_values;

        lf->read_full_message(lf_iter, this->ldt_current_line);
        lf->get_format()->annotate(this->ldt_current_line, sa, line_values);
        body = find_string_attr_range(sa, &textview_curses::SA_BODY);
        if (body.lr_end == -1) {
            return false;
        }

        data_scanner ds(this->ldt_current_line, body.lr_start, body.lr_end);
        data_parser  dp(&ds);
        dp.parse();

        lf_iter->set_schema(dp.dp_schema_id);

        /* The cached schema ID in the log line is not complete, so we still */
        /* need to check for a full match. */
        if (dp.dp_schema_id != this->ldt_schema_id) {
            return false;
        }

        this->ldt_pairs.clear();
        this->ldt_pairs.swap(dp.dp_pairs, __FILE__, __LINE__);
        this->ldt_instance += 1;

        return true;
    };

    void extract(std::shared_ptr<logfile> lf,
                 shared_buffer_ref &line,
                 std::vector<logline_value> &values)
    {
        static intern_string_t instance_name = intern_string::lookup("log_msg_instance");

        int next_column = this->ldt_parent_column_count;

        this->ldt_format_impl->extract(lf, line, values);
        values.emplace_back(instance_name, this->ldt_instance);
        logline_value &lv = values.back();
        lv.lv_column = next_column++;
        for (auto &ldt_pair : this->ldt_pairs) {
            const data_parser::element &pvalue = ldt_pair.get_pair_value();

            switch (pvalue.value_token()) {
            case DT_NUMBER: {
                char scan_value[line.length() + 1];
                double d = 0.0;

                memcpy(scan_value,
                    line.get_data() + pvalue.e_capture.c_begin,
                    pvalue.e_capture.length());
                scan_value[pvalue.e_capture.length()] = '\0';
                if (sscanf(scan_value, "%lf", &d) != 1) {
                    d = 0.0;
                }
                values.emplace_back(intern_string::lookup("", 0), d);
            }
            break;

            default: {
                shared_buffer_ref value_sbr;

                value_sbr.subset(line,
                    pvalue.e_capture.c_begin, pvalue.e_capture.length());
                values.emplace_back(intern_string::lookup("", 0),
                    logline_value::VALUE_TEXT, value_sbr);
                break;
            }

            }
            values.back().lv_column = next_column++;
        }
    };

private:
    logfile_sub_source &ldt_log_source;
    const content_line_t     ldt_template_line;
    data_parser::schema_id_t ldt_schema_id;
    shared_buffer_ref ldt_current_line;
    data_parser::element_list_t ldt_pairs;
    log_vtab_impl *ldt_format_impl;
    int ldt_parent_column_count;
    int64_t ldt_instance;
    std::vector<vtab_column> ldt_cols;
};

#endif
