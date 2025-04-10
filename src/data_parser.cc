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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stack>

#include "data_parser.hh"

#include "config.h"
#include "spookyhash/SpookyV2.h"

data_format data_parser::FORMAT_SEMI("semi", DT_COMMA, DT_SEMI);
data_format data_parser::FORMAT_COMMA("comma", DT_INVALID, DT_COMMA);
data_format data_parser::FORMAT_EMDASH("emdash", DT_INVALID, DT_EMDASH);
data_format data_parser::FORMAT_PLAIN("plain", DT_INVALID, DT_INVALID);

data_parser::data_parser(data_scanner* ds)
    : dp_errors("dp_errors", __FILE__, __LINE__),
      dp_pairs("dp_pairs", __FILE__, __LINE__), dp_msg_format(nullptr),
      dp_msg_format_begin(ds->get_init_offset()), dp_scanner(ds)
{
    if (TRACE_FILE != nullptr) {
        fprintf(TRACE_FILE, "input %s\n", ds->get_input().to_string().c_str());
    }
}

void
data_parser::pairup(data_parser::schema_id_t* schema,
                    data_parser::element_list_t& pairs_out,
                    data_parser::element_list_t& in_list,
                    int group_depth)
{
    element_list_t ELEMENT_LIST_T(el_stack), ELEMENT_LIST_T(free_row),
        ELEMENT_LIST_T(key_comps), ELEMENT_LIST_T(value),
        ELEMENT_LIST_T(prefix);
    SpookyHash context;

    require(in_list.el_format.df_name != nullptr);

    POINT_TRACE("pairup_start");

    FORMAT_TRACE(in_list);

    for (auto iter = in_list.begin(); iter != in_list.end(); ++iter) {
        if (iter->e_token == DNT_GROUP) {
            element_list_t ELEMENT_LIST_T(group_pairs);

            this->pairup(
                nullptr, group_pairs, *iter->e_sub_elements, group_depth + 1);
            if (!group_pairs.empty()) {
                iter->assign_elements(group_pairs);
            }
        }

        if (in_list.el_format.df_prefix_terminator != DT_INVALID) {
            if (iter->e_token == in_list.el_format.df_prefix_terminator) {
                in_list.el_format.df_prefix_terminator = DT_INVALID;
                in_list.el_format.df_separator = DT_COLON;
            } else {
                el_stack.PUSH_BACK(*iter);
            }
        } else if (iter->e_token == in_list.el_format.df_terminator) {
            this->end_of_value(
                el_stack, key_comps, value, in_list, group_depth, iter);

            key_comps.PUSH_BACK(*iter);
        } else if (iter->e_token == in_list.el_format.df_qualifier) {
            value.SPLICE(
                value.end(), key_comps, key_comps.begin(), key_comps.end());
            strip(value, element_is_space{});
            if (!value.empty()) {
                el_stack.PUSH_BACK(element(value, DNT_VALUE));
            }
            value.CLEAR();
        } else if (iter->e_token == in_list.el_format.df_separator
                   || iter->e_token == DNT_GROUP)
        {
            auto key_iter = key_comps.end();
            bool found = false, key_is_values = true, mixed_values = false;
            auto last_is_key = !key_comps.empty()
                && (key_comps.back().e_token == DT_WORD
                    || key_comps.back().e_token == DT_SYMBOL);
            element_list_t ELEMENT_LIST_T(mixed_queue),
                ELEMENT_LIST_T(mixed_tail);

            if (!key_comps.empty()) {
                do {
                    --key_iter;
                    if (key_iter->e_token == in_list.el_format.df_appender) {
                        ++key_iter;
                        value.SPLICE(value.end(),
                                     key_comps,
                                     key_comps.begin(),
                                     key_iter);
                        if (!key_comps.empty()) {
                            key_comps.POP_FRONT();
                        }
                        found = true;
                    } else if (key_iter->e_token
                               == in_list.el_format.df_terminator)
                    {
                        std::vector<element> key_copy;

                        value.SPLICE(value.end(),
                                     key_comps,
                                     key_comps.begin(),
                                     key_iter);
                        key_comps.POP_FRONT();
                        strip(key_comps, element_is_space{});
                        if (key_comps.empty()) {
                            key_iter = key_comps.end();
                        } else {
                            key_iter = key_comps.begin();
                        }
                        found = true;
                    }
                    if (!found && key_iter != key_comps.end()) {
                        switch (key_iter->e_token) {
                            case DT_WORD:
                            case DT_SYMBOL:
                                key_is_values = false;
                                break;
                            case DT_WHITE:
                                break;
                            case DT_ID:
                            case DT_ANCHOR:
                            case DT_QUOTED_STRING:
                            case DT_URL:
                            case DT_PATH:
                            case DT_MAC_ADDRESS:
                            case DT_DATE:
                            case DT_TIME:
                            case DT_DATE_TIME:
                            case DT_IPV4_ADDRESS:
                            case DT_IPV6_ADDRESS:
                            case DT_HEX_DUMP:
                            case DT_UUID:
                            case DT_CREDIT_CARD_NUMBER:
                            case DT_VERSION_NUMBER:
                            case DT_OCTAL_NUMBER:
                            case DT_PERCENTAGE:
                            case DT_NUMBER:
                            case DT_HEX_NUMBER:
                            case DT_EMAIL:
                            case DT_CONSTANT:
                            case DNT_MEASUREMENT: {
                                if (((in_list.el_format.df_terminator
                                          != DT_INVALID
                                      && !el_stack.empty())
                                     || (key_comps.size() == 1
                                         && mixed_queue.empty()))
                                    && key_iter->e_token == DT_ID)
                                {
                                    key_is_values = false;
                                } else if (in_list.el_format.df_terminator
                                               == DT_INVALID
                                           || el_stack.empty())
                                {
                                    element_list_t ELEMENT_LIST_T(mixed_key);
                                    element_list_t ELEMENT_LIST_T(mixed_value);

                                    mixed_values = true;
                                    auto value_iter = key_iter;
                                    if (last_is_key) {
                                        if (mixed_tail.empty()) {
                                            mixed_tail.SPLICE(
                                                mixed_tail.end(),
                                                key_comps,
                                                std::next(value_iter),
                                                key_comps.end());
                                        }
                                    } else {
                                        while (std::prev(key_comps.end())
                                               != value_iter)
                                        {
                                            key_comps.POP_BACK();
                                        }
                                    }
                                    key_iter = std::next(value_iter);
                                    mixed_value.SPLICE(mixed_value.end(),
                                                       key_comps,
                                                       value_iter,
                                                       key_iter);
                                    if (!el_stack.empty()
                                        && el_stack.back().e_token == DNT_KEY
                                        && key_comps.empty())
                                    {
                                        el_stack.PUSH_BACK(
                                            element(mixed_value, DNT_VALUE));
                                    } else {
                                        mixed_queue.PUSH_FRONT(
                                            element(mixed_value, DNT_VALUE));
                                        if (!key_comps.empty()) {
                                            if (key_comps.back().e_token
                                                == DT_WORD)
                                            {
                                                key_iter = std::prev(
                                                    key_comps.end());
                                                mixed_key.SPLICE(
                                                    mixed_key.end(),
                                                    key_comps,
                                                    key_iter,
                                                    key_comps.end());
                                                mixed_queue.PUSH_FRONT(element(
                                                    mixed_key, DNT_KEY));
                                            }
                                        }
                                    }
                                    while (!key_comps.empty()
                                           && !key_comps.back().is_value())
                                    {
                                        key_comps.POP_BACK();
                                    }
                                    key_iter = key_comps.end();
                                }
                                break;
                            }
                            default:
                                break;
                        }
                    }
                } while (key_iter != key_comps.begin() && !found);
            }
            if (!mixed_queue.empty()) {
                if (!el_stack.empty() && el_stack.back().e_token == DNT_KEY
                    && mixed_queue.front().e_token == DNT_KEY)
                {
                    el_stack.POP_BACK();
                }
                el_stack.SPLICE(el_stack.end(),
                                mixed_queue,
                                mixed_queue.begin(),
                                mixed_queue.end());
            }
            if (!mixed_tail.empty()) {
                key_comps.CLEAR();
                key_comps.SPLICE(key_comps.end(),
                                 mixed_tail,
                                 std::prev(mixed_tail.end()),
                                 mixed_tail.end());
            }
            if (!found && !mixed_values && !el_stack.empty()
                && !key_comps.empty())
            {
                element_list_t::iterator value_iter;

                if (el_stack.size() > 1
                    && in_list.el_format.df_appender != DT_INVALID
                    && in_list.el_format.df_terminator != DT_INVALID)
                {
                    /* If we're expecting a terminator and haven't found it */
                    /* then this is part of the value. */
                    continue;
                }

                value.SPLICE(
                    value.end(), key_comps, key_comps.begin(), key_comps.end());
                value_iter = value.end();
                std::advance(value_iter, -1);
                key_comps.SPLICE(
                    key_comps.begin(), value, value_iter, value.end());
                key_comps.resize(1);
            }

            strip(value, element_is_space{});
            value.remove_if(element_if(DT_COMMA));
            if (!value.empty()) {
                el_stack.PUSH_BACK(element(value, DNT_VALUE));
            }
            strip(key_comps, element_is_space{});
            if (!key_comps.empty()) {
                if (mixed_values) {
                    key_is_values = false;
                    while (key_comps.size() > 1) {
                        key_comps.POP_FRONT();
                    }
                }
                if (!key_comps.empty()) {
                    if (key_is_values) {
                        el_stack.PUSH_BACK(element(key_comps, DNT_VALUE));
                    } else {
                        el_stack.PUSH_BACK(element(key_comps, DNT_KEY, false));
                    }
                }
            }
            key_comps.CLEAR();
            value.CLEAR();

            if (iter->e_token == DNT_GROUP) {
                value.PUSH_BACK(*iter);
                el_stack.PUSH_BACK(element(value, DNT_VALUE));
                value.CLEAR();
            }
        } else if (iter->e_token != DT_WHITE && iter->e_token != DT_CSI
                   && iter->e_token != DT_LINE)
        {
            key_comps.PUSH_BACK(*iter);
        }

        POINT_TRACE("pairup_loop");
    }

    POINT_TRACE("pairup_eol");

    CONSUMED_TRACE(in_list);

    // Only perform the free-row logic at the top level, if we're in a group
    // assume it is a list.
    if (group_depth < 1 && el_stack.empty()) {
        free_row.SPLICE(
            free_row.begin(), key_comps, key_comps.begin(), key_comps.end());
    } else {
        this->end_of_value(
            el_stack, key_comps, value, in_list, group_depth, in_list.end());
    }

    POINT_TRACE("pairup_stack");

    context.Init(0, 0);
    while (!el_stack.empty()) {
        auto kv_iter = el_stack.begin();
        if (kv_iter->e_token == DNT_VALUE) {
            if (pairs_out.empty()) {
                free_row.PUSH_BACK(el_stack.front());
            } else {
                element_list_t ELEMENT_LIST_T(free_pair_subs);
                struct element blank;

                blank.e_capture.c_begin = blank.e_capture.c_end
                    = el_stack.front().e_capture.c_begin;
                blank.e_token = DNT_KEY;
                free_pair_subs.PUSH_BACK(blank);
                free_pair_subs.PUSH_BACK(el_stack.front());
                pairs_out.PUSH_BACK(element(free_pair_subs, DNT_PAIR));
            }
        }
        if (kv_iter->e_token != DNT_KEY) {
            el_stack.POP_FRONT();
            continue;
        }

        ++kv_iter;
        if (kv_iter == el_stack.end()) {
            el_stack.POP_FRONT();
            continue;
        }

        element_list_t ELEMENT_LIST_T(pair_subs);

        if (schema != nullptr) {
            size_t key_len;
            const char* key_val
                = this->get_element_string(el_stack.front(), key_len);
            context.Update(key_val, key_len);
        }

        while (!free_row.empty()) {
            element_list_t ELEMENT_LIST_T(free_pair_subs);
            struct element blank;

            blank.e_capture.c_begin = blank.e_capture.c_end
                = free_row.front().e_capture.c_begin;
            blank.e_token = DNT_KEY;
            free_pair_subs.PUSH_BACK(blank);
            free_pair_subs.PUSH_BACK(free_row.front());
            pairs_out.PUSH_BACK(element(free_pair_subs, DNT_PAIR));
            free_row.POP_FRONT();
        }

        bool has_value = false;

        if (kv_iter->e_token == DNT_VALUE) {
            ++kv_iter;
            has_value = true;
        }

        pair_subs.SPLICE(
            pair_subs.begin(), el_stack, el_stack.begin(), kv_iter);

        if (!has_value) {
            element_list_t ELEMENT_LIST_T(blank_value);
            struct element blank;

            blank.e_token = DT_QUOTED_STRING;
            blank.e_capture.c_begin = blank.e_capture.c_end
                = pair_subs.front().e_capture.c_end;
            if (blank.e_capture.c_begin >= 0
                && blank.e_capture.c_begin
                    < this->dp_scanner->get_input().sf_end)
            {
                switch (this->dp_scanner->to_string_fragment(blank.e_capture)
                            .front())
                {
                    case '=':
                    case ':':
                        blank.e_capture.c_begin += 1;
                        blank.e_capture.c_end += 1;
                        break;
                }
            }
            blank_value.PUSH_BACK(blank);
            pair_subs.PUSH_BACK(element(blank_value, DNT_VALUE));
        }

        pairs_out.PUSH_BACK(element(pair_subs, DNT_PAIR));
    }

    if (pairs_out.size() == 1) {
        element& pair = pairs_out.front();
        element& evalue = pair.e_sub_elements->back();

        if (evalue.e_token == DNT_VALUE && evalue.e_sub_elements != nullptr
            && evalue.e_sub_elements->size() > 1)
        {
            element_list_t::iterator next_sub;

            next_sub = pair.e_sub_elements->begin();
            ++next_sub;
            prefix.SPLICE(prefix.begin(),
                          *pair.e_sub_elements,
                          pair.e_sub_elements->begin(),
                          next_sub);
            free_row.CLEAR();
            free_row.SPLICE(free_row.begin(),
                            *evalue.e_sub_elements,
                            evalue.e_sub_elements->begin(),
                            evalue.e_sub_elements->end());
            pairs_out.CLEAR();
            context.Init(0, 0);
        }
    }

    if (group_depth >= 1 && pairs_out.empty() && !free_row.empty()) {
        pairs_out.SWAP(free_row);
    }

    if (pairs_out.empty() && !free_row.empty()) {
        while (!free_row.empty()) {
            switch (free_row.front().e_token) {
                case DNT_GROUP:
                case DNT_VALUE:
                case DT_EMAIL:
                case DT_CONSTANT:
                case DT_NUMBER:
                case DT_SYMBOL:
                case DT_ID:
                case DT_HEX_NUMBER:
                case DT_OCTAL_NUMBER:
                case DT_VERSION_NUMBER:
                case DT_QUOTED_STRING:
                case DT_IPV4_ADDRESS:
                case DT_IPV6_ADDRESS:
                case DT_MAC_ADDRESS:
                case DT_HEX_DUMP:
                case DT_XML_DECL_TAG:
                case DT_XML_OPEN_TAG:
                case DT_XML_CLOSE_TAG:
                case DT_XML_EMPTY_TAG:
                case DT_UUID:
                case DT_URL:
                case DT_ANCHOR:
                case DT_PATH:
                case DT_DATE:
                case DT_TIME:
                case DT_PERCENTAGE:
                case DNT_MEASUREMENT: {
                    element_list_t ELEMENT_LIST_T(pair_subs);
                    struct element blank;

                    blank.e_capture.c_begin = blank.e_capture.c_end
                        = free_row.front().e_capture.c_begin;
                    blank.e_token = DNT_KEY;
                    pair_subs.PUSH_BACK(blank);
                    pair_subs.PUSH_BACK(free_row.front());
                    pairs_out.PUSH_BACK(element(pair_subs, DNT_PAIR));

                    // Throw something into the hash so that the number of
                    // columns is significant.  I don't think we want to
                    // use the token ID since some columns values might vary
                    // between rows.
                    context.Update(" ", 1);
                } break;

                default: {
                    size_t key_len;
                    const char* key_val
                        = this->get_element_string(free_row.front(), key_len);

                    context.Update(key_val, key_len);
                    break;
                }
            }

            free_row.POP_FRONT();
        }
    }

    if (!prefix.empty()) {
        element_list_t ELEMENT_LIST_T(pair_subs);
        struct element blank;

        blank.e_capture.c_begin = blank.e_capture.c_end
            = prefix.front().e_capture.c_begin;
        blank.e_token = DNT_KEY;
        pair_subs.PUSH_BACK(blank);
        pair_subs.PUSH_BACK(prefix.front());
        pairs_out.PUSH_FRONT(element(pair_subs, DNT_PAIR));
    }

    if (schema != nullptr && this->dp_msg_format != nullptr) {
        for (auto& fiter : pairs_out) {
            *(this->dp_msg_format) += this->get_string_up_to_value(fiter);
            this->dp_msg_format->append("#");
        }
        if (this->dp_msg_format_begin < this->dp_scanner->get_input().sf_end) {
            auto last = this->dp_scanner->get_input();
            last.sf_begin = this->dp_msg_format_begin;

            switch (last.front()) {
                case '\'':
                case '"':
                    last.sf_begin += 1;
                    break;
            }
            *(this->dp_msg_format) += last.to_string();
        }
        context.Update(this->dp_msg_format->c_str(),
                       this->dp_msg_format->length());
    }

    if (schema != nullptr) {
        context.Final(schema->out(0), schema->out(1));
    }

    if (pairs_out.size() > 1000) {
        pairs_out.resize(1000);
    }
}

