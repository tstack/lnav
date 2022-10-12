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

#ifndef vtab_impl_hh
#define vtab_impl_hh

#include <map>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "logfile_sub_source.hh"
#include "pcrepp/pcre2pp.hh"
#include "robin_hood/robin_hood.h"

class textview_curses;

enum {
    VT_COL_LINE_NUMBER,
    VT_COL_PARTITION,
    VT_COL_LOG_TIME,
    VT_COL_LOG_ACTUAL_TIME,
    VT_COL_IDLE_MSECS,
    VT_COL_LEVEL,
    VT_COL_MARK,
    VT_COL_LOG_COMMENT,
    VT_COL_LOG_TAGS,
    VT_COL_FILTERS,
    VT_COL_MAX
};

class logfile_sub_source;

struct log_cursor {
    struct opid_hash {
        unsigned int value : 6;
    };

    struct string_constraint {
        unsigned char sc_op;
        std::string sc_value;
        std::shared_ptr<lnav::pcre2pp::code> sc_pattern;

        string_constraint(unsigned char op, std::string value);

        bool matches(const std::string& sf) const;
    };

    template<typename T>
    struct integral_constraint {
        unsigned char ic_op;
        T ic_value;

        static bool op_is_supported(unsigned char op)
        {
            switch (op) {
                case SQLITE_INDEX_CONSTRAINT_EQ:
                case SQLITE_INDEX_CONSTRAINT_IS:
                case SQLITE_INDEX_CONSTRAINT_NE:
                case SQLITE_INDEX_CONSTRAINT_ISNOT:
                case SQLITE_INDEX_CONSTRAINT_GT:
                case SQLITE_INDEX_CONSTRAINT_LE:
                case SQLITE_INDEX_CONSTRAINT_LT:
                case SQLITE_INDEX_CONSTRAINT_GE:
                    return true;
                default:
                    return false;
            }
        }

        integral_constraint(unsigned char op, T value)
            : ic_op(op), ic_value(value)
        {
        }

        bool matches(const T& value) const
        {
            switch (this->ic_op) {
                case SQLITE_INDEX_CONSTRAINT_EQ:
                case SQLITE_INDEX_CONSTRAINT_IS:
                    return value == this->ic_value;
                case SQLITE_INDEX_CONSTRAINT_NE:
                case SQLITE_INDEX_CONSTRAINT_ISNOT:
                    return value != this->ic_value;
                case SQLITE_INDEX_CONSTRAINT_GT:
                    return value > this->ic_value;
                case SQLITE_INDEX_CONSTRAINT_LE:
                    return value <= this->ic_value;
                case SQLITE_INDEX_CONSTRAINT_LT:
                    return value < this->ic_value;
                case SQLITE_INDEX_CONSTRAINT_GE:
                    return value >= this->ic_value;
                default:
                    return false;
            }
        }
    };

    struct column_constraint {
        column_constraint(int32_t col, string_constraint cons)
            : cc_column(col), cc_constraint(std::move(cons))
        {
        }

        int32_t cc_column;
        string_constraint cc_constraint;
    };

    vis_line_t lc_curr_line;
    int lc_sub_index;
    vis_line_t lc_end_line;

    using level_constraint = integral_constraint<log_level_t>;

    nonstd::optional<level_constraint> lc_level_constraint;
    intern_string_t lc_format_name;
    intern_string_t lc_pattern_name;
    nonstd::optional<opid_hash> lc_opid;
    std::vector<string_constraint> lc_log_path;
    logfile* lc_last_log_path_match{nullptr};
    logfile* lc_last_log_path_mismatch{nullptr};
    std::vector<string_constraint> lc_unique_path;
    logfile* lc_last_unique_path_match{nullptr};
    logfile* lc_last_unique_path_mismatch{nullptr};

    std::vector<column_constraint> lc_indexed_columns;
    std::vector<vis_line_t> lc_indexed_lines;

    enum class constraint_t {
        none,
        unique,
    };

    void update(unsigned char op, vis_line_t vl, constraint_t cons);

    void set_eof() { this->lc_curr_line = this->lc_end_line = 0_vl; }

    bool is_eof() const
    {
        return this->lc_indexed_lines.empty()
            && this->lc_curr_line >= this->lc_end_line;
    }
};

const std::string LOG_BODY = "log_body";
const std::string LOG_TIME = "log_time";

class log_vtab_impl {
public:
    struct vtab_column {
        vtab_column(const std::string name = "",
                    int type = SQLITE3_TEXT,
                    const std::string collator = "",
                    bool hidden = false,
                    const std::string comment = "",
                    unsigned int subtype = 0)
            : vc_name(name), vc_type(type), vc_collator(collator),
              vc_hidden(hidden), vc_comment(comment), vc_subtype(subtype)
        {
        }

