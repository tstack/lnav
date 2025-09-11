/**
 * Copyright (c) 2025, Timothy Stack
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

#ifndef lnav_logline_window_hh
#define lnav_logline_window_hh

#include <memory>
#include <optional>
#include <string>

#include "base/attr_line.hh"
#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/result.h"
#include "bookmarks.hh"
#include "log_format.hh"
#include "vis_line.hh"

class logfile_sub_source;

class logline_window {
public:
    logline_window(logfile_sub_source& lss,
                   vis_line_t start_vl,
                   vis_line_t end_vl)
        : lw_source(lss), lw_start_line(start_vl), lw_end_line(end_vl)
    {
    }

    class iterator;

    class logmsg_info {
    public:
        logmsg_info(logfile_sub_source& lss, vis_line_t vl);

        vis_line_t get_vis_line() const { return this->li_line; }

        size_t get_line_count() const;

        uint32_t get_file_line_number() const { return this->li_line_number; }

        logfile* get_file_ptr() const { return this->li_file; }

        logline& get_logline() const { return *this->li_logline; }

        const string_attrs_t& get_attrs() const
        {
            this->load_msg();
            return this->li_string_attrs;
        }

        const logline_value_vector& get_values() const
        {
            this->load_msg();
            return this->li_line_values;
        }

        std::optional<bookmark_metadata*> get_metadata() const;

        Result<auto_buffer, std::string> get_line_hash() const;

        struct metadata_edit_guard {
            ~metadata_edit_guard();

            bookmark_metadata& operator*();

        private:
            friend logmsg_info;

            explicit metadata_edit_guard(logmsg_info& li) : meg_logmsg_info(li)
            {
            }
            logmsg_info& meg_logmsg_info;
        };

        metadata_edit_guard edit_metadata()
        {
            return metadata_edit_guard{*this};
        }

        template<typename T>
        std::optional<string_fragment> get_string_for_attr(
            const string_attr_type<T>& sat) const
        {
            this->load_msg();

            auto attr_opt = get_string_attr(this->get_attrs(), sat);
            if (!attr_opt) {
                return std::nullopt;
            }

            return this->to_string_fragment(
                attr_opt->saw_string_attr->sa_range);
        }

        string_fragment to_string_fragment(const line_range& lr) const;

    private:
        friend iterator;
        friend metadata_edit_guard;
        friend logline_window;

        void next_msg();
        void prev_msg();
        void load_msg() const;
        bool is_valid() const;

        logfile_sub_source& li_source;
        vis_line_t li_line;
        uint32_t li_line_number;
        logfile* li_file{nullptr};
        logfile::iterator li_logline;
        mutable string_attrs_t li_string_attrs;
        mutable logline_value_vector li_line_values;
    };

    class iterator {
    public:
        iterator(logfile_sub_source& lss, vis_line_t vl) : i_info(lss, vl) {}

        iterator& operator++();
        iterator& operator--();

        bool operator!=(const iterator& rhs) const
        {
            return this->i_info.get_vis_line() != rhs.i_info.get_vis_line();
        }

        bool operator==(const iterator& rhs) const
        {
            return this->i_info.get_vis_line() == rhs.i_info.get_vis_line();
        }

        const logmsg_info& operator*() const { return this->i_info; }

        const logmsg_info* operator->() const { return &this->i_info; }

    private:
        logmsg_info i_info;
    };

    iterator begin();

    iterator end();

private:
    logfile_sub_source& lw_source;
    vis_line_t lw_start_line;
    vis_line_t lw_end_line;
};

#endif
