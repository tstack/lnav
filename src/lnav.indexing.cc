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

#include "lnav.indexing.hh"

#include "bound_tags.hh"
#include "lnav.events.hh"
#include "lnav.hh"
#include "service_tags.hh"
#include "session_data.hh"
#include "sql_util.hh"

using namespace std::chrono_literals;
using namespace lnav::roles::literals;

/**
 * Observer for loading progress that updates the bottom status bar.
 */
class loading_observer : public logfile_observer {
public:
    loading_observer() : lo_last_offset(0){};

    indexing_result logfile_indexing(const std::shared_ptr<logfile>& lf,
                                     file_off_t off,
                                     file_size_t total) override
    {
        static sig_atomic_t index_counter = 0;

        if (lnav_data.ld_window == nullptr) {
            return indexing_result::CONTINUE;
        }

        /* XXX require(off <= total); */
        if (off > (off_t) total) {
            off = total;
        }

        if ((((size_t) off == total) && (this->lo_last_offset != off))
            || ui_periodic_timer::singleton().time_to_update(index_counter))
        {
            if (off == total) {
                lnav_data.ld_bottom_source.update_loading(0, 0);
            } else {
                lnav_data.ld_bottom_source.update_loading(off, total);
            }
            do_observer_update(lf);
            this->lo_last_offset = off;
        }

        if (!lnav_data.ld_looping) {
            return indexing_result::BREAK;
        }
        return indexing_result::CONTINUE;
    }

    off_t lo_last_offset;
};

void
do_observer_update(const std::shared_ptr<logfile>& lf)
{
    if (isendwin()) {
        return;
    }
    lnav_data.ld_status_refresher();
    if (lf && lnav_data.ld_mode == ln_mode_t::FILES
        && lnav_data.ld_exec_phase < lnav_exec_phase::INTERACTIVE)
    {
        auto& fc = lnav_data.ld_active_files;
        auto iter = std::find(fc.fc_files.begin(), fc.fc_files.end(), lf);

        if (iter != fc.fc_files.end()) {
            auto index = std::distance(fc.fc_files.begin(), iter);
            lnav_data.ld_files_view.set_selection(
                vis_line_t(fc.fc_other_files.size() + index));
            lnav_data.ld_files_view.reload_data();
            lnav_data.ld_files_view.do_update();
        }
    }
    if (handle_winch()) {
        layout_views();
        lnav_data.ld_view_stack.do_update();
    }
    refresh();
}

void
rebuild_hist()
{
    logfile_sub_source& lss = lnav_data.ld_log_source;
    hist_source2& hs = lnav_data.ld_hist_source2;
    int zoom = lnav_data.ld_zoom_level;

    hs.set_time_slice(ZOOM_LEVELS[zoom]);
    lss.reload_index_delegate();
}

class textfile_callback : public textfile_sub_source::scan_callback {
public:
    void closed_files(
        const std::vector<std::shared_ptr<logfile>>& files) override
    {
        for (const auto& lf : files) {
            log_info("closed text files: %s", lf->get_filename().c_str());
        }
        lnav_data.ld_active_files.close_files(files);
    }

    void promote_file(const std::shared_ptr<logfile>& lf) override
    {
        auto& ftf = lnav_data.ld_files_to_front;

        ftf.remove_if([&lf](const auto& elem) {
            return elem.first == lf->get_filename()
                || elem.first == lf->get_open_options().loo_filename;
        });
        if (lnav_data.ld_log_source.insert_file(lf)) {
            this->did_promotion = true;
            log_info("promoting text file to log file: %s (%s)",
                     lf->get_filename().c_str(),
                     lf->get_content_id().c_str());
            auto format = lf->get_format();
            if (format->lf_is_self_describing) {
                auto vt = format->get_vtab_impl();

                if (vt != nullptr) {
                    lnav_data.ld_vtab_manager->register_vtab(vt);
                }
            }

            auto iter = session_data.sd_file_states.find(lf->get_filename());
            if (iter != session_data.sd_file_states.end()) {
                log_info("  found visibility state for log file: %d",
                         iter->second.fs_is_visible);

                lnav_data.ld_log_source.find_data(lf) | [&iter](auto ld) {
                    ld->set_visibility(iter->second.fs_is_visible);
                };
            }

            lnav::events::publish(lnav_data.ld_db.in(),
                                  lnav::events::file::format_detected{
                                      lf->get_filename(),
                                      lf->get_format_name().to_string(),
                                  });
        } else {
            this->closed_files({lf});
        }
    }

