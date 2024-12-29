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

#ifndef data_parser_hh
#define data_parser_hh

#include <iterator>
#include <list>
#include <vector>

#include <stdio.h>

#include "base/lnav_log.hh"
#include "byte_array.hh"
#include "data_scanner.hh"

#define ELEMENT_LIST_T(var) var("" #var, __FILE__, __LINE__, group_depth)
#define PUSH_FRONT(elem)    push_front(elem, __FILE__, __LINE__)
#define PUSH_BACK(elem)     push_back(elem, __FILE__, __LINE__)
#define POP_FRONT(elem)     pop_front(__FILE__, __LINE__)
#define POP_BACK(elem)      pop_back(__FILE__, __LINE__)
#define CLEAR(elem)         clear2(__FILE__, __LINE__)
#define SWAP(other)         swap(other, __FILE__, __LINE__)
#define SPLICE(pos, other, first, last) \
    splice(pos, other, first, last, __FILE__, __LINE__)

template<class Container, class UnaryPredicate>
void
strip(Container& container, UnaryPredicate p)
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
    data_format(const char* name = nullptr,
                data_token_t appender = DT_INVALID,
                data_token_t terminator = DT_INVALID) noexcept
        : df_name(name), df_appender(appender), df_terminator(terminator),
          df_qualifier(DT_INVALID), df_separator(DT_COLON),
          df_prefix_terminator(DT_INVALID)
    {
    }

    const char* df_name;
    data_token_t df_appender;
    data_token_t df_terminator;
    data_token_t df_qualifier;
    data_token_t df_separator;
    data_token_t df_prefix_terminator;
};

data_format_state_t dfs_prefix_next(data_format_state_t state,
                                    data_token_t next_token);
data_format_state_t dfs_semi_next(data_format_state_t state,
                                  data_token_t next_token);
data_format_state_t dfs_comma_next(data_format_state_t state,
                                   data_token_t next_token);

#define LIST_INIT_TRACE \
    do { \
        if (TRACE_FILE != NULL) { \
            fprintf(TRACE_FILE, \
                    "%p %s:%d %s %s %d\n", \
                    this, \
                    fn, \
                    line, \
                    __func__, \
                    varname, \
                    group_depth); \
        } \
    } while (false)

#define LIST_DEINIT_TRACE \
    do { \
        if (TRACE_FILE != NULL) { \
            fprintf(TRACE_FILE, "%p %s:%d %s\n", this, fn, line, __func__); \
        } \
    } while (false)

#define ELEMENT_TRACE \
    do { \
        if (TRACE_FILE != NULL) { \
            fprintf(TRACE_FILE, \
                    "%p %s:%d %s %s %d:%d\n", \
                    this, \
                    fn, \
                    line, \
                    __func__, \
                    data_scanner::token2name(elem.e_token), \
                    elem.e_capture.c_begin, \
                    elem.e_capture.c_end); \
        } \
    } while (false)

#define LIST_TRACE \
    do { \
        if (TRACE_FILE != NULL) { \
            fprintf(TRACE_FILE, "%p %s:%d %s\n", this, fn, line, __func__); \
        } \
    } while (false)

#define SPLICE_TRACE \
    do { \
        if (TRACE_FILE != NULL) { \
            fprintf(TRACE_FILE, \
                    "%p %s:%d %s %d %p %d:%d\n", \
                    this, \
                    fn, \
                    line, \
                    __func__, \
                    (int) std::distance(this->begin(), pos), \
                    &other, \
                    (int) std::distance(other.begin(), first), \
                    (int) std::distance(last, other.end())); \
        } \
    } while (false);

#define SWAP_TRACE(other) \
    do { \
        if (TRACE_FILE != NULL) { \
            fprintf(TRACE_FILE, \
                    "%p %s:%d %s %p\n", \
                    this, \
                    fn, \
                    line, \
                    __func__, \
                    &other); \
        } \
    } while (false);

#define POINT_TRACE(name) \
    do { \
        if (TRACE_FILE) { \
            fprintf( \
                TRACE_FILE, "0x0 %s:%d point %s\n", __FILE__, __LINE__, name); \
        } \
    } while (false);

#define FORMAT_TRACE(elist) \
    do { \
        if (TRACE_FILE) { \
            const data_format& df = elist.el_format; \
            fprintf(TRACE_FILE, \
                    "%p %s:%d format %d %s %s %s %s %s\n", \
                    &elist, \
                    __FILE__, \
                    __LINE__, \
                    group_depth, \
                    data_scanner::token2name(df.df_appender), \
                    data_scanner::token2name(df.df_terminator), \
                    data_scanner::token2name(df.df_qualifier), \
                    data_scanner::token2name(df.df_separator), \
                    data_scanner::token2name(df.df_prefix_terminator)); \
        } \
    } while (false);

