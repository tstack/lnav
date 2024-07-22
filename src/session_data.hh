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
 * @file session_data.hh
 */

#ifndef lnav_session_data_hh
#define lnav_session_data_hh

#include <map>
#include <set>
#include <string>
#include <vector>

#include "mapbox/variant.hpp"
#include "view_helpers.hh"

struct file_state {
    bool fs_is_visible{true};
};

struct view_state {
    int64_t vs_top{0};
    std::optional<int64_t> vs_selection;
    std::string vs_search;
    bool vs_word_wrap{false};
    bool vs_filtering{true};
    std::vector<std::string> vs_commands;
};

struct session_data_t {
    uint64_t sd_save_time{0};
    bool sd_time_offset{false};
    std::map<std::string, file_state> sd_file_states;
    view_state sd_view_states[LNV__MAX];
};

struct recent_refs_t {
    std::set<std::string> rr_netlocs;
};

extern struct session_data_t session_data;
extern struct recent_refs_t recent_refs;

void init_session();
void load_session();
void load_time_bookmarks();
void save_session();
void reset_session();

namespace lnav {
namespace session {

void restore_view_states();

namespace regex101 {

struct entry {
    std::string re_format_name;
    std::string re_regex_name;
    std::string re_permalink;
    std::string re_delete_code;
};

void insert_entry(const entry& ei);

struct no_entry {};

struct error {
    std::string e_msg;
};

using get_result_t = mapbox::util::variant<entry, no_entry, error>;

get_result_t get_entry(const std::string& format_name,
                       const std::string& regex_name);
void delete_entry(const std::string& format_name,
                  const std::string& regex_name);
Result<std::vector<entry>, std::string> get_entries();

}  // namespace regex101
}  // namespace session
}  // namespace lnav

#endif