    void scanned_file(const std::shared_ptr<logfile>& lf) override
    {
        const auto& ftf = lnav_data.ld_files_to_front;

        if (!ftf.empty()
            && (ftf.front().first == lf->get_filename()
                || ftf.front().first == lf->get_open_options().loo_filename))
        {
            this->front_file = lf;
            this->front_top = lnav_data.ld_files_to_front.front().second;

            lnav_data.ld_files_to_front.pop_front();
        }
    }

    void renamed_file(const std::shared_ptr<logfile>& lf) override
    {
        lnav_data.ld_active_files.regenerate_unique_file_names();
    }

    std::shared_ptr<logfile> front_file;
    file_location_t front_top;
    bool did_promotion{false};
};

rebuild_indexes_result_t
rebuild_indexes(nonstd::optional<ui_clock::time_point> deadline)
{
    logfile_sub_source& lss = lnav_data.ld_log_source;
    textview_curses& log_view = lnav_data.ld_views[LNV_LOG];
    textview_curses& text_view = lnav_data.ld_views[LNV_TEXT];
    bool scroll_downs[LNV__MAX];
    rebuild_indexes_result_t retval;

    for (auto lpc : {LNV_LOG, LNV_TEXT}) {
        auto& view = lnav_data.ld_views[lpc];

        if (view.is_selectable()) {
            auto inner_height = view.get_inner_height();

            if (inner_height > 0_vl) {
                scroll_downs[lpc]
                    = (view.get_selection() == inner_height - 1_vl)
                    && !(lnav_data.ld_flags & LNF_HEADLESS);
            } else {
                scroll_downs[lpc] = !(lnav_data.ld_flags & LNF_HEADLESS);
            }
        } else {
            scroll_downs[lpc] = (view.get_top() >= view.get_top_for_last_row())
                && !(lnav_data.ld_flags & LNF_HEADLESS);
        }
    }

    {
        auto* tss = &lnav_data.ld_text_source;
        textfile_callback cb;

        auto rescan_res = tss->rescan_files(cb, deadline);
        if (rescan_res.rr_new_data) {
            text_view.reload_data();
            retval.rir_changes += rescan_res.rr_new_data;
        }
        if (!rescan_res.rr_scan_completed) {
            retval.rir_completed = false;
        }

        if (cb.front_file != nullptr) {
            ensure_view(&text_view);

            if (tss->current_file() != cb.front_file) {
                tss->to_front(cb.front_file);
            }

            nonstd::optional<vis_line_t> new_top_opt;
            cb.front_top.match(
                [&new_top_opt](vis_line_t vl) {
                    log_info("file open request to jump to line: %d", (int) vl);
                    if (vl < 0_vl) {
                        vl += lnav_data.ld_views[LNV_TEXT].get_inner_height();
                    }
                    if (vl < lnav_data.ld_views[LNV_TEXT].get_inner_height()) {
                        new_top_opt = vl;
                    }
                },
                [&new_top_opt](const std::string& loc) {
                    log_info("file open request to jump to anchor: %s",
                             loc.c_str());
                    auto* ta = dynamic_cast<text_anchors*>(
                        lnav_data.ld_views[LNV_TEXT].get_sub_source());

                    if (ta != nullptr) {
                        new_top_opt = ta->row_for_anchor(loc);
                    }
                });
            if (new_top_opt) {
                log_info("  setting requested top line: %d",
                         (int) new_top_opt.value());
                text_view.set_selection(new_top_opt.value());
                log_info("  actual top is now: %d", (int) text_view.get_top());
                log_info("  actual selection is now: %d",
                         (int) text_view.get_selection());
                scroll_downs[LNV_TEXT] = false;
            } else {
                log_warning("could not jump to requested line");
            }
        }
        if (cb.did_promotion && deadline) {
            // If there's a new log file, extend the deadline so it can be
            // indexed quickly.
            deadline = deadline.value() + 500ms;
        }
    }

    std::vector<std::shared_ptr<logfile>> closed_files;
    for (auto& lf : lnav_data.ld_active_files.fc_files) {
        if ((!lf->exists() || lf->is_closed())) {
            log_info("closed log file: %s", lf->get_filename().c_str());
            lnav_data.ld_text_source.remove(lf);
            lnav_data.ld_log_source.remove_file(lf);
            closed_files.emplace_back(lf);
        }
    }
    if (!closed_files.empty()) {
        lnav_data.ld_active_files.close_files(closed_files);
    }

    auto result = lss.rebuild_index(deadline);
    if (result != logfile_sub_source::rebuild_result::rr_no_change) {
        size_t new_count = lss.text_line_count();
        bool force
            = result == logfile_sub_source::rebuild_result::rr_full_rebuild;

        if ((!scroll_downs[LNV_LOG]
             || log_view.get_top() > vis_line_t(new_count))
            && force)
        {
            scroll_downs[LNV_LOG] = false;
        }

        {
            std::unordered_map<std::string, std::list<std::shared_ptr<logfile>>>
                id_to_files;
            bool reload = false;

            for (const auto& lf : lnav_data.ld_active_files.fc_files) {
                id_to_files[lf->get_content_id()].push_back(lf);
            }

            for (auto& pair : id_to_files) {
                if (pair.second.size() == 1) {
                    continue;
                }

                pair.second.sort([](const auto& left, const auto& right) {
                    return right->get_stat().st_size < left->get_stat().st_size;
                });

                auto dupe_name = pair.second.front()->get_unique_path();
                pair.second.pop_front();
                for_each(pair.second.begin(),
                         pair.second.end(),
                         [&dupe_name](auto& lf) {
                             if (lf->mark_as_duplicate(dupe_name)) {
                                 log_info("Hiding duplicate file: %s",
                                          lf->get_filename().c_str());
                                 lnav_data.ld_log_source.find_data(lf) |
                                     [](auto ld) { ld->set_visibility(false); };
                             }
                         });
                reload = true;
            }

            if (reload) {
                lss.text_filters_changed();
            }
        }

        retval.rir_changes += 1;
    }

    for (auto lpc : {LNV_LOG, LNV_TEXT}) {
        auto& scroll_view = lnav_data.ld_views[lpc];

        if (scroll_downs[lpc]) {
            if (scroll_view.is_selectable()) {
                auto inner_height = scroll_view.get_inner_height();

                if (inner_height > 0_vl) {
                    scroll_view.set_selection(inner_height - 1_vl);
                }
            } else if (scroll_view.get_top_for_last_row()
                       > scroll_view.get_top())
            {
                scroll_view.set_top(scroll_view.get_top_for_last_row());
            }
        }
    }

    lnav_data.ld_view_stack.top() | [&closed_files, &retval](auto tc) {
        if (!closed_files.empty() && tc == &lnav_data.ld_views[LNV_GANTT]) {
            auto* gantt_source = lnav_data.ld_views[LNV_GANTT].get_sub_source();
            if (gantt_source != nullptr) {
                gantt_source->text_filters_changed();
            }
        }

        auto* tss = tc->get_sub_source();
        lnav_data.ld_filter_status_source.update_filtered(tss);
        if (retval.rir_changes > 0) {
            lnav_data.ld_scroll_broadcaster(tc);
        }
    };

    return retval;
}