#define CONSUMED_TRACE(elist) \
    do { \
        if (TRACE_FILE) { \
            fprintf(TRACE_FILE, \
                    "%p %s:%d consumed\n", \
                    &elist, \
                    __FILE__, \
                    __LINE__); \
        } \
    } while (false);

class data_parser {
public:
    static data_format FORMAT_SEMI;
    static data_format FORMAT_COMMA;
    static data_format FORMAT_EMDASH;
    static data_format FORMAT_PLAIN;

    static FILE* TRACE_FILE;

    typedef byte_array<2, uint64_t> schema_id_t;

    struct element;
    /* typedef std::list<element> element_list_t; */

    class element_list_t : public std::list<element> {
    public:
        element_list_t(const char* varname,
                       const char* fn,
                       int line,
                       int group_depth = -1)
        {
            LIST_INIT_TRACE;
        }

        element_list_t()
        {
            const char* varname = "_anon2_";
            const char* fn = __FILE__;
            int line = __LINE__;
            int group_depth = -1;

            LIST_INIT_TRACE;
        }

        element_list_t(const element_list_t& other) : std::list<element>(other)
        {
            this->el_format = other.el_format;
        }

        ~element_list_t()
        {
            const char* fn = __FILE__;
            int line = __LINE__;

            LIST_DEINIT_TRACE;
        }

        void push_front(const element& elem, const char* fn, int line)
        {
            ELEMENT_TRACE;

            require(elem.e_capture.c_end >= -1);
            this->std::list<element>::push_front(elem);
        }

        void push_back(const element& elem, const char* fn, int line);

        void pop_front(const char* fn, int line)
        {
            LIST_TRACE;

            this->std::list<element>::pop_front();
        }

        void pop_back(const char* fn, int line)
        {
            LIST_TRACE;

            this->std::list<element>::pop_back();
        }

        void clear2(const char* fn, int line)
        {
            LIST_TRACE;

            this->std::list<element>::clear();
        }

        void swap(element_list_t& other, const char* fn, int line)
        {
            SWAP_TRACE(other);

            this->std::list<element>::swap(other);
        }

        void splice(iterator pos,
                    element_list_t& other,
                    iterator first,
                    iterator last,
                    const char* fn,
                    int line)
        {
            SPLICE_TRACE;

            this->std::list<element>::splice(pos, other, first, last);
        }

        data_format el_format;
    };

    struct element {
        element();

        element(element_list_t& subs,
                data_token_t token,
                bool assign_subs_elements = true);

        element(const element& other);

        ~element();

        element& operator=(const element& other);

        void assign_elements(element_list_t& subs);

        void update_capture();

        const element& get_pair_value() const;

        data_token_t value_token() const;

        const element& get_value_elem() const;

        const element& get_pair_elem() const;

        bool is_value() const;

        void print(FILE* out, data_scanner&, int offset = 0) const;

        data_scanner::capture_t e_capture;
        data_token_t e_token;

        element_list_t* e_sub_elements;
    };

    struct element_cmp {
        bool operator()(data_token_t token, const element& elem) const
        {
            return token == elem.e_token || token == DT_ANY;
        }

        bool operator()(const element& elem, data_token_t token) const
        {
            return (*this)(token, elem);
        }
    };

    struct element_if {
        element_if(data_token_t token) : ei_token(token) {}

        bool operator()(const element& a) const
        {
            return a.e_token == this->ei_token;
        }

    private:
        data_token_t ei_token;
    };

    struct element_is_space {
        bool operator()(const element& el) const
        {
            return el.e_token == DT_WHITE || el.e_token == DT_CSI;
        }
    };

    struct discover_format_state {
        discover_format_state();

        void update_for_element(const element& elem);

        void finalize();

        data_format_state_t dfs_prefix_state;
        data_format_state_t dfs_semi_state;
        data_format_state_t dfs_comma_state;
        int dfs_hist[DT_TERMINAL_MAX];

        data_format dfs_format;
    };

    data_parser(data_scanner* ds);

    void pairup(schema_id_t* schema,
                element_list_t& pairs_out,
                element_list_t& in_list,
                int group_depth = 0);

    void discover_format();

    void end_of_value(element_list_t& el_stack,
                      element_list_t& key_comps,
                      element_list_t& value,
                      const element_list_t& in_list,
                      int group_depth,
                      element_list_t::iterator iter);

    void parse();

    std::string get_element_string(const element& elem) const;

    std::string get_string_up_to_value(const element& elem);

    const char* get_element_string(const element& elem, size_t& len_out);

    void print(FILE* out, element_list_t& el);

    std::vector<data_token_t> dp_group_token;
    std::list<element_list_t> dp_group_stack;

    element_list_t dp_errors;

    element_list_t dp_pairs;
    schema_id_t dp_schema_id;
    std::string* dp_msg_format;
    int dp_msg_format_begin;

private:
    data_scanner* dp_scanner;
};

#endif