        vtab_column& with_comment(const std::string comment)
        {
            this->vc_comment = comment;
            return *this;
        }

        std::string vc_name;
        int vc_type;
        std::string vc_collator;
        bool vc_hidden;
        std::string vc_comment;
        int vc_subtype;
    };

    static std::pair<int, unsigned int> logline_value_to_sqlite_type(
        value_kind_t kind);

    log_vtab_impl(const intern_string_t name)
        : vi_name(name), vi_tags_name(intern_string::lookup(
                             fmt::format(FMT_STRING("{}.log_tags"), name)))
    {
        this->vi_attrs.resize(128);
    }

    virtual ~log_vtab_impl() = default;

    intern_string_t get_name() const { return this->vi_name; }

    intern_string_t get_tags_name() const { return this->vi_tags_name; }

    std::string get_table_statement();

    virtual bool is_valid(log_cursor& lc, logfile_sub_source& lss);

    virtual void filter(log_cursor& lc, logfile_sub_source& lss) {}

    virtual bool next(log_cursor& lc, logfile_sub_source& lss) = 0;

    virtual void get_columns(std::vector<vtab_column>& cols) const {}

    virtual void get_foreign_keys(std::vector<std::string>& keys_inout) const;

    virtual void get_primary_keys(std::vector<std::string>& keys_out) const {}

    virtual void extract(logfile* lf,
                         uint64_t line_number,
                         logline_value_vector& values);

    struct column_index {
        robin_hood::unordered_map<std::string, std::vector<vis_line_t>>
            ci_value_to_lines;
        int32_t ci_index_generation{0};
        vis_line_t ci_max_line{0};
    };

    std::map<int32_t, column_index> vi_column_indexes;

    bool vi_supports_indexes{true};
    int vi_column_count{0};
    string_attrs_t vi_attrs;

protected:
    const intern_string_t vi_name;
    const intern_string_t vi_tags_name;
};

class log_format_vtab_impl : public log_vtab_impl {
public:
    log_format_vtab_impl(const log_format& format)
        : log_vtab_impl(format.get_name()), lfvi_format(format)
    {
    }

    virtual bool next(log_cursor& lc, logfile_sub_source& lss);

protected:
    const log_format& lfvi_format;
};

using sql_progress_callback_t = int (*)(const log_cursor&);
using sql_progress_finished_callback_t = void (*)();

struct _log_vtab_data {
    bool lvd_looping{true};
    sql_progress_callback_t lvd_progress;
    sql_progress_finished_callback_t lvd_finished;
    source_location lvd_location;
    attr_line_t lvd_content;
};

extern thread_local _log_vtab_data log_vtab_data;

class sql_progress_guard {
public:
    sql_progress_guard(sql_progress_callback_t cb,
                       sql_progress_finished_callback_t fcb,
                       source_location loc,
                       const attr_line_t& content)
    {
        log_vtab_data.lvd_looping = true;
        log_vtab_data.lvd_progress = cb;
        log_vtab_data.lvd_finished = fcb;
        log_vtab_data.lvd_location = loc;
        log_vtab_data.lvd_content = content;
    }

    ~sql_progress_guard()
    {
        if (log_vtab_data.lvd_finished) {
            log_vtab_data.lvd_finished();
        }
        log_vtab_data.lvd_looping = true;
        log_vtab_data.lvd_progress = nullptr;
        log_vtab_data.lvd_finished = nullptr;
        log_vtab_data.lvd_location = source_location{};
        log_vtab_data.lvd_content.clear();
    }
};

class log_vtab_manager {
public:
    using iterator = std::map<intern_string_t,
                              std::shared_ptr<log_vtab_impl>>::const_iterator;

    log_vtab_manager(sqlite3* db, textview_curses& tc, logfile_sub_source& lss);
    ~log_vtab_manager();

    textview_curses* get_view() const { return &this->vm_textview; }

    logfile_sub_source* get_source() { return &this->vm_source; }

    std::string register_vtab(std::shared_ptr<log_vtab_impl> vi);
    std::string unregister_vtab(intern_string_t name);

    std::shared_ptr<log_vtab_impl> lookup_impl(intern_string_t name) const
    {
        auto iter = this->vm_impls.find(name);

        if (iter != this->vm_impls.end()) {
            return iter->second;
        }
        return nullptr;
    }

    iterator begin() const { return this->vm_impls.begin(); }

    iterator end() const { return this->vm_impls.end(); }

private:
    sqlite3* vm_db;
    textview_curses& vm_textview;
    logfile_sub_source& vm_source;
    std::map<intern_string_t, std::shared_ptr<log_vtab_impl>> vm_impls;
};

#endif