void
rebuild_indexes_repeatedly()
{
    for (size_t attempt = 0; attempt < 10 && rebuild_indexes().rir_changes > 0;
         attempt++)
    {
        log_info("continuing to rebuild indexes...");
    }
}

bool
update_active_files(file_collection& new_files)
{
    static loading_observer obs;

    if (lnav_data.ld_active_files.fc_invalidate_merge) {
        lnav_data.ld_active_files.fc_invalidate_merge = false;

        return true;
    }

    bool was_below_open_file_limit
        = lnav_data.ld_active_files.is_below_open_file_limit();

    for (const auto& lf : new_files.fc_files) {
        lf->set_logfile_observer(&obs);
        lnav_data.ld_text_source.push_back(lf);
    }
    for (const auto& other_pair : new_files.fc_other_files) {
        switch (other_pair.second.ofd_format) {
            case file_format_t::SQLITE_DB:
                attach_sqlite_db(lnav_data.ld_db.in(), other_pair.first);
                break;
            default:
                break;
        }
    }
    lnav_data.ld_active_files.merge(new_files);
    lnav_data.ld_child_pollers.insert(
        lnav_data.ld_child_pollers.begin(),
        std::make_move_iterator(
            lnav_data.ld_active_files.fc_child_pollers.begin()),
        std::make_move_iterator(
            lnav_data.ld_active_files.fc_child_pollers.end()));
    lnav_data.ld_active_files.fc_child_pollers.clear();

    lnav::events::publish(
        lnav_data.ld_db.in(), new_files.fc_files, [](const auto& lf) {
            return lnav::events::file::open{
                lf->get_filename(),
            };
        });

    if (was_below_open_file_limit
        && !lnav_data.ld_active_files.is_below_open_file_limit())
    {
        auto um
            = lnav::console::user_message::error("Unable to open more files")
                  .with_reason(
                      attr_line_t("The file-descriptor limit of ")
                          .append(lnav::roles::number(fmt::to_string(
                              file_collection::get_limits().l_fds)))
                          .append(" is too low to support opening more files"))
                  .with_help(
                      attr_line_t("Use ")
                          .append("ulimit -n"_quoted_code)
                          .append(
                              " to increase the limit before running lnav"));

        lnav_data.ld_exec_context.ec_error_callback_stack.back()(um);
    }

    return true;
}

