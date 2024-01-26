/**
 * Copyright (c) 2016, Timothy Stack
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
 */

#include "elem_to_json.hh"

#include "base/itertools.hh"
#include "config.h"
#include "yajlpp/yajlpp.hh"

static void
element_to_json(yajl_gen gen, data_parser& dp, const data_parser::element& elem)
{
    size_t value_len;
    const char* value_str = dp.get_element_string(elem, value_len);

    switch (elem.value_token()) {
        case DT_NUMBER: {
            auto leading_plus = value_str[0] == '+';

            yajl_gen_number(gen,
                            leading_plus ? value_str + 1 : value_str,
                            leading_plus ? value_len - 1 : value_len);
            break;
        }
        case DNT_GROUP: {
            elements_to_json(
                gen, dp, elem.get_value_elem().e_sub_elements, false);
            break;
        }
        case DNT_MEASUREMENT: {
            elements_to_json(
                gen, dp, elem.get_value_elem().e_sub_elements, false);
            break;
        }
        case DNT_PAIR: {
            const data_parser::element& pair_elem = elem.get_pair_elem();
            const auto key_str
                = dp.get_element_string(pair_elem.e_sub_elements->front());

            if (!key_str.empty()) {
                yajlpp_map singleton_map(gen);

                singleton_map.gen(key_str);
                element_to_json(gen, dp, pair_elem.get_pair_value());
            } else {
                element_to_json(gen, dp, pair_elem.get_pair_value());
            }
            break;
        }
        case DT_CONSTANT: {
            if (strncasecmp("true", value_str, value_len) == 0) {
                yajl_gen_bool(gen, true);
            } else if (strncasecmp("false", value_str, value_len) == 0) {
                yajl_gen_bool(gen, false);
            } else {
                yajl_gen_null(gen);
            }
            break;
        }
        default:
            yajl_gen_pstring(gen, value_str, value_len);
            break;
    }
}

static void
map_elements_to_json2(yajl_gen gen,
                      data_parser& dp,
                      data_parser::element_list_t* el)
{
    yajlpp_map root_map(gen);
    int col = 0;

    for (auto& iter : *el) {
        if (iter.e_token != DNT_PAIR) {
            log_warning("dropping non-pair element: %s",
                        dp.get_element_string(iter).c_str());
            continue;
        }

        const data_parser::element& pvalue = iter.get_pair_value();

        if (pvalue.value_token() == DT_INVALID) {
            log_debug("invalid!!");
            // continue;
        }

        std::string key_str
            = dp.get_element_string(iter.e_sub_elements->front());

        if (key_str.empty()) {
            key_str = fmt::format(FMT_STRING("col_{}"), col);
            col += 1;
        }
        root_map.gen(key_str);
        element_to_json(gen, dp, pvalue);
    }
}

static void
list_body_elements_to_json(yajl_gen gen,
                           data_parser& dp,
                           data_parser::element_list_t* el)
{
    for (auto& iter : *el) {
        element_to_json(gen, dp, iter);
    }
}

static void
list_elements_to_json(yajl_gen gen,
                      data_parser& dp,
                      data_parser::element_list_t* el)
{
    yajlpp_array root_array(gen);

    list_body_elements_to_json(gen, dp, el);
}

static void
map_elements_to_json(yajl_gen gen,
                     data_parser& dp,
                     data_parser::element_list_t* el)
{
    bool unique_names = el->size() > 1;
    std::vector<std::string> names;

    for (auto& iter : *el) {
        if (iter.e_token != DNT_PAIR) {
            unique_names = false;
            continue;
        }

        const auto& pvalue = iter.get_pair_value();

        if (pvalue.value_token() == DT_INVALID) {
            log_debug("invalid!!");
            // continue;
        }

        std::string key_str
            = dp.get_element_string(iter.e_sub_elements->front());
        if (key_str.empty()) {
            continue;
        }
        if (names | lnav::itertools::find(key_str)) {
            unique_names = false;
            break;
        }
        names.push_back(key_str);
    }

    names.clear();

    if (unique_names) {
        map_elements_to_json2(gen, dp, el);
    } else {
        list_elements_to_json(gen, dp, el);
    }
}

void
elements_to_json(yajl_gen gen,
                 data_parser& dp,
                 data_parser::element_list_t* el,
                 bool root)
{
    if (el->empty()) {
        yajl_gen_null(gen);
    } else {
        switch (el->front().e_token) {
            case DNT_PAIR: {
                if (root && el->size() == 1) {
                    const data_parser::element& pair_elem
                        = el->front().get_pair_elem();
                    std::string key_str = dp.get_element_string(
                        pair_elem.e_sub_elements->front());

                    if (key_str.empty()
                        && el->front().get_pair_value().value_token()
                            == DNT_GROUP)
                    {
                        element_to_json(gen, dp, el->front().get_pair_value());
                    } else {
                        yajlpp_map singleton_map(gen);

                        if (key_str.empty()) {
                            key_str = "col_0";
                        }
                        singleton_map.gen(key_str);
                        element_to_json(gen, dp, pair_elem.get_pair_value());
                    }
                } else {
                    map_elements_to_json(gen, dp, el);
                }
                break;
            }
            default:
                list_elements_to_json(gen, dp, el);
                break;
        }
    }
}
