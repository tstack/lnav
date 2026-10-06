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

#ifndef lnav_ext_longpoll_hh
#define lnav_ext_longpoll_hh

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lnav::ext {

struct log_index_change {
    uint64_t lic_seq;
    uint32_t lic_generation;
    uint64_t lic_from_row;
    uint64_t lic_row_count;
};

struct view_states {
    std::string vs_log;
    std::string vs_log_selection;
    std::string vs_text;
    /** logfile_sub_source::lss_index_change_seq */
    uint64_t vs_log_index_seq{0};
    uint64_t vs_log_row_count{0};
    /** A copy of logfile_sub_source::lss_index_changes */
    std::vector<log_index_change> vs_log_index_changes;
};

void notify_pollers();

void notify_pollers(const view_states& vs);

/**
 * Queue the given file to be opened by an editor client, like an IDE plugin
 * polling through the external-access server.  The client whose editor
 * roots most closely contain the path is used.  This does not wait for
 * the client.  If the client does not pick up the request before the
 * deadline, the request is dropped.
 *
 * @return true if the request was queued for a client.
 */
bool send_to_editor_client(const std::filesystem::path& path,
                           uint32_t line,
                           uint32_t col,
                           std::chrono::milliseconds deadline);

/**
 * @return The number of clients, like browser pages and editor plugins,
 *   that are polling the external-access server.  A client is counted while
 *   it has a poll in progress and for a while after its last one, since it
 *   comes right back unless it is gone.
 */
size_t active_client_count();

/**
 * Forget the clients that were polling, for when the server is stopped.
 */
void forget_clients();

}  // namespace lnav::ext

#endif
