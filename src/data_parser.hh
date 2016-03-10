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

#ifndef __data_parser_hh
#define __data_parser_hh

#include <stdio.h>

#include "spookyhash/SpookyV2.h"

#include <list>
#include <vector>
#include <iterator>
#include <algorithm>

#include "lnav_log.hh"
#include "lnav_util.hh"
#include "yajlpp.hh"
#include "pcrepp.hh"
#include "byte_array.hh"
#include "data_scanner.hh"

#define ELEMENT_LIST_T(var)                var("" #var, __FILE__, __LINE__)
#define PUSH_FRONT(elem)                   push_front(elem, __FILE__, __LINE__)
#define PUSH_BACK(elem)                    push_back(elem, __FILE__, __LINE__)
#define POP_FRONT(elem)                    pop_front(__FILE__, __LINE__)
#define POP_BACK(elem)                     pop_back(__FILE__, __LINE__)
#define CLEAR(elem)                        clear2(__FILE__, __LINE__)
#define SPLICE(pos, other, first, last)    splice(pos, other, first, last, \
                                                  __FILE__, __LINE__)

template<class Container, class UnaryPredicate>
void strip(Container &container, UnaryPredicate p)
{
    while (!container.empty() && p(container.front())) {
        container.POP_FRONT();
    }
    while (!container.empty() && p(container.back())) {
        container.POP_BACK();
    }
}

enum data_format_state_t {
    DFS_ERROR = -1,
    DFS_INIT,
    DFS_KEY,
    DFS_EXPECTING_SEP,
    DFS_VALUE,
};

struct data_format {
    data_format(const char *name,
                data_token_t appender = DT_INVALID,
                data_token_t terminator = DT_INVALID)
        : df_name(name),
          df_appender(appender),
          df_terminator(terminator)
    {};

    const char *       df_name;
    const data_token_t df_appender;
    const data_token_t df_terminator;
};

data_format_state_t dfs_prefix_next(data_format_state_t state,
                                    data_token_t next_token);
data_format_state_t dfs_semi_next(data_format_state_t state,
                                  data_token_t next_token);
data_format_state_t dfs_comma_next(data_format_state_t state,
                                   data_token_t next_token);

#define LIST_INIT_TRACE                 \
    do {                                \
        if (TRACE_FILE != NULL) {       \
            fprintf(TRACE_FILE,         \
                    "%p %s:%d %s %s\n", \
                    this,               \
                    fn, line,           \
                    __func__,           \
                    varname);           \
        }                               \
    } while (false)

#define LIST_DEINIT_TRACE            \
    do {                             \
        if (TRACE_FILE != NULL) {    \
            fprintf(TRACE_FILE,      \
                    "%p %s:%d %s\n", \
                    this,            \
                    fn, line,        \
                    __func__);       \
        }                            \
    } while (false)

#define ELEMENT_TRACE                                       \
    do {                                                    \
        if (TRACE_FILE != NULL) {                           \
            fprintf(TRACE_FILE,                             \
                    "%p %s:%d %s %s %d:%d\n",               \
                    this,                                   \
                    fn, line,                               \
                    __func__,                               \
                    data_scanner::token2name(elem.e_token), \
                    elem.e_capture.c_begin,                 \
                    elem.e_capture.c_end);                  \
        }                                                   \
    } while (false)

#define LIST_TRACE                   \
    do {                             \
        if (TRACE_FILE != NULL) {    \
            fprintf(TRACE_FILE,      \
                    "%p %s:%d %s\n", \
                    this,            \
                    fn, line,        \
                    __func__);       \
        }                            \
    } while (false)

#define SPLICE_TRACE                                          \
    do {                                                      \
        if (TRACE_FILE != NULL) {                             \
            fprintf(TRACE_FILE,                               \
                    "%p %s:%d %s %d %p %d:%d\n",              \
                    this,                                     \
                    fn, line,                                 \
                    __func__,                                 \
                    (int)std::distance(this->begin(), pos),   \
                    &other,                                   \
                    (int)std::distance(other.begin(), first), \
                    (int)std::distance(last, other.end()));   \
        }                                                     \
    } while (false);

