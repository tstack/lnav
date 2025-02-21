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

#include "lnav.events.hh"

#include "sqlitepp.client.hh"

namespace lnav::events {

namespace file {

const std::string open::SCHEMA_ID
    = "https://lnav.org/event-file-open-v1.schema.json";

const typed_json_path_container<open> open::handlers = typed_json_path_container<open>{
    yajlpp::property_handler("$schema").for_field(&open::o_schema)
        .with_example(open::SCHEMA_ID),
    yajlpp::property_handler("filename")
        .with_description("The path of the file that was opened")
        .for_field(&open::o_filename),
}
    .with_schema_id2(open::SCHEMA_ID)
    .with_description2("Event fired when a file is opened.");

const std::string format_detected::SCHEMA_ID
    = "https://lnav.org/event-file-format-detected-v1.schema.json";

const typed_json_path_container<format_detected> format_detected::handlers = typed_json_path_container<format_detected>{
    yajlpp::property_handler("$schema").for_field(&format_detected::fd_schema)
        .with_example(format_detected::SCHEMA_ID),
    yajlpp::property_handler("filename")
        .with_description("The path of the file for which a matching format was found")
        .for_field(&format_detected::fd_filename),
    yajlpp::property_handler("format")
        .with_description("The name of the format")
        .for_field(&format_detected::fd_format),
}
    .with_schema_id2(format_detected::SCHEMA_ID)
    .with_description2("Event fired when a log format is detected for a file.");

}  // namespace file

namespace log {

const std::string msg_detected::SCHEMA_ID
    = "https://lnav.org/event-log-msg-detected-v1.schema.json";

static const json_path_container msg_values_handlers = {
    yajlpp::pattern_property_handler("(?<name>[\\w\\-]+)")
        .with_synopsis("<name>")
        .for_field(&msg_detected::md_values),
};

const typed_json_path_container<msg_detected> msg_detected::handlers = typed_json_path_container<msg_detected>{
    yajlpp::property_handler("$schema").for_field(&msg_detected::md_schema)
        .with_example(msg_detected::SCHEMA_ID),
    yajlpp::property_handler("watch-name")
        .with_description("The name of the watch expression that matched this log message")
        .for_field(&msg_detected::md_watch_name),
    yajlpp::property_handler("filename")
        .with_description("The path of the file containing the log message")
        .for_field(&msg_detected::md_filename),
    yajlpp::property_handler("line-number")
        .with_description("The line number in the file, starting from zero")
        .for_field(&msg_detected::md_line_number),
    yajlpp::property_handler("format")
        .with_description("The name of the log format that matched this log message")
        .for_field(&msg_detected::md_format),
    yajlpp::property_handler("timestamp")
        .with_description("The timestamp of the log message")
        .for_field(&msg_detected::md_timestamp),
    yajlpp::property_handler("values")
        .with_description("The log message values captured by the log format")
        .with_children(msg_values_handlers),
}
    .with_schema_id2(msg_detected::SCHEMA_ID)
    .with_description2("Event fired when a log message is detected by a watch expression.");

}  // namespace log

namespace session {

const std::string loaded::SCHEMA_ID
    = "https://lnav.org/event-session-loaded-v1.schema.json";

const typed_json_path_container<loaded> loaded::handlers = typed_json_path_container<loaded>{
    yajlpp::property_handler("$schema").for_field(&loaded::l_schema)
        .with_example(loaded::SCHEMA_ID),
}
    .with_schema_id2(loaded::SCHEMA_ID)
    .with_description2("Event fired when a session is loaded.");

}  // namespace session

int
register_events_tab(sqlite3* db)
{
    static constexpr auto* CREATE_EVENTS_TAB_SQL = R"(
CREATE TABLE lnav_events (
   ts TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%f', 'now')),
   content TEXT
)
)";
    static constexpr auto* DELETE_EVENTS_TRIGGER_SQL = R"(
CREATE TRIGGER lnav_events_cleaner AFTER INSERT ON lnav_events
BEGIN
  DELETE FROM lnav_events WHERE rowid <= NEW.rowid - 1000;
END
)";

    auto_mem<char> errmsg(sqlite3_free);

    if (sqlite3_exec(db, CREATE_EVENTS_TAB_SQL, nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("Unable to create events table: %s", errmsg.in());
    }
    if (sqlite3_exec(
            db, DELETE_EVENTS_TRIGGER_SQL, nullptr, nullptr, errmsg.out())
        != SQLITE_OK)
    {
        log_error("Unable to create event cleaner trigger: %s", errmsg.in());
    }

    return 0;
}

void
details::publish(sqlite3* db, const std::string& content)
{
    static constexpr auto* INSERT_SQL = R"(
INSERT INTO lnav_events (content) VALUES (?)
)";

    auto prep_res = prepare_stmt(db, INSERT_SQL, content);
    if (prep_res.isErr()) {
        log_error("unable to prepare event statement: %s",
                  prep_res.unwrapErr().c_str());
        return;
    }

    auto exec_res = prep_res.unwrap().execute();
    if (exec_res.isErr()) {
        log_error("failed to execute insert: %s", exec_res.unwrapErr().c_str());
        return;
    }
}

}  // namespace lnav::events