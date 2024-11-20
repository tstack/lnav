/**
 * Copyright (c) 2021, Timothy Stack
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
 * @file log_data_helper.cc
 */

#include "log_data_helper.hh"

#include "config.h"

void
log_data_helper::clear()
{
    this->ldh_file = nullptr;
    this->ldh_line_values.lvv_sbr.disown();
    this->ldh_parser.reset();
    this->ldh_scanner.reset();
    this->ldh_namer.reset();
    this->ldh_extra_json.clear();
    this->ldh_json_pairs.clear();
    this->ldh_xml_pairs.clear();
    this->ldh_line_attrs.clear();
}

bool
log_data_helper::parse_line(content_line_t line, bool allow_middle)
{
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
    if (!ll->is_message()) {
        log_warning("failed to parse line %d", line);
        this->ldh_parser.reset();
        this->ldh_scanner.reset();
        this->ldh_namer.reset();
        this->ldh_extra_json.clear();
        this->ldh_json_pairs.clear();
        this->ldh_xml_pairs.clear();
        this->ldh_line_attrs.clear();
    } else {
        auto format = this->ldh_file->get_format();
        line_range body;
        auto& sa = this->ldh_line_attrs;

        this->ldh_line_attrs.clear();
        this->ldh_line_values.clear();
        this->ldh_file->read_full_message(ll, this->ldh_line_values.lvv_sbr);
        this->ldh_line_values.lvv_sbr.erase_ansi();
        format->annotate(this->ldh_file.get(),
                         this->ldh_line_index,
                         sa,
                         this->ldh_line_values);

        body = find_string_attr_range(sa, &SA_BODY);
        if (body.lr_start == -1) {
            body.lr_start = this->ldh_line_values.lvv_sbr.length();
            body.lr_end = this->ldh_line_values.lvv_sbr.length();
        }
        this->ldh_scanner = std::make_unique<data_scanner>(
            this->ldh_line_values.lvv_sbr.to_string_fragment().sub_range(
                body.lr_start, body.lr_end));
        this->ldh_parser
            = std::make_unique<data_parser>(this->ldh_scanner.get());
        this->ldh_msg_format.clear();
        this->ldh_parser->dp_msg_format = &this->ldh_msg_format;
        if (body.length() < 128 * 1024) {
            this->ldh_parser->parse();
        }
        this->ldh_namer
            = std::make_unique<column_namer>(column_namer::language::SQL);
        this->ldh_extra_json.clear();
        this->ldh_json_pairs.clear();
        this->ldh_xml_pairs.clear();

        for (const auto& lv : this->ldh_line_values.lvv_values) {
            this->ldh_namer->cn_builtin_names.emplace_back(
                lv.lv_meta.lvm_name.get());
        }

        for (auto& ldh_line_value : this->ldh_line_values.lvv_values) {
            if (ldh_line_value.lv_meta.lvm_name == format->lf_timestamp_field) {
                continue;
            }
            if (ldh_line_value.lv_meta.lvm_column
                    .is<logline_value_meta::external_column>())
            {
                char buf[ldh_line_value.lv_meta.lvm_name.size() + 2];

                auto rc = fmt::format_to(
                    buf, FMT_STRING("/{}"), ldh_line_value.lv_meta.lvm_name);
                *rc = '\0';
                this->ldh_extra_json[intern_string::lookup(buf, -1)]
                    = ldh_line_value.to_string();
                continue;
            }

            switch (ldh_line_value.lv_meta.lvm_kind) {
                case value_kind_t::VALUE_JSON: {
                    if (!ldh_line_value.lv_meta.lvm_struct_name.empty()) {
                        continue;
                    }

                    json_ptr_walk jpw;

                    if (jpw.parse(ldh_line_value.text_value(),
                                  ldh_line_value.text_length())
                            == yajl_status_ok
                        && jpw.complete_parse() == yajl_status_ok)
                    {
                        this->ldh_json_pairs[ldh_line_value.lv_meta.lvm_name]
                            = jpw.jpw_values;
                    }
                    break;
                }
                case value_kind_t::VALUE_XML: {
                    auto col_name = ldh_line_value.lv_meta.lvm_name;
                    pugi::xml_document doc;

                    auto parse_res
                        = doc.load_buffer(ldh_line_value.text_value(),
                                          ldh_line_value.text_length());

                    if (parse_res) {
                        pugi::xpath_query query("//*");
                        auto node_set = doc.select_nodes(query);

                        for (const auto& xpath_node : node_set) {
                            auto node_path = lnav::pugixml::get_actual_path(
                                xpath_node.node());
                            for (auto& attr : xpath_node.node().attributes()) {
                                auto attr_path
                                    = fmt::format(FMT_STRING("{}/@{}"),
                                                  node_path,
                                                  attr.name());

                                this->ldh_xml_pairs[std::make_pair(col_name,
                                                                   attr_path)]
                                    = attr.value();
                            }

                            if (xpath_node.node().text().empty()) {
                                continue;
                            }

                            auto text_path = fmt::format(
                                FMT_STRING("{}/text()"), node_path);
                            this->ldh_xml_pairs[std::make_pair(col_name,
                                                               text_path)]
                                = trim(xpath_node.node().text().get());
                        }
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
}

int
log_data_helper::get_line_bounds(size_t& line_index_out,
                                 size_t& line_end_index_out) const
{
    int retval = 0;

    line_end_index_out = 0;
    do {
        line_index_out = line_end_index_out;
        const auto* line_end = (const char*) memchr(
            this->ldh_line_values.lvv_sbr.get_data() + line_index_out + 1,
            '\n',
            this->ldh_line_values.lvv_sbr.length() - line_index_out - 1);
        if (line_end != nullptr) {
            line_end_index_out
                = line_end - this->ldh_line_values.lvv_sbr.get_data();
        } else {
            line_end_index_out = std::string::npos;
        }
        retval += 1;
    } while (retval <= this->ldh_y_offset);

    if (line_end_index_out == std::string::npos) {
        line_end_index_out = this->ldh_line_values.lvv_sbr.length();
    }

    return retval;
}

std::string
log_data_helper::format_json_getter(const intern_string_t field, int index)
{
    std::string retval;

    auto qname = sql_quote_ident(field.get());
    retval
        = lnav::sql::mprintf("jget(%s,%Q)",
                             qname.in(),
                             this->ldh_json_pairs[field][index].wt_ptr.c_str());

    return retval;
}