#define POINT_TRACE(name)                   \
    do {                                    \
        if (TRACE_FILE) {                   \
            fprintf(TRACE_FILE,             \
                    "0x0 %s:%d point %s\n", \
                    __FILE__, __LINE__,     \
                    name);                  \
        }                                   \
    } while (false);

class data_parser {
public:
    static data_format FORMAT_SEMI;
    static data_format FORMAT_COMMA;
    static data_format FORMAT_PLAIN;

    static FILE *TRACE_FILE;

    typedef byte_array<2, uint64> schema_id_t;

    struct element;
    /* typedef std::list<element> element_list_t; */

    class element_list_t : public std::list<element> {
public:
        element_list_t(const char *varname, const char *fn, int line)
        {
            LIST_INIT_TRACE;
        }

        element_list_t()
        {
            const char *varname = "_anon2_";
            const char *fn      = __FILE__;
            int         line    = __LINE__;

            LIST_INIT_TRACE;
        };

        ~element_list_t()
        {
            const char *fn   = __FILE__;
            int         line = __LINE__;

            LIST_DEINIT_TRACE;
        };

        void push_front(const element &elem, const char *fn, int line)
        {
            ELEMENT_TRACE;

            this->std::list<element>::push_front(elem);
        };

        void push_back(const element &elem, const char *fn, int line)
        {
            ELEMENT_TRACE;

            this->std::list<element>::push_back(elem);
        };

        void pop_front(const char *fn, int line)
        {
            LIST_TRACE;

            this->std::list<element>::pop_front();
        };

        void pop_back(const char *fn, int line)
        {
            LIST_TRACE;

            this->std::list<element>::pop_back();
        };

        void clear2(const char *fn, int line)
        {
            LIST_TRACE;

            this->std::list<element>::clear();
        };

        void splice(iterator pos,
                    element_list_t &other,
                    iterator first,
                    iterator last,
                    const char *fn,
                    int line)
        {
            SPLICE_TRACE;

            this->std::list<element>::splice(pos, other, first, last);
        }
    };

    struct element {
        element() : e_token(DT_INVALID), e_sub_elements(NULL) { };
        element(element_list_t &subs,
                data_token_t token,
                bool assign_subs_elements = true)
            : e_capture(subs.front().e_capture.c_begin,
                        subs.back().e_capture.c_end),
              e_token(token),
              e_sub_elements(NULL)
        {
            if (assign_subs_elements) {
                this->assign_elements(subs);
            }
        };

        element(const element &other)
        {
            /* require(other.e_sub_elements == NULL); */

            this->e_capture      = other.e_capture;
            this->e_token        = other.e_token;
            this->e_sub_elements = NULL;
            if (other.e_sub_elements != NULL) {
                this->assign_elements(*other.e_sub_elements);
            }
        };

        ~element()
        {
            delete this->e_sub_elements;
            this->e_sub_elements = NULL;
        };

        element & operator=(const element &other)
        {
            this->e_capture      = other.e_capture;
            this->e_token        = other.e_token;
            this->e_sub_elements = NULL;
            if (other.e_sub_elements != NULL) {
                this->assign_elements(*other.e_sub_elements);
            }
            return *this;
        };

        void                    assign_elements(element_list_t &subs)
        {
            if (this->e_sub_elements == NULL) {
                this->e_sub_elements = new element_list_t("_sub_", __FILE__,
                                                          __LINE__);
            }
            this->e_sub_elements->swap(subs);
            this->update_capture();
        };

        void                    update_capture(void)
        {
            if (this->e_sub_elements != NULL) {
                this->e_capture.c_begin =
                    this->e_sub_elements->front().e_capture.c_begin;
                this->e_capture.c_end =
                    this->e_sub_elements->back().e_capture.c_end;
            }
        };

        const element &         get_pair_value(void) const
        {
            require(this->e_token == DNT_PAIR);

            return this->e_sub_elements->back();
        };

        data_token_t            value_token(void) const
        {
            data_token_t retval = DT_INVALID;

            if (this->e_token == DNT_VALUE) {
                if (this->e_sub_elements != NULL &&
                    this->e_sub_elements->size() == 1) {
                    retval = this->e_sub_elements->front().e_token;
                }
            }
            else {
                retval = this->e_token;
            }
            return retval;
        };

