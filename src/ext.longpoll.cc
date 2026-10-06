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

#include <chrono>
#include <condition_variable>
#include <list>
#include <map>

#include "ext.longpoll.hh"

#include "base/injector.hh"
#include "base/isc.hh"
#include "base/itertools.enumerate.hh"
#include "base/lnav_log.hh"
#include "base/progress.hh"
#include "config.h"
#include "lnav.hh"
#include "lnav_rs_ext.cxx.hh"
#include "safe/safe.h"
#include "service_tags.hh"

using namespace std::chrono_literals;

#ifdef HAVE_RUST_DEPS
namespace lnav_rs_ext {

struct editor_client {
    std::vector<std::string> ec_roots;
    std::chrono::steady_clock::time_point ec_last_seen;
    bool ec_polling{false};
};

/**
 * Something polling the server, like a browser page or an editor plugin.
 */
struct poller_client {
    std::string pc_name;
    std::chrono::steady_clock::time_point pc_last_seen;
    /** A browser with several pages open shares one session. */
    int pc_polls_in_progress{0};
};

/**
 * How long after its last poll a client is still counted.  A poll waits for
 * at most ten seconds and clients come right back, so a client that has not
 * polled in this long is gone.
 */
constexpr auto CLIENT_EXPIRY = std::chrono::seconds(15);

struct open_request {
    size_t or_id;
    std::string or_client;
    std::string or_path;
    uint32_t or_line;
    uint32_t or_col;
    bool or_delivered{false};
};

struct pollers {
    std::list<PollInput> p_pollers;
    lnav::ext::view_states p_latest_state;
    std::condition_variable p_condvar;

    std::map<std::string, editor_client> p_editors;
    std::map<std::string, poller_client> p_clients;
    std::list<open_request> p_open_requests;
    // Ids start from the wall clock so that a last_event_id echoed back by a
    // client of an earlier lnav process on the same port is below them.
    size_t p_next_request_id{static_cast<size_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count())};

    bool has_requests_for(const std::string& client) const
    {
        for (const auto& req : this->p_open_requests) {
            if (req.or_client == client) {
                return true;
            }
        }
        return false;
    }
};

using safe_pollers_t = safe::Safe<pollers>;

safe_pollers_t POLLERS;