void
data_parser::discover_format()
{
    std::stack<discover_format_state> state_stack;
    this->dp_group_token.push_back(DT_INVALID);
    this->dp_group_stack.resize(1);

    state_stack.push(discover_format_state());
    while (true) {
        auto tok_res = this->dp_scanner->tokenize2();
        if (!tok_res) {
            break;
        }

        element elem;
        elem.e_token = tok_res->tr_token;
        elem.e_capture = tok_res->tr_inner_capture;

        require(elem.e_capture.c_begin >= 0);
        require(elem.e_capture.c_end >= 0);
        require(elem.e_capture.c_begin <= elem.e_capture.c_end);

        state_stack.top().update_for_element(elem);
        switch (elem.e_token) {
            case DT_LPAREN:
            case DT_LANGLE:
            case DT_LCURLY:
            case DT_LSQUARE:
                this->dp_group_token.push_back(elem.e_token);
                this->dp_group_stack.emplace_back("_anon_", __FILE__, __LINE__);
                state_stack.push(discover_format_state());
                break;

            case DT_EMPTY_CONTAINER: {
                auto& curr_group = this->dp_group_stack.back();
                auto empty_list = element_list_t("_anon_", __FILE__, __LINE__);
                discover_format_state dfs;

                dfs.finalize();

                empty_list.el_format = dfs.dfs_format;
                curr_group.PUSH_BACK(element());

                auto& empty = curr_group.back();
                empty.e_capture.c_begin = elem.e_capture.c_begin + 1;
                empty.e_capture.c_end = elem.e_capture.c_begin + 1;
                empty.e_token = DNT_GROUP;
                empty.assign_elements(empty_list);
                break;
            }

            case DT_RPAREN:
            case DT_RANGLE:
            case DT_RCURLY:
            case DT_RSQUARE:
                if (this->dp_group_token.back() == (elem.e_token - 1)) {
                    this->dp_group_token.pop_back();

                    auto riter = this->dp_group_stack.rbegin();
                    ++riter;
                    state_stack.top().finalize();
                    this->dp_group_stack.back().el_format
                        = state_stack.top().dfs_format;
                    state_stack.pop();
                    if (!this->dp_group_stack.back().empty()) {
                        (*riter).PUSH_BACK(
                            element(this->dp_group_stack.back(), DNT_GROUP));
                    } else {
                        (*riter).PUSH_BACK(element());
                        riter->back().e_capture.c_begin
                            = elem.e_capture.c_begin;
                        riter->back().e_capture.c_end = elem.e_capture.c_begin;
                        riter->back().e_token = DNT_GROUP;
                        riter->back().assign_elements(
                            this->dp_group_stack.back());
                    }
                    this->dp_group_stack.pop_back();
                } else {
                    this->dp_group_stack.back().PUSH_BACK(elem);
                }
                break;

            case DT_UNIT: {
                element_list_t measurement_list;

                measurement_list.SPLICE(
                    measurement_list.end(),
                    this->dp_group_stack.back(),
                    std::prev(this->dp_group_stack.back().end()),
                    this->dp_group_stack.back().end());
                measurement_list.PUSH_BACK(elem);
                this->dp_group_stack.back().PUSH_BACK(
                    element(measurement_list, DNT_MEASUREMENT));
                break;
            }

            default:
                this->dp_group_stack.back().PUSH_BACK(elem);
                break;
        }
    }

    while (this->dp_group_stack.size() > 1) {
        this->dp_group_token.pop_back();

        auto riter = this->dp_group_stack.rbegin();
        ++riter;
        if (!this->dp_group_stack.back().empty()) {
            state_stack.top().finalize();
            this->dp_group_stack.back().el_format
                = state_stack.top().dfs_format;
            state_stack.pop();
            (*riter).PUSH_BACK(element(this->dp_group_stack.back(), DNT_GROUP));
        }
        this->dp_group_stack.pop_back();
    }

    state_stack.top().finalize();
    this->dp_group_stack.back().el_format = state_stack.top().dfs_format;
}

