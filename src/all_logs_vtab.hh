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
 */

#ifndef LNAV_ALL_LOGS_VTAB_HH
#define LNAV_ALL_LOGS_VTAB_HH

#include "byte_array.hh"
#include "log_vtab_impl.hh"
#include "data_parser.hh"

class all_logs_vtab : public log_vtab_impl {
public:

    all_logs_vtab() : log_vtab_impl(intern_string::lookup("all_logs")) {
        this->alv_value_name = intern_string::lookup("log_format");
        this->alv_msg_name = intern_string::lookup("log_msg_format");
        this->alv_schema_name = intern_string::lookup("log_msg_schema");
    }

    void get_columns(std::vector<vtab_column> &cols) {
        cols.push_back(vtab_column(this->alv_value_name.get()));
        cols.push_back(vtab_column(this->alv_msg_name.get()));
        cols.push_back(vtab_column(this->alv_schema_name.get(), SQLITE3_TEXT, NULL, true));
    };

    void extract(logfile *lf, shared_buffer_ref &line,
                 std::vector<logline_value> &values) {
        log_format *format = lf->get_format();
        values.push_back(logline_value(this->alv_value_name,
                                       format->get_name(), 0));

        std::vector<logline_value> sub_values;
        struct line_range body;
        string_attrs_t sa;

        format->annotate(line, sa, sub_values);

        body = find_string_attr_range(sa, &textview_curses::SA_BODY);
        if (body.lr_start == -1) {
            body.lr_start = 0;
            body.lr_end = line.length();
        }

        data_scanner ds(line, body.lr_start, body.lr_end);
        data_parser dp(&ds);

        std::string str;
        dp.dp_msg_format = &str;
        dp.parse();

        tmp_shared_buffer tsb(str.c_str());

        values.push_back(logline_value(this->alv_msg_name, tsb.tsb_ref, 1));

        this->alv_schema_manager.invalidate_refs();
        dp.dp_schema_id.to_string(this->alv_schema_buffer);
        shared_buffer_ref schema_ref;
        schema_ref.share(this->alv_schema_manager,
                         this->alv_schema_buffer,
                         data_parser::schema_id_t::STRING_SIZE - 1);
        values.push_back(logline_value(this->alv_schema_name, schema_ref, 2));
    }

    bool next(log_cursor &lc, logfile_sub_source &lss) {
        lc.lc_curr_line = lc.lc_curr_line + vis_line_t(1);
        lc.lc_sub_index = 0;

        if (lc.is_eof()) {
            return true;
        }

        content_line_t    cl(lss.at(lc.lc_curr_line));
        logfile *         lf      = lss.find(cl);
        logfile::iterator lf_iter = lf->begin() + cl;

        if (lf_iter->get_level() & logline::LEVEL_CONTINUED) {
            return false;
        }

        return true;
    };

private:
    intern_string_t alv_value_name;
    intern_string_t alv_msg_name;
    intern_string_t alv_schema_name;
    shared_buffer alv_schema_manager;
    char alv_schema_buffer[data_parser::schema_id_t::STRING_SIZE];
};

#endif //LNAV_ALL_LOGS_VTAB_HH