PollResult
longpoll(const PollInput& pi)
{
    auto pi_retval = PollInput{
        0,
    };
    auto log_index = LogIndexState{};
    auto timeout = 10000ms;

    {
        auto& bts = lnav::progress_tracker::get_tasks();

        for (const auto& bt : **bts.readAccess()) {
            auto tp = bt();
            if (tp.tp_status == lnav::progress_status_t::working) {
                timeout = 333ms;
                break;
            }
        }
    }

    ::rust::Vec<OpenRequest> open_requests;
    {
        auto p = POLLERS.writeAccess<std::unique_lock>();
        const auto client_id = (std::string) pi.client_id;
        // An id this process never issued came from an earlier process.
        const auto last_event_id = pi.last_event_id < p->p_next_request_id
            ? pi.last_event_id
            : 0;
        const auto poller_key = (std::string) pi.poller_key;
        if (!poller_key.empty()) {
            const auto now = std::chrono::steady_clock::now();
            auto& pc = p->p_clients[poller_key];
            const auto is_new = pc.pc_polls_in_progress == 0
                && now - pc.pc_last_seen > CLIENT_EXPIRY;

            pc.pc_name = (std::string) pi.poller_name;
            pc.pc_last_seen = now;
            pc.pc_polls_in_progress += 1;
            if (is_new) {
                log_info("external access client connected: %s",
                         pc.pc_name.c_str());
                // Sent without waiting, so the poll never waits on the UI.
                isc::to<main_looper&, services::main_t>().send(
                    [name = pc.pc_name](auto& mlooper) {
                        auto um = lnav::console::user_message::info(
                                      attr_line_t(name).append(
                                          " connected to external access"))
                                      .with_help(
                                          "The number of connected clients is "
                                          "shown next to the globe in the top "
                                          "status bar")
                                      .move();
                        show_user_message(um.to_attr_line());
                    });
            }
        }
        if (!client_id.empty()) {
            auto& ec = p->p_editors[client_id];
            ec.ec_roots.clear();
            for (const auto& root : pi.editor_roots) {
                ec.ec_roots.emplace_back((std::string) root);
            }
            ec.ec_last_seen = std::chrono::steady_clock::now();
            ec.ec_polling = true;
            // The client echoes back the highest request id it received.
            p->p_open_requests.remove_if([&](const open_request& req) {
                return req.or_client == client_id
                    && req.or_id <= last_event_id;
            });
        }
        auto views_are_same = pi.view_states.log == p->p_latest_state.vs_log
            && pi.view_states.log_selection
                == p->p_latest_state.vs_log_selection
            && pi.view_states.text == p->p_latest_state.vs_text
            && pi.log_index_seq == p->p_latest_state.vs_log_index_seq;
        auto tasks_are_same = true;

        {
            auto& bts = lnav::progress_tracker::get_tasks();
            auto* task_cont = *bts.readAccess();
            tasks_are_same = pi.task_states.size() == task_cont->size();
            if (tasks_are_same) {
                for (size_t lpc = 0; lpc < pi.task_states.size(); lpc++) {
                    if (lpc >= task_cont->size()) {
                        continue;
                    }

                    auto tp = (*std::next(task_cont->begin(), lpc))();
                    if (tp.tp_version != pi.task_states[lpc]) {
                        tasks_are_same = false;
                        break;
                    }
                }
            }
        }

        if (views_are_same && tasks_are_same
            && (client_id.empty() || !p->has_requests_for(client_id)))
        {
            p->p_pollers.emplace_front(pi);
            auto iter = p->p_pollers.begin();

            p->p_condvar.wait_for(p.lock, timeout);
            p->p_pollers.erase(iter);
        }
        // The clients are forgotten if the server is stopped while a poll is
        // waiting.
        auto pc_iter = poller_key.empty() ? p->p_clients.end()
                                          : p->p_clients.find(poller_key);
        if (pc_iter != p->p_clients.end()
            && pc_iter->second.pc_polls_in_progress > 0)
        {
            pc_iter->second.pc_polls_in_progress -= 1;
            pc_iter->second.pc_last_seen = std::chrono::steady_clock::now();
        }
        pi_retval.last_event_id = last_event_id;
        pi_retval.client_id = pi.client_id;
        pi_retval.editor_roots = pi.editor_roots;
        if (!client_id.empty()) {
            auto& ec = p->p_editors[client_id];
            ec.ec_polling = false;
            ec.ec_last_seen = std::chrono::steady_clock::now();

            for (auto& req : p->p_open_requests) {
                if (req.or_client != client_id) {
                    continue;
                }
                open_requests.emplace_back(OpenRequest{
                    req.or_id,
                    ::rust::String::lossy(req.or_path),
                    req.or_line,
                    req.or_col,
                });
                pi_retval.last_event_id
                    = std::max(pi_retval.last_event_id, req.or_id);
                req.or_delivered = true;
            }
        }
        pi_retval.view_states = ViewStates{
            ::rust::String::lossy(p->p_latest_state.vs_log),
            ::rust::String::lossy(p->p_latest_state.vs_log_selection),
            ::rust::String::lossy(p->p_latest_state.vs_text),
        };

        const auto& latest = p->p_latest_state;
        pi_retval.log_index_seq = latest.vs_log_index_seq;
        log_index.seq = latest.vs_log_index_seq;
        log_index.row_count = latest.vs_log_row_count;
        if (pi.log_index_seq != latest.vs_log_index_seq) {
            const auto& changes = latest.vs_log_index_changes;
            // The entries after the caller's seq are only all there if the
            // oldest retained one immediately follows it.
            // A seq ahead of ours came from an earlier lnav process.
            log_index.reset = pi.log_index_seq == 0
                || pi.log_index_seq > latest.vs_log_index_seq || changes.empty()
                || changes.front().lic_seq > pi.log_index_seq + 1;
            if (!log_index.reset) {
                for (const auto& lic : changes) {
                    if (lic.lic_seq <= pi.log_index_seq) {
                        continue;
                    }
                    log_index.changes.emplace_back(LogIndexChange{
                        lic.lic_seq,
                        lic.lic_generation,
                        lic.lic_from_row,
                        lic.lic_row_count,
                    });
                }
            }
        }
    }

    ::rust::Vec<ExtProgress> bt_out;
    {
        auto& bts = lnav::progress_tracker::get_tasks();
        for (const auto& [index, bt] :
             lnav::itertools::enumerate(**bts.readAccess()))
        {
            auto tp = bt();
            pi_retval.task_states.emplace_back(tp.tp_version);
            if (tp.tp_version == 0) {
                continue;
            }
            if (tp.tp_status == lnav::progress_status_t::idle
                && index < pi.task_states.size()
                && pi.task_states[index] == tp.tp_version)
            {
                continue;
            }

            ::rust::Vec<ExtError> errors_out;
            for (const auto& msg : tp.tp_messages) {
                errors_out.emplace_back(ExtError{
                    msg.um_message.al_string,
                    msg.um_reason.al_string,
                    msg.um_help.al_string,
                });
            }

            auto ep = ExtProgress{
                tp.tp_id,
                tp.tp_status == lnav::progress_status_t::idle ? Status::idle
                                                              : Status::working,
                tp.tp_version,
                tp.tp_step,
                tp.tp_completed,
                tp.tp_total,
                std::move(errors_out),
            };
            bt_out.emplace_back(std::move(ep));
        }
    }

    return PollResult{
        pi_retval,
        std::move(bt_out),
        std::move(log_index),
        std::move(open_requests),
    };
}

void
notify_pollers()
{
#    ifdef HAVE_RUST_DEPS
    auto p = POLLERS.writeAccess<std::unique_lock>();
    p->p_condvar.notify_all();
#    endif
}

void
notify_completion()
{
    lnav::progress_tracker::instance().notify_completion();
}

}  // namespace lnav_rs_ext
#endif