        void                    print(FILE *out, pcre_input &pi, int offset =
                                          0) const
        {
            int lpc;

            if (this->e_sub_elements != NULL) {
                for (element_list_t::iterator iter2 =
                         this->e_sub_elements->begin();
                     iter2 != this->e_sub_elements->end();
                     ++iter2) {
                    iter2->print(out, pi, offset + 1);
                }
            }

            fprintf(out, "%4s %3d:%-3d ",
                    data_scanner::token2name(this->e_token),
                    this->e_capture.c_begin,
                    this->e_capture.c_end);
            for (lpc = 0; lpc < this->e_capture.c_end; lpc++) {
                if (lpc == this->e_capture.c_begin) {
                    fputc('^', out);
                }
                else if (lpc == (this->e_capture.c_end - 1)) {
                    fputc('^', out);
                }
                else if (lpc > this->e_capture.c_begin) {
                    fputc('-', out);
                }
                else{
                    fputc(' ', out);
                }
            }
            for (; lpc < (int)pi.pi_length; lpc++) {
                fputc(' ', out);
            }

            std::string sub = pi.get_substr(&this->e_capture);
            fprintf(out, "  %s\n", sub.c_str());
        };

        pcre_context::capture_t e_capture;
        data_token_t            e_token;

        element_list_t *        e_sub_elements;
    };

    struct element_cmp {
        bool operator()(data_token_t token, const element &elem) const
        {
            return token == elem.e_token || token == DT_ANY;
        };

        bool operator()(const element &elem, data_token_t token) const
        {
            return (*this)(token, elem);
        };
    };

    struct element_if {
        element_if(data_token_t token) : ei_token(token) { };

        bool operator()(const element &a) const
        {
            return a.e_token == this->ei_token;
        };

private:
        data_token_t ei_token;
    };

    data_parser(data_scanner *ds)
        : dp_errors("dp_errors", __FILE__, __LINE__),
          dp_pairs("dp_pairs", __FILE__, __LINE__),
          dp_format(NULL),
          dp_qualifier(DT_INVALID),
          dp_separator(DT_INVALID),
          dp_prefix_terminator(DT_INVALID),
          dp_msg_format(NULL),
          dp_msg_format_begin(ds->get_input().pi_offset),
          dp_scanner(ds)
    {
        if (TRACE_FILE != NULL) {
            fprintf(TRACE_FILE, "input %s\n", ds->get_input().get_string());
        }
    };

