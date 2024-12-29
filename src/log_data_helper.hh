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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file log_data_helper.hh
 */

#ifndef log_data_helper_hh
#define log_data_helper_hh

#include <map>
#include <memory>
#include <string>

#include "column_namer.hh"
#include "data_parser.hh"
#include "logfile_sub_source.hh"
#include "yajlpp/json_ptr.hh"

class log_data_helper {
public:
    explicit log_data_helper(logfile_sub_source& lss) : ldh_log_source(lss) {}

    void clear();

    bool parse_line(vis_line_t line, bool allow_middle = false)
    {
        return this->parse_line(this->ldh_log_source.at(line), allow_middle);
    }

    bool parse_line(content_line_t line, bool allow_middle = false);

    int get_line_bounds(size_t& line_index_out,
                        size_t& line_end_index_out) const;

    int get_value_line(const logline_value& lv) const
    {
        return std::count(
            this->ldh_line_values.lvv_sbr.get_data(),
            this->ldh_line_values.lvv_sbr.get_data() + lv.lv_origin.lr_start,
            '\n');
    }

    std::string format_json_getter(const intern_string_t field, int index);

    logfile_sub_source& ldh_log_source;
    content_line_t ldh_source_line;
    std::shared_ptr<logfile> ldh_file;
    int ldh_y_offset{0};
    logfile::iterator ldh_line;
    content_line_t ldh_line_index;
    std::unique_ptr<data_scanner> ldh_scanner;
    std::unique_ptr<data_parser> ldh_parser;
    std::unique_ptr<column_namer> ldh_namer;
    string_attrs_t ldh_line_attrs;
    logline_value_vector ldh_line_values;
    std::map<const intern_string_t, std::string> ldh_extra_json;
    std::map<const intern_string_t, json_ptr_walk::walk_list_t> ldh_json_pairs;
    std::map<std::pair<const intern_string_t, std::string>, std::string>
        ldh_xml_pairs;
    std::string ldh_msg_format;
};

#endif