void
data_parser::end_of_value(data_parser::element_list_t& el_stack,
                          data_parser::element_list_t& key_comps,
                          data_parser::element_list_t& value,
                          const data_parser::element_list_t& in_list,
                          int group_depth,
                          element_list_t::iterator iter)
{
    auto key_iter = key_comps.end();
    bool found = false, key_is_values = true, mixed_values = false;
    auto last_is_key = !key_comps.empty()
        && (key_comps.back().e_token == DT_WORD
            || key_comps.back().e_token == DT_SYMBOL);
    element_list_t ELEMENT_LIST_T(mixed_queue), ELEMENT_LIST_T(mixed_tail);

    if (!key_comps.empty()) {
        do {
            --key_iter;
            if (key_iter->e_token == in_list.el_format.df_appender) {
                ++key_iter;
                value.SPLICE(
                    value.end(), key_comps, key_comps.begin(), key_iter);
                if (!key_comps.empty()) {
                    key_comps.POP_FRONT();
                }
                found = true;
            } else if (key_iter->e_token == in_list.el_format.df_terminator) {
                value.SPLICE(
                    value.end(), key_comps, key_comps.begin(), key_iter);
                key_comps.POP_FRONT();
                strip(key_comps, element_is_space{});
                if (key_comps.empty()) {
                    key_iter = key_comps.end();
                } else {
                    key_iter = key_comps.begin();
                }
                found = true;
            }
            if (!found && key_iter != key_comps.end()) {
                switch (key_iter->e_token) {
                    case DT_WORD:
                    case DT_SYMBOL:
                        key_is_values = false;
                        break;
                    case DT_WHITE:
                        break;
                    case DT_ID:
                    case DT_QUOTED_STRING:
                    case DT_URL:
                    case DT_PATH:
                    case DT_ANCHOR:
                    case DT_MAC_ADDRESS:
                    case DT_DATE:
                    case DT_TIME:
                    case DT_DATE_TIME:
                    case DT_IPV4_ADDRESS:
                    case DT_IPV6_ADDRESS:
                    case DT_HEX_DUMP:
                    case DT_UUID:
                    case DT_CREDIT_CARD_NUMBER:
                    case DT_VERSION_NUMBER:
                    case DT_OCTAL_NUMBER:
                    case DT_PERCENTAGE:
                    case DT_NUMBER:
                    case DT_HEX_NUMBER:
                    case DT_EMAIL:
                    case DT_CONSTANT:
                    case DNT_MEASUREMENT: {
                        if (((in_list.el_format.df_terminator != DT_INVALID
                              && !el_stack.empty())
                             || (key_comps.size() == 1 && mixed_queue.empty()))
                            && key_iter->e_token == DT_ID)
                        {
                            key_is_values = false;
                        } else if (in_list.el_format.df_terminator == DT_INVALID
                                   || el_stack.empty())
                        {
                            element_list_t ELEMENT_LIST_T(mixed_key);
                            element_list_t ELEMENT_LIST_T(mixed_value);

                            mixed_values = true;
                            auto value_iter = key_iter;
                            if (last_is_key) {
                                if (mixed_tail.empty()) {
                                    mixed_tail.SPLICE(mixed_tail.end(),
                                                      key_comps,
                                                      std::next(value_iter),
                                                      key_comps.end());
                                }
                            } else {
                                while (std::prev(key_comps.end()) != value_iter)
                                {
                                    key_comps.POP_BACK();
                                }
                            }
                            key_iter = std::next(value_iter);
                            mixed_value.SPLICE(mixed_value.end(),
                                               key_comps,
                                               value_iter,
                                               key_iter);
                            if (!el_stack.empty()
                                && el_stack.back().e_token == DNT_KEY
                                && key_comps.empty())
                            {
                                el_stack.PUSH_BACK(
                                    element(mixed_value, DNT_VALUE));
                            } else {
                                mixed_queue.PUSH_FRONT(
                                    element(mixed_value, DNT_VALUE));
                                if (!key_comps.empty()) {
                                    if (key_comps.back().e_token == DT_WORD) {
                                        key_iter = std::prev(key_comps.end());
                                        mixed_key.SPLICE(mixed_key.end(),
                                                         key_comps,
                                                         key_iter,
                                                         key_comps.end());
                                        mixed_queue.PUSH_FRONT(
                                            element(mixed_key, DNT_KEY));
                                    }
                                }
                            }
                            while (!key_comps.empty()
                                   && !key_comps.back().is_value())
                            {
                                key_comps.POP_BACK();
                            }
                            key_iter = key_comps.end();
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        } while (key_iter != key_comps.begin() && !found);
    }
    if (!mixed_queue.empty()) {
        if (!el_stack.empty() && el_stack.back().e_token == DNT_KEY
            && mixed_queue.front().e_token == DNT_KEY)
        {
            el_stack.POP_BACK();
        }
        el_stack.SPLICE(el_stack.end(),
                        mixed_queue,
                        mixed_queue.begin(),
                        mixed_queue.end());
    }
    if (!mixed_tail.empty()) {
        key_comps.CLEAR();
        key_comps.SPLICE(key_comps.end(),
                         mixed_tail,
                         std::prev(mixed_tail.end()),
                         mixed_tail.end());
    }
    if (!mixed_values && !el_stack.empty() && !key_comps.empty()) {
        element_list_t::iterator value_iter;

        if (el_stack.size() > 1 && in_list.el_format.df_appender != DT_INVALID
            && in_list.el_format.df_terminator != DT_INVALID
            && iter->e_token == in_list.el_format.df_separator)
        {
            /* If we're expecting a terminator and haven't found it */
            /* then this is part of the value. */
            return;
        }

        value.SPLICE(
            value.end(), key_comps, key_comps.begin(), key_comps.end());

        if (value.size() == 2
            && (value.front().e_token == DT_WORD
                || value.front().e_token == DT_SYMBOL
                || value.front().e_token == DT_ID)
            && !el_stack.empty() && el_stack.back().e_token != DNT_KEY)
        {
            element_list_t ELEMENT_LIST_T(mixed_key);

            mixed_key.SPLICE(mixed_key.end(),
                             value,
                             value.begin(),
                             std::next(value.begin()));
            el_stack.PUSH_BACK(element(mixed_key, DNT_KEY, false));
        }
    }

    strip(value, element_is_space{});
    value.remove_if(element_if(DT_COMMA));
    if (!value.empty()) {
        el_stack.PUSH_BACK(element(value, DNT_VALUE));
    }
    strip(key_comps, element_is_space{});
    if (!key_comps.empty()) {
        if (mixed_values) {
            key_is_values = false;
            while (key_comps.size() > 1) {
                key_comps.POP_FRONT();
            }
        }
        if (!key_comps.empty()) {
            if (iter == in_list.end()
                || iter->e_token != in_list.el_format.df_separator)
            {
                key_is_values = true;
            }
            if (key_is_values) {
                el_stack.PUSH_BACK(element(key_comps, DNT_VALUE));
            } else {
                el_stack.PUSH_BACK(element(key_comps, DNT_KEY, false));
            }
        }
    }
    key_comps.CLEAR();
    value.CLEAR();
}

void
data_parser::parse()
{
    this->discover_format();

    this->pairup(
        &this->dp_schema_id, this->dp_pairs, this->dp_group_stack.front());
}

std::string
data_parser::get_element_string(const data_parser::element& elem) const
{
    return this->dp_scanner->to_string_fragment(elem.e_capture).to_string();
}

std::string
data_parser::get_string_up_to_value(const data_parser::element& elem)
{
    const element& val_elem
        = elem.e_token == DNT_PAIR ? elem.e_sub_elements->back() : elem;

    if (this->dp_msg_format_begin <= val_elem.e_capture.c_begin) {
        auto leading_and_key_cap = data_scanner::capture_t(
            this->dp_msg_format_begin, val_elem.e_capture.c_begin);
        auto leading_and_key_sf
            = this->dp_scanner->to_string_fragment(leading_and_key_cap);
        if (leading_and_key_cap.length() >= 2) {
            switch (leading_and_key_sf.back()) {
                case '\'':
                case '"':
                    leading_and_key_sf.pop_back();
                    switch (leading_and_key_sf.back()) {
                        case 'r':
                        case 'u':
                            leading_and_key_sf.pop_back();
                            break;
                    }
                    break;
            }
            switch (leading_and_key_sf.front()) {
                case '\'':
                case '"':
                    leading_and_key_sf.sf_begin += 1;
                    break;
            }
        }
        this->dp_msg_format_begin = val_elem.e_capture.c_end;
        return leading_and_key_sf.to_string();
    } else {
        this->dp_msg_format_begin = val_elem.e_capture.c_end;
    }
    return "";
}

const char*
data_parser::get_element_string(const data_parser::element& elem,
                                size_t& len_out)
{
    len_out = elem.e_capture.length();
    return this->dp_scanner->to_string_fragment(elem.e_capture).data();
}

void
data_parser::print(FILE* out, data_parser::element_list_t& el)
{
    fprintf(out,
            "             %s\n",
            this->dp_scanner->get_input().to_string().c_str());
    for (auto& iter : el) {
        iter.print(out, *this->dp_scanner);
    }
}

FILE* data_parser::TRACE_FILE;

data_format_state_t
dfs_prefix_next(data_format_state_t state, data_token_t next_token)
{
    data_format_state_t retval = state;

    switch (state) {
        case DFS_INIT:
            switch (next_token) {
                case DT_PATH:
                case DT_COLON:
                case DT_EQUALS:
                case DT_CONSTANT:
                case DT_EMAIL:
                case DT_WORD:
                case DT_SYMBOL:
                case DT_ID:
                case DT_OCTAL_NUMBER:
                case DT_HEX_NUMBER:
                case DT_NUMBER:
                case DT_WHITE:
                case DT_CSI:
                case DT_LSQUARE:
                case DT_RSQUARE:
                case DT_LANGLE:
                case DT_RANGLE:
                case DT_EMPTY_CONTAINER:
                case DT_ANCHOR:
                    break;

                default:
                    retval = DFS_ERROR;
                    break;
            }
            break;

        case DFS_EXPECTING_SEP:
        case DFS_ERROR:
            retval = DFS_ERROR;
            break;

        default:
            break;
    }

    return retval;
}

data_format_state_t
dfs_semi_next(data_format_state_t state, data_token_t next_token)
{
    data_format_state_t retval = state;

    switch (state) {
        case DFS_INIT:
            switch (next_token) {
                case DT_COMMA:
                case DT_SEMI:
                    retval = DFS_ERROR;
                    break;

                default:
                    retval = DFS_KEY;
                    break;
            }
            break;

        case DFS_KEY:
            switch (next_token) {
                case DT_COLON:
                case DT_EQUALS:
                    retval = DFS_VALUE;
                    break;

                case DT_SEMI:
                    retval = DFS_ERROR;
                    break;

                default:
                    break;
            }
            break;

        case DFS_VALUE:
            switch (next_token) {
                case DT_SEMI:
                    retval = DFS_INIT;
                    break;

                default:
                    break;
            }
            break;

        case DFS_EXPECTING_SEP:
        case DFS_ERROR:
            retval = DFS_ERROR;
            break;
    }

    return retval;
}

data_format_state_t
dfs_comma_next(data_format_state_t state, data_token_t next_token)
{
    data_format_state_t retval = state;

    switch (state) {
        case DFS_INIT:
            switch (next_token) {
                case DT_COMMA:
                    break;

                case DT_SEMI:
                    retval = DFS_ERROR;
                    break;

                default:
                    retval = DFS_KEY;
                    break;
            }
            break;

        case DFS_KEY:
            switch (next_token) {
                case DT_COLON:
                case DT_EQUALS:
                    retval = DFS_VALUE;
                    break;

                case DT_COMMA:
                    retval = DFS_INIT;
                    break;

                case DT_WORD:
                    retval = DFS_EXPECTING_SEP;
                    break;

                case DT_SEMI:
                    retval = DFS_ERROR;
                    break;

                default:
                    break;
            }
            break;

        case DFS_EXPECTING_SEP:
            switch (next_token) {
                case DT_COLON:
                case DT_EQUALS:
                case DT_LPAREN:
                case DT_LCURLY:
                case DT_LSQUARE:
                case DT_LANGLE:
                    retval = DFS_VALUE;
                    break;

                case DT_EMPTY_CONTAINER:
                    retval = DFS_INIT;
                    break;

                case DT_COMMA:
                case DT_SEMI:
                    retval = DFS_ERROR;
                    break;

                default:
                    break;
            }
            break;

        case DFS_VALUE:
            switch (next_token) {
                case DT_COMMA:
                    retval = DFS_INIT;
                    break;

                case DT_COLON:
                case DT_EQUALS:
                    retval = DFS_ERROR;
                    break;

                default:
                    break;
            }
            break;

        case DFS_ERROR:
            retval = DFS_ERROR;
            break;
    }

    return retval;
}

data_parser::element::element()
    : e_capture(-1, -1), e_token(DT_INVALID), e_sub_elements(nullptr)
{
}

data_parser::element::element(data_parser::element_list_t& subs,
                              data_token_t token,
                              bool assign_subs_elements)
    : e_capture(subs.front().e_capture.c_begin, subs.back().e_capture.c_end),
      e_token(token), e_sub_elements(nullptr)
{
    if (assign_subs_elements) {
        this->assign_elements(subs);
    }
}

data_parser::element::element(const data_parser::element& other)
{
    /* require(other.e_sub_elements == nullptr); */

    this->e_capture = other.e_capture;
    this->e_token = other.e_token;
    this->e_sub_elements = nullptr;
    if (other.e_sub_elements != nullptr) {
        this->assign_elements(*other.e_sub_elements);
    }
}

data_parser::element::~element()
{
    delete this->e_sub_elements;
    this->e_sub_elements = nullptr;
}

data_parser::element&
data_parser::element::operator=(const data_parser::element& other)
{
    this->e_capture = other.e_capture;
    this->e_token = other.e_token;
    this->e_sub_elements = nullptr;
    if (other.e_sub_elements != nullptr) {
        this->assign_elements(*other.e_sub_elements);
    }
    return *this;
}

void
data_parser::element::assign_elements(data_parser::element_list_t& subs)
{
    if (this->e_sub_elements == nullptr) {
        this->e_sub_elements = new element_list_t("_sub_", __FILE__, __LINE__);
        this->e_sub_elements->el_format = subs.el_format;
    }
    this->e_sub_elements->SWAP(subs);
    this->update_capture();
}

void
data_parser::element::update_capture()
{
    if (this->e_sub_elements != nullptr && !this->e_sub_elements->empty()) {
        this->e_capture.c_begin
            = this->e_sub_elements->front().e_capture.c_begin;
        this->e_capture.c_end = this->e_sub_elements->back().e_capture.c_end;
    }
}

const data_parser::element&
data_parser::element::get_pair_value() const
{
    require(this->e_token == DNT_PAIR);

    return this->e_sub_elements->back();
}

data_token_t
data_parser::element::value_token() const
{
    data_token_t retval = DT_INVALID;

    if (this->e_token == DNT_VALUE) {
        if (this->e_sub_elements != nullptr
            && this->e_sub_elements->size() == 1)
        {
            retval = this->e_sub_elements->front().e_token;
        } else {
            retval = DT_SYMBOL;
        }
    } else {
        retval = this->e_token;
    }
    return retval;
}

const data_parser::element&
data_parser::element::get_value_elem() const
{
    if (this->e_token == DNT_VALUE) {
        if (this->e_sub_elements != nullptr
            && this->e_sub_elements->size() == 1)
        {
            return this->e_sub_elements->front();
        }
    }
    return *this;
}

const data_parser::element&
data_parser::element::get_pair_elem() const
{
    if (this->e_token == DNT_VALUE) {
        return this->e_sub_elements->front();
    }
    return *this;
}

void
data_parser::element::print(FILE* out, data_scanner& ds, int offset) const
{
    int lpc;

    if (this->e_sub_elements != nullptr) {
        for (auto& e_sub_element : *this->e_sub_elements) {
            e_sub_element.print(out, ds, offset + 1);
        }
    }

    fprintf(out,
            "%4s %3d:%-3d ",
            data_scanner::token2name(this->e_token),
            this->e_capture.c_begin,
            this->e_capture.c_end);
    for (lpc = 0; lpc < this->e_capture.c_end; lpc++) {
        if (lpc == this->e_capture.c_begin) {
            fputc('^', out);
        } else if (lpc == (this->e_capture.c_end - 1)) {
            fputc('^', out);
        } else if (lpc > this->e_capture.c_begin) {
            fputc('-', out);
        } else {
            fputc(' ', out);
        }
    }
    for (; lpc < (int) ds.get_input().length(); lpc++) {
        fputc(' ', out);
    }

    std::string sub = ds.to_string_fragment(this->e_capture).to_string();
    fprintf(out, "  %s\n", sub.c_str());
}

bool
data_parser::element::is_value() const
{
    switch (this->e_token) {
        case DNT_MEASUREMENT:
        case DT_ID:
        case DT_QUOTED_STRING:
        case DT_URL:
        case DT_PATH:
        case DT_MAC_ADDRESS:
        case DT_DATE:
        case DT_TIME:
        case DT_DATE_TIME:
        case DT_IPV4_ADDRESS:
        case DT_IPV6_ADDRESS:
        case DT_HEX_DUMP:
        case DT_UUID:
        case DT_CREDIT_CARD_NUMBER:
        case DT_VERSION_NUMBER:
        case DT_OCTAL_NUMBER:
        case DT_PERCENTAGE:
        case DT_NUMBER:
        case DT_HEX_NUMBER:
        case DT_EMAIL:
        case DT_CONSTANT:
        case DT_ANCHOR:
            return true;
        default:
            return false;
    }
}

data_parser::discover_format_state::discover_format_state()
    : dfs_prefix_state(DFS_INIT), dfs_semi_state(DFS_INIT),
      dfs_comma_state(DFS_INIT)
{
    memset(this->dfs_hist, 0, sizeof(this->dfs_hist));
}

void
data_parser::discover_format_state::update_for_element(
    const data_parser::element& elem)
{
    this->dfs_prefix_state
        = dfs_prefix_next(this->dfs_prefix_state, elem.e_token);
    this->dfs_semi_state = dfs_semi_next(this->dfs_semi_state, elem.e_token);
    this->dfs_comma_state = dfs_comma_next(this->dfs_comma_state, elem.e_token);
    if (this->dfs_prefix_state != DFS_ERROR) {
        if (this->dfs_semi_state == DFS_ERROR) {
            this->dfs_semi_state = DFS_INIT;
        }
        if (this->dfs_comma_state == DFS_ERROR) {
            this->dfs_comma_state = DFS_INIT;
        }
    }
    this->dfs_hist[elem.e_token] += 1;
}

void
data_parser::discover_format_state::finalize()
{
    data_token_t qualifier = this->dfs_format.df_qualifier;
    data_token_t separator = this->dfs_format.df_separator;
    data_token_t prefix_term = this->dfs_format.df_prefix_terminator;

    this->dfs_format = FORMAT_PLAIN;
    if (this->dfs_hist[DT_EQUALS]) {
        qualifier = DT_COLON;
        separator = DT_EQUALS;
    }

    if (this->dfs_semi_state != DFS_ERROR && this->dfs_hist[DT_SEMI]) {
        this->dfs_format = FORMAT_SEMI;
    } else if (this->dfs_comma_state != DFS_ERROR) {
        if (this->dfs_hist[DT_COMMA] > 0) {
            this->dfs_format = FORMAT_COMMA;
        } else if (this->dfs_hist[DT_EMDASH] > 0) {
            this->dfs_format = FORMAT_EMDASH;
        }
        if (separator == DT_COLON && this->dfs_hist[DT_COMMA] > 0) {
            if (!((this->dfs_hist[DT_COLON] == this->dfs_hist[DT_COMMA])
                  || ((this->dfs_hist[DT_COLON] - 1)
                      == this->dfs_hist[DT_COMMA])))
            {
                separator = DT_INVALID;
                if (this->dfs_hist[DT_COLON] > 0) {
                    prefix_term = DT_COLON;
                }
            }
        }
    }

    this->dfs_format.df_qualifier = qualifier;
    this->dfs_format.df_separator = separator;
    this->dfs_format.df_prefix_terminator = prefix_term;
}

void
data_parser::element_list_t::push_back(const data_parser::element& elem,
                                       const char* fn,
                                       int line)
{
    ELEMENT_TRACE;

    require(elem.e_capture.c_end >= -1);
    require(this->empty()
            || (elem.e_capture.c_begin == -1 && elem.e_capture.c_end == -1)
            || this->back().e_capture.c_end <= elem.e_capture.c_begin);
    this->std::list<element>::push_back(elem);
}
