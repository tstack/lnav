/**
 * Copyright (c) 2022, Timothy Stack
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

#ifndef lnav_events_hh
#define lnav_events_hh

#include <sqlite3.h>

#include "yajlpp/yajlpp_def.hh"

namespace lnav::events {

namespace file {

struct open {
    std::string o_filename;
    std::string o_schema{SCHEMA_ID};

    static const std::string SCHEMA_ID;
    static const typed_json_path_container<open> handlers;
};

struct format_detected {
    std::string fd_filename;
    std::string fd_format;
    std::string fd_schema{SCHEMA_ID};

    static const std::string SCHEMA_ID;
    static const typed_json_path_container<format_detected> handlers;
};

}  // namespace file

namespace log {

struct msg_detected {
    std::string md_watch_name;
    std::string md_filename;
    std::string md_format;
    uint32_t md_line_number;
    std::string md_timestamp;
    std::map<std::string, json_any_t> md_values;
    std::string md_schema{SCHEMA_ID};

    static const std::string SCHEMA_ID;
    static const typed_json_path_container<msg_detected> handlers;
};

}  // namespace log

namespace session {

struct loaded {
    std::string l_schema{SCHEMA_ID};

    static const std::string SCHEMA_ID;
    static const typed_json_path_container<loaded> handlers;
};

}  // namespace session

int register_events_tab(sqlite3* db);

namespace details {
void publish(sqlite3* db, const std::string& content);
}  // namespace details

template<typename T>
void
publish(sqlite3* db, T event)
{
    auto serialized = T::handlers.to_string(event);

    details::publish(db, serialized);
}

template<typename T, typename F>
void
publish(sqlite3* db, const T& container, F func)
{
    auto_mem<char> errmsg(sqlite3_free);

    if (sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("unable to start event transaction: %s", errmsg.in());
    }
    for (const auto& elem : container) {
        publish(db, func(elem));
    }
    if (sqlite3_exec(db, "COMMIT TRANSACTION", nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("unable to commit event transaction: %s", errmsg.in());
    }
}

}  // namespace lnav::events

#endif