bool
rescan_files(bool req)
{
    auto& mlooper = injector::get<main_looper&, services::main_t>();
    bool done = false;
    auto delay = 0ms;

    do {
        auto fc = lnav_data.ld_active_files.rescan_files(req);
        bool all_synced = true;

        update_active_files(fc);
        mlooper.get_port().process_for(delay);
        for (const auto& pair : lnav_data.ld_active_files.fc_other_files) {
            if (pair.second.ofd_format != file_format_t::REMOTE) {
                continue;
            }

            if (lnav_data.ld_active_files.fc_name_to_errors->readAccess()
                    ->count(pair.first))
            {
                continue;
            }

            if (lnav_data.ld_active_files.fc_synced_files.count(pair.first)
                == 0)
            {
                all_synced = false;
            }
        }
        if (!lnav_data.ld_active_files.fc_name_to_errors->readAccess()->empty())
        {
            return false;
        }
        if (!all_synced) {
            delay = 30ms;
        }
        done = fc.fc_file_names.empty() && all_synced;
        if (!done && !(lnav_data.ld_flags & LNF_HEADLESS)) {
            lnav_data.ld_files_view.set_needs_update();
            lnav_data.ld_files_view.do_update();
            lnav_data.ld_status_refresher();
        }
    } while (!done && lnav_data.ld_looping);
    return true;
}
