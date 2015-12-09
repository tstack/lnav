/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file log_data_helper.hh
 */

#ifndef __log_data_helper_hh
#define __log_data_helper_hh

#include <map>
#include <string>
#include <memory>

#include <sqlite3.h>

#include "logfile_sub_source.hh"
#include "data_parser.hh"
#include "column_namer.hh"
#include "json_ptr.hh"
#include "lnav_log.hh"
#include "sql_util.hh"

class log_data_helper
{
public:
    log_data_helper(logfile_sub_source &lss)
            : ldh_log_source(lss),
              ldh_file(NULL),
              ldh_y_offset(0)
    {

    };

    void clear() {
        this->ldh_file = NULL;
        this->ldh_msg.disown();
        this->ldh_parser.reset();
        this->ldh_scanner.reset();
        this->ldh_namer.reset();
        this->ldh_json_pairs.clear();
    };

    bool parse_line(vis_line_t line, bool allow_middle = false) {
        return this->parse_line(this->ldh_log_source.at(line), allow_middle);
    }

    bool parse_line(content_line_t line, bool allow_middle = false) {
        logfile::iterator ll;
        bool retval = false;

        this->ldh_source_line = this->ldh_line_index = line;

        this->ldh_file = this->ldh_log_source.find(this->ldh_line_index);
        ll = this->ldh_file->begin() + this->ldh_line_index;
        this->ldh_y_offset = 0;
        while (allow_middle && ll->is_continued()) {
            --ll;
            this->ldh_y_offset += 1;
        }
        this->ldh_line = ll;
        if (ll->is_continued()) {
            this->ldh_parser.reset();
            this->ldh_scanner.reset();
            this->ldh_namer.reset();
            this->ldh_json_pairs.clear();
        }
        else {
            log_format *format = this->ldh_file->get_format();
            struct line_range body;
            string_attrs_t    sa;

            this->ldh_line_values.clear();
            this->ldh_file->read_full_message(ll, this->ldh_msg);
            format->annotate(this->ldh_msg, sa, this->ldh_line_values);

            body = find_string_attr_range(sa, &textview_curses::SA_BODY);
            if (body.lr_start == -1) {
                body.lr_start = this->ldh_msg.length();
                body.lr_end = this->ldh_msg.length();
            }
            this->ldh_scanner.reset(new data_scanner(
                this->ldh_msg, body.lr_start, body.lr_end));
            this->ldh_parser.reset(new data_parser(this->ldh_scanner.get()));
            this->ldh_parser->parse();
            this->ldh_namer.reset(new column_namer());
            this->ldh_json_pairs.clear();

            for (std::vector<logline_value>::iterator iter =
                 this->ldh_line_values.begin();
                 iter != this->ldh_line_values.end();
                 ++iter) {
                switch (iter->lv_kind) {
                case logline_value::VALUE_JSON: {
                    json_ptr_walk jpw;

                    if (jpw.parse(iter->lv_sbr.get_data(), iter->lv_sbr.length()) == yajl_status_ok &&
                        jpw.complete_parse() == yajl_status_ok) {
                        this->ldh_json_pairs[iter->lv_name] = jpw.jpw_values;
                    }
                    break;
                }
                default:
                    break;
                }
            }

            retval = true;
        }

        return retval;
    };

    int get_line_bounds(size_t &line_index_out, size_t &line_end_index_out) const {
        int retval = 0;

        line_end_index_out = 0;
        do {
            const char *line_end;

            line_index_out = line_end_index_out;
            line_end = (const char *)memchr(
                this->ldh_msg.get_data() + line_index_out + 1,
                '\n',
                this->ldh_msg.length() - line_index_out - 1);
            if (line_end != NULL) {
                line_end_index_out = line_end - this->ldh_msg.get_data();
            } else {
                line_end_index_out = std::string::npos;
            }
            retval += 1;
        } while (retval <= this->ldh_y_offset);

        if (line_end_index_out == std::string::npos) {
            line_end_index_out = this->ldh_msg.length();
        }

        return retval;
    };

    int get_value_line(const logline_value &lv) const {
        return std::count(this->ldh_msg.get_data(),
                          this->ldh_msg.get_data() + lv.lv_origin.lr_start,
                          '\n');
    };

    std::string format_json_getter(const intern_string_t field, int index) {
        auto_mem<char, sqlite3_free> qname;
        auto_mem<char, sqlite3_free> jget;
        std::string retval;

        qname = sql_quote_ident(field.get());
        jget = sqlite3_mprintf("jget(%s,%Q)", qname.in(),
            this->ldh_json_pairs[field][index].wt_ptr.c_str());
        retval = std::string(jget);

        return retval;
    };

    logfile_sub_source &ldh_log_source;
    content_line_t ldh_source_line;
    logfile *ldh_file;
    int ldh_y_offset;
    logfile::iterator ldh_line;
    shared_buffer_ref ldh_msg;
    content_line_t ldh_line_index;
    std::auto_ptr<data_scanner> ldh_scanner;
    std::auto_ptr<data_parser> ldh_parser;
    std::auto_ptr<column_namer> ldh_namer;
    std::vector<logline_value> ldh_line_values;
    std::map<const intern_string_t, json_ptr_walk::walk_list_t> ldh_json_pairs;
};

#endif