    void pairup(schema_id_t *schema, element_list_t &pairs_out,
                element_list_t &in_list)
    {
        element_list_t ELEMENT_LIST_T(el_stack), ELEMENT_LIST_T(free_row),
        ELEMENT_LIST_T(key_comps), ELEMENT_LIST_T(value),
        ELEMENT_LIST_T(prefix);
        SpookyHash context;

        POINT_TRACE("pairup_start");

        for (element_list_t::iterator iter = in_list.begin();
             iter != in_list.end();
             ++iter) {
            if (iter->e_token == DNT_GROUP) {
                element_list_t ELEMENT_LIST_T(group_pairs);

                this->pairup(NULL, group_pairs, *iter->e_sub_elements);
                if (!group_pairs.empty()) {
                    iter->assign_elements(group_pairs);
                }
            }

            if (this->dp_prefix_terminator != DT_INVALID) {
                if (iter->e_token == this->dp_prefix_terminator) {
                    this->dp_prefix_terminator = DT_INVALID;
                }
                else {
                    el_stack.PUSH_BACK(*iter);
                }
            }
            else if (iter->e_token == this->dp_format->df_terminator) {
                this->end_of_value(el_stack, key_comps, value);

                key_comps.PUSH_BACK(*iter);
            }
            else if (iter->e_token == this->dp_qualifier) {
                value.SPLICE(value.end(),
                             key_comps,
                             key_comps.begin(),
                             key_comps.end());
                strip(value, element_if(DT_WHITE));
                if (!value.empty()) {
                    el_stack.PUSH_BACK(element(value, DNT_VALUE));
                }
            }
            else if (iter->e_token == this->dp_separator) {
                element_list_t::iterator key_iter = key_comps.end();
                bool found = false, key_is_values = true;

                do {
                    --key_iter;
                    if (key_iter->e_token == this->dp_format->df_appender) {
                        ++key_iter;
                        value.SPLICE(value.end(),
                                     key_comps,
                                     key_comps.begin(),
                                     key_iter);
                        key_comps.POP_FRONT();
                        found = true;
                    }
                    else if (key_iter->e_token ==
                             this->dp_format->df_terminator) {
                        std::vector<element> key_copy;

                        value.SPLICE(value.end(),
                                     key_comps,
                                     key_comps.begin(),
                                     key_iter);
                        ++key_iter;
                        key_comps.POP_FRONT();
                        strip(key_comps, element_if(DT_WHITE));
                        found = true;
                    }
                    switch (key_iter->e_token) {
                        case DT_WORD:
                        case DT_SYMBOL:
                            key_is_values = false;
                            break;
                        default:
                            break;
                    }
                } while (key_iter != key_comps.begin() && !found);
                if (!found && !el_stack.empty() && !key_comps.empty()) {
                    element_list_t::iterator value_iter;

                    if (el_stack.size() > 1 &&
                        this->dp_format->df_appender != DT_INVALID &&
                        this->dp_format->df_terminator != DT_INVALID) {
                        /* If we're expecting a terminator and haven't found it */
                        /* then this is part of the value. */
                        continue;
                    }

                    value.SPLICE(value.end(),
                                 key_comps,
                                 key_comps.begin(),
                                 key_comps.end());
                    value_iter = value.end();
                    std::advance(value_iter, -1);
                    key_comps.SPLICE(key_comps.begin(),
                                     value,
                                     value_iter,
                                     value.end());
                    key_comps.resize(1);
                }

                strip(value, element_if(DT_WHITE));
                value.remove_if(element_if(DT_COMMA));
                if (!value.empty()) {
                    el_stack.PUSH_BACK(element(value, DNT_VALUE));
                }
                strip(key_comps, element_if(DT_WHITE));
                if (!key_comps.empty()) {
                    if (key_is_values) {
                        el_stack.PUSH_BACK(element(key_comps, DNT_VALUE));
                    }
                    else {
                        el_stack.PUSH_BACK(element(key_comps, DNT_KEY, false));
                    }
                }
                key_comps.CLEAR();
                value.CLEAR();
            }
            else {
                key_comps.PUSH_BACK(*iter);
            }

            POINT_TRACE("pairup_loop");
        }

        POINT_TRACE("pairup_eol");

        if (el_stack.empty()) {
            free_row.SPLICE(free_row.begin(),
                            key_comps, key_comps.begin(), key_comps.end());
        }
        else {
            this->end_of_value(el_stack, key_comps, value);
        }

        POINT_TRACE("pairup_stack");

        context.Init(0, 0);
        while (!el_stack.empty()) {
            element_list_t::iterator kv_iter = el_stack.begin();
            if (kv_iter->e_token == DNT_VALUE) {
                if (pairs_out.empty()) {
                    free_row.PUSH_BACK(el_stack.front());
                }
                else {
                    element_list_t ELEMENT_LIST_T(free_pair_subs);
                    struct element blank;

                    blank.e_capture.c_begin = blank.e_capture.c_end =
                                                  el_stack.front().e_capture.
                                                  c_begin;
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

            if (schema != NULL) {
                size_t key_len;
                const char *key_val =
                    this->get_element_string(el_stack.front(), key_len);
                context.Update(key_val, key_len);
            }

            while (!free_row.empty()) {
                element_list_t ELEMENT_LIST_T(free_pair_subs);
                struct element blank;

                blank.e_capture.c_begin = blank.e_capture.c_end =
                                              free_row.front().e_capture.
                                              c_begin;
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

            pair_subs.SPLICE(pair_subs.begin(),
                             el_stack,
                             el_stack.begin(),
                             kv_iter);

            if (!has_value) {
                element_list_t ELEMENT_LIST_T(blank_value);
                pcre_input &pi = this->dp_scanner->get_input();
                const char *str = pi.get_string();
                struct element blank;

                blank.e_token = DT_QUOTED_STRING;
                blank.e_capture.c_begin = blank.e_capture.c_end = pair_subs.front().e_capture.c_end;
                if (blank.e_capture.c_begin < pi.pi_length) {
                    switch (str[blank.e_capture.c_begin]) {
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
            element &pair  = pairs_out.front();
            element &evalue = pair.e_sub_elements->back();

            if (evalue.e_token == DNT_VALUE &&
                evalue.e_sub_elements != NULL &&
                evalue.e_sub_elements->size() > 1) {
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

        if (pairs_out.empty() && !free_row.empty()) {
            while (!free_row.empty()) {
                switch (free_row.front().e_token) {
                case DNT_GROUP:
                case DNT_VALUE:
                case DT_EMAIL:
                case DT_CONSTANT:
                case DT_NUMBER:
                case DT_SYMBOL:
                case DT_HEX_NUMBER:
                case DT_OCTAL_NUMBER:
                case DT_VERSION_NUMBER:
                case DT_QUOTED_STRING:
                case DT_IPV4_ADDRESS:
                case DT_IPV6_ADDRESS:
                case DT_MAC_ADDRESS:
                case DT_HEX_DUMP:
                case DT_XML_OPEN_TAG:
                case DT_XML_CLOSE_TAG:
                case DT_XML_EMPTY_TAG:
                case DT_UUID:
                case DT_URL:
                case DT_PATH:
                case DT_DATE:
                case DT_TIME:
                case DT_PERCENTAGE: {
                    element_list_t ELEMENT_LIST_T(pair_subs);
                    struct element blank;

                    blank.e_capture.c_begin = blank.e_capture.c_end =
                                                  free_row.front().e_capture.
                                                  c_begin;
                    blank.e_token = DNT_KEY;
                    pair_subs.PUSH_BACK(blank);
                    pair_subs.PUSH_BACK(free_row.front());
                    pairs_out.PUSH_BACK(element(pair_subs, DNT_PAIR));

                    // Throw something into the hash so that the number of
                    // columns is significant.  I don't think we want to
                    // use the token ID since some columns values might vary
                    // between rows.
                    context.Update(" ", 1);
                }
                break;

                default: {
                    size_t key_len;
                    const char *key_val = this->get_element_string(
                        free_row.front(), key_len);

                    context.Update(key_val, key_len);
                }
                break;
                }

                free_row.POP_FRONT();
            }
        }

        if (!prefix.empty()) {
            element_list_t ELEMENT_LIST_T(pair_subs);
            struct element blank;

            blank.e_capture.c_begin = blank.e_capture.c_end =
                                          prefix.front().e_capture.c_begin;
            blank.e_token = DNT_KEY;
            pair_subs.PUSH_BACK(blank);
            pair_subs.PUSH_BACK(prefix.front());
            pairs_out.PUSH_FRONT(element(pair_subs, DNT_PAIR));
        }

        if (schema != NULL) {
            context.Final(schema->out(0), schema->out(1));
        }

        if (schema != NULL && this->dp_msg_format != NULL) {
            pcre_input &pi = this->dp_scanner->get_input();
            for (element_list_t::iterator fiter = pairs_out.begin();
                 fiter != pairs_out.end();
                 ++fiter) {
                *(this->dp_msg_format) += this->get_string_up_to_value(*fiter);
                this->dp_msg_format->append("#");
            }
            if (this->dp_msg_format_begin < pi.pi_length) {
                const char *str = pi.get_string();
                pcre_context::capture_t last(this->dp_msg_format_begin,
                                             pi.pi_length);

                switch (str[last.c_begin]) {
                    case '\'':
                    case '"':
                        last.c_begin += 1;
                        break;
                }
                *(this->dp_msg_format) += pi.get_substr(&last);
            }
        }
    };

    void discover_format(void)
    {
        pcre_context_static<30> pc;
        int            hist[DT_TERMINAL_MAX];
        struct element elem;

        this->dp_group_token.push_back(DT_INVALID);
        this->dp_group_stack.resize(1);
        this->dp_qualifier = DT_INVALID;
        this->dp_separator = DT_COLON;
        this->dp_prefix_terminator = DT_INVALID;

        data_format_state_t prefix_state = DFS_INIT;
        data_format_state_t semi_state   = DFS_INIT;
        data_format_state_t comma_state  = DFS_INIT;

        memset(hist, 0, sizeof(hist));
        while (this->dp_scanner->tokenize2(pc, elem.e_token)) {
            pcre_context::iterator pc_iter;

            pc_iter = std::find_if(pc.begin(), pc.end(), capture_if_not(-1));
            require(pc_iter != pc.end());

            elem.e_capture = *pc_iter;

            require(elem.e_capture.c_begin != -1);
            require(elem.e_capture.c_end != -1);

            prefix_state = dfs_prefix_next(prefix_state, elem.e_token);
            semi_state   = dfs_semi_next(semi_state, elem.e_token);
            comma_state  = dfs_comma_next(comma_state, elem.e_token);
            if (prefix_state != DFS_ERROR) {
                if (semi_state == DFS_ERROR) {
                    semi_state = DFS_INIT;
                }
                if (comma_state == DFS_ERROR) {
                    comma_state = DFS_INIT;
                }
            }
            hist[elem.e_token] += 1;
            switch (elem.e_token) {
            case DT_LPAREN:
            case DT_LANGLE:
            case DT_LCURLY:
            case DT_LSQUARE:
                this->dp_group_token.push_back(elem.e_token);
                this->dp_group_stack.push_back(element_list_t("_anon_",
                                                              __FILE__,
                                                              __LINE__));
                break;

            case DT_RPAREN:
            case DT_RANGLE:
            case DT_RCURLY:
            case DT_RSQUARE:
                if (this->dp_group_token.back() == (elem.e_token - 1)) {
                    this->dp_group_token.pop_back();

                    std::list<element_list_t>::reverse_iterator riter =
                        this->dp_group_stack.rbegin();
                    ++riter;
                    if (!this->dp_group_stack.back().empty()) {
                        (*riter).PUSH_BACK(element(this->dp_group_stack.back(),
                                                   DNT_GROUP));
                    }
                    this->dp_group_stack.pop_back();
                }
                else {
                    this->dp_group_stack.back().PUSH_BACK(elem);
                }
                break;

            default:
                this->dp_group_stack.back().PUSH_BACK(elem);
                break;
            }
        }

        while (this->dp_group_stack.size() > 1) {
            this->dp_group_token.pop_back();

            std::list<element_list_t>::reverse_iterator riter =
                this->dp_group_stack.rbegin();
            ++riter;
            if (!this->dp_group_stack.back().empty()) {
                (*riter).PUSH_BACK(element(this->dp_group_stack.back(),
                                           DNT_GROUP));
            }
            this->dp_group_stack.pop_back();
        }

        if (hist[DT_EQUALS]) {
            this->dp_qualifier = DT_COLON;
            this->dp_separator = DT_EQUALS;
        }

        if (semi_state != DFS_ERROR && hist[DT_SEMI]) {
            this->dp_format = &FORMAT_SEMI;
        }
        else if (comma_state != DFS_ERROR) {
            this->dp_format = &FORMAT_COMMA;
            if (this->dp_separator == DT_COLON && hist[DT_COMMA] > 0) {
                if (!((hist[DT_COLON] == hist[DT_COMMA]) ||
                      ((hist[DT_COLON] - 1) == hist[DT_COMMA]))) {
                    this->dp_separator = DT_INVALID;
                    if (hist[DT_COLON] == 1) {
                        this->dp_prefix_terminator = DT_COLON;
                    }
                }
            }
        }
        else {
            this->dp_format = &FORMAT_PLAIN;
        }
    };

    void end_of_value(element_list_t &el_stack,
                      element_list_t &key_comps,
                      element_list_t &value) {
        key_comps.remove_if(element_if(this->dp_format->df_terminator));
        key_comps.remove_if(element_if(DT_COMMA));
        value.remove_if(element_if(this->dp_format->df_terminator));
        value.remove_if(element_if(DT_COMMA));
        strip(key_comps, element_if(DT_WHITE));
        strip(value, element_if(DT_WHITE));
        if (value.empty() && key_comps.size() > 1 &&
            (key_comps.front().e_token == DT_WORD ||
             key_comps.front().e_token == DT_SYMBOL)) {
            element_list_t::iterator key_iter, key_end;
            bool found_value = false;
            int word_count = 0;
            key_iter = key_comps.begin();
            key_end = key_comps.begin();
            for (; key_iter != key_comps.end(); ++key_iter) {
                if (key_iter->e_token == DT_WORD ||
                    key_iter->e_token == DT_SYMBOL) {
                    word_count += 1;
                    if (found_value) {
                        key_end = key_comps.begin();
                    }
                }
                else if (key_iter->e_token == DT_WHITE) {

                }
                else {
                    if (!found_value) {
                        key_end = key_iter;
                    }
                    found_value = true;
                }
            }
            if (word_count != 1) {
                key_end = key_comps.begin();
            }
            value.SPLICE(value.end(),
                         key_comps,
                         key_end,
                         key_comps.end());
            strip(key_comps, element_if(DT_WHITE));
            if (!key_comps.empty()) {
                el_stack.PUSH_BACK(element(key_comps, DNT_KEY, false));
            }
            key_comps.CLEAR();
        }
        else {
            value.SPLICE(value.end(),
                         key_comps,
                         key_comps.begin(),
                         key_comps.end());
        }
        strip(value, element_if(DT_WHITE));
        strip(value, element_if(DT_COLON));
        strip(value, element_if(DT_WHITE));
        if (!value.empty()) {
            el_stack.PUSH_BACK(element(value, DNT_VALUE));
        }
        value.CLEAR();
    };

    void parse(void)
    {
        this->discover_format();

        this->pairup(&this->dp_schema_id,
                     this->dp_pairs,
                     this->dp_group_stack.front());
    };

    std::string get_element_string(const element &elem) const
    {
        pcre_input &pi = this->dp_scanner->get_input();

        return pi.get_substr(&elem.e_capture);
    };

    std::string get_string_up_to_value(const element &elem) {
        pcre_input &pi = this->dp_scanner->get_input();
        const element &val_elem = elem.e_token == DNT_PAIR ?
                elem.e_sub_elements->back() : elem;

        if (this->dp_msg_format_begin <= val_elem.e_capture.c_begin) {
            pcre_context::capture_t leading_and_key = pcre_context::capture_t(
                    this->dp_msg_format_begin, val_elem.e_capture.c_begin);
            const char *str = pi.get_string();
            if (leading_and_key.length() >= 2) {
                switch (str[leading_and_key.c_end - 1]) {
                    case '\'':
                    case '"':
                        leading_and_key.c_end -= 1;
                        switch (str[leading_and_key.c_end - 1]) {
                            case 'r':
                            case 'u':
                                leading_and_key.c_end -= 1;
                                break;
                        }
                        break;
                }
                switch (str[leading_and_key.c_begin]) {
                    case '\'':
                    case '"':
                        leading_and_key.c_begin += 1;
                        break;
                }
            }
            this->dp_msg_format_begin = val_elem.e_capture.c_end;
            return pi.get_substr(&leading_and_key);
        }
        else {
            this->dp_msg_format_begin = val_elem.e_capture.c_end;
        }
        return "";
    };

    const char *get_element_string(const element &elem, size_t &len_out) {
        pcre_input &pi = this->dp_scanner->get_input();

        len_out = elem.e_capture.length();
        return pi.get_substr_start(&elem.e_capture);
    };

    void print(FILE *out, element_list_t &el)
    {
        fprintf(out, "             %s\n",
                this->dp_scanner->get_input().get_string());
        for (element_list_t::iterator iter = el.begin();
             iter != el.end();
             ++iter) {
            iter->print(out, this->dp_scanner->get_input());
        }
    };

    std::vector<data_token_t> dp_group_token;
    std::list<element_list_t> dp_group_stack;

    element_list_t dp_errors;

    element_list_t dp_pairs;
    schema_id_t    dp_schema_id;
    data_format *  dp_format;
    data_token_t   dp_qualifier;
    data_token_t   dp_separator;
    data_token_t   dp_prefix_terminator;
    std::string    *dp_msg_format;
    int dp_msg_format_begin;

private:
    data_scanner *dp_scanner;
};
#endif