namespace lnav::ext {

void
notify_pollers(const view_states& vs)
{
#ifdef HAVE_RUST_DEPS
    auto p = lnav_rs_ext::POLLERS.writeAccess<std::unique_lock>();

    for (const auto& poller : p->p_pollers) {
        if (poller.view_states.log != vs.vs_log
            || poller.view_states.log_selection != vs.vs_log_selection
            || poller.view_states.text != vs.vs_text
            || poller.log_index_seq != vs.vs_log_index_seq)
        {
            p->p_condvar.notify_all();
            break;
        }
    }
    p->p_latest_state = vs;
#endif
}

bool
send_to_editor_client(const std::filesystem::path& path,
                      uint32_t line,
                      uint32_t col,
                      std::chrono::milliseconds deadline)
{
#ifdef HAVE_RUST_DEPS
    // A client that is between polls comes back quickly, unless it's gone.
    static constexpr auto LIVENESS = std::chrono::seconds(5);
    static constexpr auto EXPIRY = std::chrono::seconds(10);

    auto& main_service = injector::get<main_looper&, services::main_t>();

    std::vector<std::string> candidates = {path.lexically_normal().string()};
    {
        std::error_code ec;
        auto canon = std::filesystem::weakly_canonical(path, ec);
        if (!ec && canon.string() != candidates.front()) {
            candidates.emplace_back(canon.string());
        }
    }

    auto p = lnav_rs_ext::POLLERS.writeAccess<std::unique_lock>();
    auto now = std::chrono::steady_clock::now();
    const std::string* best_client = nullptr;
    const std::string* best_path = nullptr;
    size_t best_len = 0;
    auto best_seen = std::chrono::steady_clock::time_point{};

    for (auto iter = p->p_editors.begin(); iter != p->p_editors.end();) {
        const auto& [client, ec] = *iter;
        if (!ec.ec_polling && now - ec.ec_last_seen > EXPIRY) {
            const auto gone = client;
            p->p_open_requests.remove_if(
                [&](const auto& req) { return req.or_client == gone; });
            log_info("editor client %s expired", gone.c_str());
            iter = p->p_editors.erase(iter);
            continue;
        }
        if (ec.ec_polling || now - ec.ec_last_seen <= LIVENESS) {
            for (const auto& root_str : ec.ec_roots) {
                auto root = std::string_view(root_str);
                while (root.size() > 1 && root.back() == '/') {
                    root.remove_suffix(1);
                }
                if (root.empty()) {
                    continue;
                }
                for (const auto& cand : candidates) {
                    auto inside = cand == root
                        || (cand.size() > root.size()
                            && cand.compare(0, root.size(), root) == 0
                            && (root.back() == '/'
                                || cand[root.size()] == '/'));
                    if (!inside) {
                        continue;
                    }
                    if (root.size() > best_len
                        || (root.size() == best_len
                            && ec.ec_last_seen > best_seen))
                    {
                        best_client = &client;
                        best_path = &cand;
                        best_len = root.size();
                        best_seen = ec.ec_last_seen;
                    }
                }
            }
        }
        ++iter;
    }

    if (best_client == nullptr) {
        log_info(
            "no editor client found for %s:%u:%u", path.c_str(), line, col);
        return false;
    }

    auto req_id = p->p_next_request_id++;

    auto _queue_item_id = main_service.run_after(
        deadline, [req_id, best_client = *best_client] {
            auto p = lnav_rs_ext::POLLERS.writeAccess<std::unique_lock>();
            p->p_open_requests.remove_if([&](const auto& req) {
                if (req_id != req.or_id || req.or_client != best_client) {
                    return false;
                }
                if (!req.or_delivered) {
                    log_error("editor client %s did not pick up request %zu",
                              req.or_client.c_str(),
                              req.or_id);
                }
                return true;
            });
        });

    p->p_open_requests.emplace_back(lnav_rs_ext::open_request{
        req_id,
        *best_client,
        *best_path,
        line,
        col,
    });
    log_info("sending %s:%u:%u to editor client %s (request %zu)",
             best_path->c_str(),
             line,
             col,
             best_client->c_str(),
             req_id);
    p->p_condvar.notify_all();
    return true;
#else
    return false;
#endif
}

size_t
active_client_count()
{
#ifdef HAVE_RUST_DEPS
    auto p = lnav_rs_ext::POLLERS.writeAccess<std::unique_lock>();
    const auto now = std::chrono::steady_clock::now();

    for (auto iter = p->p_clients.begin(); iter != p->p_clients.end();) {
        const auto& pc = iter->second;
        if (pc.pc_polls_in_progress == 0
            && now - pc.pc_last_seen > lnav_rs_ext::CLIENT_EXPIRY)
        {
            log_info("external access client gone: %s", pc.pc_name.c_str());
            iter = p->p_clients.erase(iter);
        } else {
            ++iter;
        }
    }

    return p->p_clients.size();
#else
    return 0;
#endif
}

void
forget_clients()
{
#ifdef HAVE_RUST_DEPS
    lnav_rs_ext::POLLERS.writeAccess<std::unique_lock>()->p_clients.clear();
#endif
}

}  // namespace lnav::ext
