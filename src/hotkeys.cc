/**
 * Copyright (c) 2015, Timothy Stack
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

#include "hotkeys.hh"

#include "base/ansi_scrubber.hh"
#include "base/injector.hh"
#include "base/itertools.hh"
#include "base/math_util.hh"
#include "base/opt_util.hh"
#include "bookmarks.hh"
#include "bound_tags.hh"
#include "command_executor.hh"
#include "config.h"
#include "field_overlay_source.hh"
#include "lnav.hh"
#include "lnav_config.hh"
#include "shlex.hh"
#include "sql_util.hh"
#include "sqlitepp.client.hh"
#include "xterm_mouse.hh"

using namespace lnav::roles::literals;

static int
key_sql_callback(exec_context& ec, sqlite3_stmt* stmt)
{
    if (!sqlite3_stmt_busy(stmt)) {
        return 0;
    }

    int ncols = sqlite3_column_count(stmt);

    auto& vars = ec.ec_local_vars.top();

    for (int lpc = 0; lpc < ncols; lpc++) {
        const char* column_name = sqlite3_column_name(stmt, lpc);

        if (sql_ident_needs_quote(column_name)) {
            continue;
        }

        auto* raw_value = sqlite3_column_value(stmt, lpc);
        auto value_type = sqlite3_column_type(stmt, lpc);
        scoped_value_t value;
        switch (value_type) {
            case SQLITE_INTEGER:
                value = (int64_t) sqlite3_value_int64(raw_value);
                break;
            case SQLITE_FLOAT:
                value = sqlite3_value_double(raw_value);
                break;
            case SQLITE_NULL:
                value = null_value_t{};
                break;
            default:
                value = std::string((const char*) sqlite3_value_text(raw_value),
                                    sqlite3_value_bytes(raw_value));
                break;
        }
        vars[column_name] = value;
    }

    return 0;
}

bool
handle_keyseq(const char* keyseq)
{
    const auto& km = lnav_config.lc_active_keymap;
    const auto& iter = km.km_seq_to_cmd.find(keyseq);
    if (iter == km.km_seq_to_cmd.end()) {
        return false;
    }

    logline_value_vector values;
    exec_context ec(&values, key_sql_callback, pipe_callback);
    auto& var_stack = ec.ec_local_vars;

    ec.ec_label_source_stack.push_back(&lnav_data.ld_db_row_source);
    ec.ec_global_vars = lnav_data.ld_exec_context.ec_global_vars;
    ec.ec_error_callback_stack
        = lnav_data.ld_exec_context.ec_error_callback_stack;
    var_stack.push(std::map<std::string, scoped_value_t>());
    // XXX push another so it doesn't look like interactive use
    var_stack.push(std::map<std::string, scoped_value_t>());
    auto& vars = var_stack.top();
    vars["keyseq"] = keyseq;
    const auto& kc = iter->second;

    log_debug(
        "executing key sequence %s: %s", keyseq, kc.kc_cmd.pp_value.c_str());
    auto sg = ec.enter_source(kc.kc_cmd.pp_location.sl_source,
                              kc.kc_cmd.pp_location.sl_line_number,
                              kc.kc_cmd.pp_value);
    auto result = execute_any(ec, kc.kc_cmd.pp_value);
    if (result.isOk()) {
        lnav_data.ld_rl_view->set_value(result.unwrap());
    } else {
        auto um = result.unwrapErr();

        ec.ec_error_callback_stack.back()(um);
    }

    if (!kc.kc_alt_msg.empty()) {
        shlex lexer(kc.kc_alt_msg);
        std::string expanded_msg;

        if (lexer.eval(expanded_msg,
                       {
                           &vars,
                           &ec.ec_global_vars,
                       }))
        {
            lnav_data.ld_rl_view->set_alt_value(expanded_msg);
        }
    }

    return true;
}

bool
handle_paging_key(int ch, const char* keyseq)
{
    if (lnav_data.ld_view_stack.empty()) {
        return false;
    }

    textview_curses* tc = *lnav_data.ld_view_stack.top();
    exec_context& ec = lnav_data.ld_exec_context;
    text_sub_source* tc_tss = tc->get_sub_source();
    bookmarks<vis_line_t>::type& bm = tc->get_bookmarks();

    if (tc->get_overlay_selection()) {
        if (tc->handle_key(ch)) {
            return true;
        }
    }

    if (handle_keyseq(keyseq)) {
        return true;
    }

    if (tc->handle_key(ch)) {
        return true;
    }

    auto lss = dynamic_cast<logfile_sub_source*>(tc->get_sub_source());
    auto text_accel_p = dynamic_cast<text_accel_source*>(tc->get_sub_source());

    /* process the command keystroke */
    switch (ch) {
        case 0x7f:
        case KEY_BACKSPACE:
            break;

        case 'a':
            if (lnav_data.ld_last_view == nullptr) {
                alerter::singleton().chime("no last view available");
            } else {
                auto* tc = lnav_data.ld_last_view;

                lnav_data.ld_last_view = nullptr;
                ensure_view(tc);
            }
            break;

        case 'A':
            if (lnav_data.ld_last_view == nullptr) {
                alerter::singleton().chime("no last view available");
            } else {
                auto* tc = lnav_data.ld_last_view;
                auto* top_tc = *lnav_data.ld_view_stack.top();
                auto* dst_view
                    = dynamic_cast<text_time_translator*>(tc->get_sub_source());
                auto* src_view = dynamic_cast<text_time_translator*>(
                    top_tc->get_sub_source());

                lnav_data.ld_last_view = nullptr;
                if (src_view != nullptr && dst_view != nullptr) {
                    src_view->time_for_row(top_tc->get_selection()) |
                        [dst_view, tc](auto top_ri) {
                            dst_view->row_for_time(top_ri.ri_time) |
                                [tc](auto row) { tc->set_selection(row); };
                        };
                }
                ensure_view(tc);
            }
            break;

        case KEY_F(2):
            if (xterm_mouse::is_available()) {
                auto& mouse_i = injector::get<xterm_mouse&>();
                mouse_i.set_enabled(!mouse_i.is_enabled());

                auto al = attr_line_t("mouse mode -- ")
                              .append(mouse_i.is_enabled() ? "enabled"_symbol
                                                           : "disabled"_symbol)
                              .move();
                if (mouse_i.is_enabled()
                    && lnav_config.lc_mouse_mode == lnav_mouse_mode::disabled)
                {
                    al.append(" -- enable permanently with ")
                        .append(":config /ui/mouse/mode enabled"_quoted_code);

                    auto clear_note = prepare_stmt(lnav_data.ld_db, R"(
DELETE FROM lnav_user_notifications WHERE id = 'org.lnav.mouse-support'
)");
                    clear_note.unwrap().execute();
                }
                auto um = lnav::console::user_message::ok(al);
                lnav_data.ld_rl_view->set_attr_value(um.to_attr_line());
            } else {
                lnav_data.ld_rl_view->set_value(
                    "error: mouse support is not available, make sure your "
                    "TERM is set to xterm or xterm-256color");
            }
            break;

        case 'C':
            if (lss) {
                lss->text_clear_marks(&textview_curses::BM_USER);
            }

            lnav_data.ld_select_start.erase(tc);
            lnav_data.ld_last_user_mark.erase(tc);
            tc->get_bookmarks()[&textview_curses::BM_USER].clear();
            tc->reload_data();

            lnav_data.ld_rl_view->set_attr_value(
                lnav::console::user_message::ok("Cleared bookmarks")
                    .to_attr_line());
            break;

        case '>': {
            auto range_opt = tc->horiz_shift(
                tc->get_top(), tc->get_bottom(), tc->get_left());
            if (range_opt && range_opt.value().second != INT_MAX) {
                tc->set_left(range_opt.value().second);
                lnav_data.ld_rl_view->set_alt_value(
                    HELP_MSG_1(m, "to bookmark a line"));
            } else {
                alerter::singleton().chime("no more search hits to the right");
            }
            break;
        }

        case '<':
            if (tc->get_left() == 0) {
                alerter::singleton().chime("no more search hits to the left");
            } else {
                auto range_opt = tc->horiz_shift(
                    tc->get_top(), tc->get_bottom(), tc->get_left());
                if (range_opt && range_opt.value().first != -1) {
                    tc->set_left(range_opt.value().first);
                } else {
                    tc->set_left(0);
                }
                lnav_data.ld_rl_view->set_alt_value(
                    HELP_MSG_1(m, "to bookmark a line"));
            }
            break;

        case 'f':
            if (tc == &lnav_data.ld_views[LNV_LOG]) {
                bm[&logfile_sub_source::BM_FILES].next(tc->get_selection()) |
                    [&tc](auto vl) { tc->set_selection(vl); };
            } else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
                textfile_sub_source& tss = lnav_data.ld_text_source;

                if (!tss.empty()) {
                    tss.rotate_left();
                }
                tc->reload_data();
            }
            break;

        case 'F':
            if (tc == &lnav_data.ld_views[LNV_LOG]) {
                bm[&logfile_sub_source::BM_FILES].prev(tc->get_selection()) |
                    [&tc](auto vl) {
                        // setting the selection for movement to previous file
                        // marker instead of the top will move the cursor, too,
                        // if needed.
                        tc->set_selection(vl);
                    };
            } else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
                textfile_sub_source& tss = lnav_data.ld_text_source;

                if (!tss.empty()) {
                    tss.rotate_right();
                }
                tc->reload_data();
            }
            break;

        case 'z':
            if ((lnav_data.ld_zoom_level - 1) < 0) {
                alerter::singleton().chime("maximum zoom-in level reached");
            } else {
                execute_command(
                    ec,
                    "zoom-to "
                        + lnav_zoom_strings[lnav_data.ld_zoom_level - 1]);
            }
            break;

        case 'Z':
            if ((lnav_data.ld_zoom_level + 1) >= ZOOM_COUNT) {
                alerter::singleton().chime("maximum zoom-out level reached");
            } else {
                execute_command(
                    ec,
                    "zoom-to "
                        + lnav_zoom_strings[lnav_data.ld_zoom_level + 1]);
            }
            break;

        case 'J':
            if (tc->is_selectable()) {
                if (tc->get_selection() >= 0_vl) {
                    tc->toggle_user_mark(&textview_curses::BM_USER,
                                         tc->get_selection());
                    lnav_data.ld_select_start[tc] = tc->get_selection();
                    lnav_data.ld_last_user_mark[tc] = tc->get_selection();
                    if (tc->get_selection() + 1_vl < tc->get_inner_height()) {
                        tc->set_selection(tc->get_selection() + 1_vl);
                    }
                }
            } else {
                if (lnav_data.ld_last_user_mark.find(tc)
                        == lnav_data.ld_last_user_mark.end()
                    || !tc->is_line_visible(
                        vis_line_t(lnav_data.ld_last_user_mark[tc])))
                {
                    lnav_data.ld_select_start[tc] = tc->get_selection();
                    lnav_data.ld_last_user_mark[tc] = tc->get_selection();
                } else {
                    vis_line_t height;
                    unsigned long width;

                    tc->get_dimensions(height, width);
                    if (lnav_data.ld_last_user_mark[tc] > (tc->get_bottom() - 2)
                        && tc->get_selection() + height
                            < tc->get_inner_height())
                    {
                        tc->shift_top(1_vl);
                    }
                    if (lnav_data.ld_last_user_mark[tc] + 1
                        >= tc->get_inner_height())
                    {
                        break;
                    }
                    lnav_data.ld_last_user_mark[tc] += 1;
                }
                tc->toggle_user_mark(
                    &textview_curses::BM_USER,
                    vis_line_t(lnav_data.ld_last_user_mark[tc]));
            }
            tc->reload_data();

            lnav_data.ld_rl_view->set_alt_value(
                HELP_MSG_1(c, "to copy marked lines to the clipboard"));
            break;

        case 'K': {
            int new_mark;

            if (lnav_data.ld_last_user_mark.find(tc)
                    == lnav_data.ld_last_user_mark.end()
                || !tc->is_line_visible(
                    vis_line_t(lnav_data.ld_last_user_mark[tc])))
            {
                new_mark = tc->get_selection();
            } else {
                new_mark = lnav_data.ld_last_user_mark[tc];
            }

            tc->toggle_user_mark(&textview_curses::BM_USER,
                                 vis_line_t(new_mark));
            if (new_mark == tc->get_selection() && tc->get_top() > 0_vl) {
                tc->shift_top(-1_vl);
            }
            if (new_mark > 0) {
                lnav_data.ld_last_user_mark[tc] = new_mark - 1;
            } else {
                lnav_data.ld_last_user_mark[tc] = new_mark;
                alerter::singleton().chime("no more lines to mark");
            }
            lnav_data.ld_select_start[tc] = tc->get_selection();
            if (tc->is_selectable() && tc->get_selection() > 0_vl) {
                tc->set_selection(tc->get_selection() - 1_vl);
            }
            tc->reload_data();

            lnav_data.ld_rl_view->set_alt_value(
                HELP_MSG_1(c, "to copy marked lines to the clipboard"));
            break;
        }

        case 'M':
            if (lnav_data.ld_last_user_mark.find(tc)
                == lnav_data.ld_last_user_mark.end())
            {
                alerter::singleton().chime("no lines have been marked");
            } else {
                int start_line = std::min((int) tc->get_selection(),
                                          lnav_data.ld_last_user_mark[tc] + 1);
                int end_line = std::max((int) tc->get_selection(),
                                        lnav_data.ld_last_user_mark[tc] - 1);

                tc->toggle_user_mark(&textview_curses::BM_USER,
                                     vis_line_t(start_line),
                                     vis_line_t(end_line));
                tc->reload_data();
            }
            break;

#if 0
            case 'S':
                {
                    bookmark_vector<vis_line_t>::iterator iter;

                    for (iter = bm[&textview_curses::BM_SEARCH].begin();
                         iter != bm[&textview_curses::BM_SEARCH].end();
                         ++iter) {
                        tc->toggle_user_mark(&textview_curses::BM_USER, *iter);
                    }

                    lnav_data.ld_last_user_mark[tc] = -1;
                    tc->reload_data();
                }
                break;
#endif

        case 's':
            if (text_accel_p && text_accel_p->is_time_offset_supported()) {
                auto next_top = tc->get_selection() + 1_vl;

                if (!tc->is_selectable()) {
                    next_top += 1_vl;
                }

                if (!text_accel_p->is_time_offset_enabled()) {
                    lnav_data.ld_rl_view->set_alt_value(
                        HELP_MSG_1(T, "to disable elapsed-time mode"));
                }
                text_accel_p->set_time_offset(true);
                while (next_top < tc->get_inner_height()) {
                    if (!text_accel_p->text_accel_get_line(next_top)
                             ->is_message())
                    {
                    } else if (text_accel_p->get_line_accel_direction(next_top)
                               == log_accel::A_DECEL)
                    {
                        if (!tc->is_selectable()) {
                            --next_top;
                        }
                        tc->set_selection(next_top);
                        break;
                    }

                    ++next_top;
                }
            }
            break;

        case 'S':
            if (text_accel_p && text_accel_p->is_time_offset_supported()) {
                auto next_top = tc->get_selection();

                if (tc->is_selectable() && next_top > 0_vl) {
                    next_top -= 1_vl;
                }
                if (!text_accel_p->is_time_offset_enabled()) {
                    lnav_data.ld_rl_view->set_alt_value(
                        HELP_MSG_1(T, "to disable elapsed-time mode"));
                }
                text_accel_p->set_time_offset(true);
                while (0 <= next_top && next_top < tc->get_inner_height()) {
                    if (!text_accel_p->text_accel_get_line(next_top)
                             ->is_message())
                    {
                    } else if (text_accel_p->get_line_accel_direction(next_top)
                               == log_accel::A_DECEL)
                    {
                        if (!tc->is_selectable()) {
                            --next_top;
                        }
                        tc->set_selection(next_top);
                        break;
                    }

                    --next_top;
                }
            }
            break;

        case '9':
            if (lss) {
                double tenth = ((double) tc->get_inner_height()) / 10.0;

                tc->shift_top(vis_line_t(tenth));
            }
            break;

        case '(':
            if (lss) {
                double tenth = ((double) tc->get_inner_height()) / 10.0;

                tc->shift_top(vis_line_t(-tenth));
            }
            break;

        case '0':
            if (lss) {
                const int step = 24 * 60 * 60;
                lss->time_for_row(tc->get_selection()) |
                    [lss, tc](auto first_ri) {
                        lss->find_from_time(
                            roundup_size(first_ri.ri_time.tv_sec, step))
                            | [tc](auto line) { tc->set_selection(line); };
                    };
            }
            break;

        case ')':
            if (lss) {
                lss->time_for_row(tc->get_selection()) |
                    [lss, tc](auto first_ri) {
                        time_t day
                            = rounddown(first_ri.ri_time.tv_sec, 24 * 60 * 60);
                        lss->find_from_time(day) | [tc](auto line) {
                            if (line != 0_vl) {
                                --line;
                            }
                            tc->set_selection(line);
                        };
                    };
            }
            break;

        case 'D':
            if (tc->get_selection() == 0) {
                alerter::singleton().chime(
                    "the top of the log has been reached");
            } else if (lss) {
                lss->time_for_row(tc->get_selection()) |
                    [lss, ch, tc](auto first_ri) {
                        int step = ch == 'D' ? (24 * 60 * 60) : (60 * 60);
                        time_t top_time = first_ri.ri_time.tv_sec;
                        lss->find_from_time(top_time - step) | [tc](auto line) {
                            if (line != 0_vl) {
                                --line;
                            }
                            tc->set_selection(line);
                        };
                    };

                lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(/, "to search"));
            }
            break;

        case 'd':
            if (lss) {
                lss->time_for_row(tc->get_selection()) |
                    [ch, lss, tc](auto first_ri) {
                        int step = ch == 'd' ? (24 * 60 * 60) : (60 * 60);
                        lss->find_from_time(first_ri.ri_time.tv_sec + step) |
                            [tc](auto line) { tc->set_selection(line); };
                    };

                lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(/, "to search"));
            }
            break;

        case 'o':
        case 'O':
            if (lss != nullptr) {
                auto start_win = lss->window_at(tc->get_selection());
                auto start_win_iter = start_win.begin();
                const auto& opid_opt
                    = start_win_iter->get_values().lvv_opid_value;
                if (!opid_opt) {
                    alerter::singleton().chime(
                        "Log message does not contain an opid");
                    lnav_data.ld_rl_view->set_attr_value(
                        lnav::console::user_message::error(
                            "Log message does not contain an opid")
                            .to_attr_line());
                } else {
                    const auto& start_line = start_win_iter->get_logline();
                    unsigned int opid_hash = start_line.get_opid();
                    auto next_win
                        = lss->window_to_end(start_win_iter->get_vis_line());
                    auto next_win_iter = next_win.begin();
                    bool found = false;

                    while (true) {
                        if (ch == 'o') {
                            ++next_win_iter;
                            if (next_win_iter == next_win.end()) {
                                break;
                            }
                        } else {
                            if (next_win_iter->get_vis_line() == 0) {
                                break;
                            }
                            --next_win_iter;
                        }
                        const auto& next_line = next_win_iter->get_logline();
                        if (!next_line.match_opid_hash(opid_hash)) {
                            continue;
                        }
                        const auto& next_opid_opt
                            = next_win_iter->get_values().lvv_opid_value;
                        if (!next_opid_opt
                            || opid_opt.value() != next_opid_opt.value())
                        {
                            continue;
                        }
                        found = true;
                        break;
                    }
                    if (found) {
                        lnav_data.ld_rl_view->set_value("");
                        tc->set_selection(next_win_iter->get_vis_line());
                    } else {
                        lnav_data.ld_rl_view->set_attr_value(
                            lnav::console::user_message::error(
                                attr_line_t(
                                    "No more messages found with opid: ")
                                    .append(
                                        lnav::roles::symbol(opid_opt.value())))
                                .to_attr_line());
                        alerter::singleton().chime(
                            "no more messages found with opid");
                    }
                }
            }
            break;

        case 't':
            if (lnav_data.ld_text_source.current_file() == nullptr) {
                alerter::singleton().chime("No text files loaded");
                lnav_data.ld_rl_view->set_attr_value(
                    lnav::console::user_message::error("No text files loaded")
                        .to_attr_line());
            } else if (toggle_view(&lnav_data.ld_views[LNV_TEXT])) {
                lnav_data.ld_rl_view->set_alt_value(
                    HELP_MSG_2(f, F, "to switch to the next/previous file"));
            }
            break;

        case 'I': {
            auto& hist_tc = lnav_data.ld_views[LNV_HISTOGRAM];

            if (toggle_view(&hist_tc)) {
                auto* src_view
                    = dynamic_cast<text_time_translator*>(tc->get_sub_source());

                if (src_view != nullptr) {
                    src_view->time_for_row(tc->get_selection()) |
                        [](auto log_top_ri) {
                            lnav_data.ld_hist_source2.row_for_time(
                                log_top_ri.ri_time)
                                | [](auto row) {
                                      lnav_data.ld_views[LNV_HISTOGRAM]
                                          .set_selection(row);
                                  };
                        };
                }
            } else {
                lnav_data.ld_view_stack.top() | [&](auto top_tc) {
                    auto* dst_view = dynamic_cast<text_time_translator*>(
                        top_tc->get_sub_source());

                    if (dst_view != nullptr) {
                        auto& hs = lnav_data.ld_hist_source2;
                        auto hist_top_time_opt
                            = hs.time_for_row(hist_tc.get_selection());
                        auto curr_top_time_opt
                            = dst_view->time_for_row(top_tc->get_selection());
                        if (hist_top_time_opt && curr_top_time_opt
                            && hs.row_for_time(hist_top_time_opt->ri_time)
                                != hs.row_for_time(curr_top_time_opt->ri_time))
                        {
                            dst_view->row_for_time(hist_top_time_opt->ri_time) |
                                [top_tc](auto new_top) {
                                    top_tc->set_selection(new_top);
                                    top_tc->set_needs_update();
                                };
                        }
                    }
                };
            }
            break;
        }

        case 'V': {
            textview_curses* db_tc = &lnav_data.ld_views[LNV_DB];
            db_label_source& dls = lnav_data.ld_db_row_source;

            if (toggle_view(db_tc)) {
                auto log_line_index = dls.column_name_to_index("log_line");

                if (!log_line_index) {
                    log_line_index = dls.column_name_to_index("min(log_line)");
                }

                if (log_line_index) {
                    fmt::memory_buffer linestr;
                    int line_number = (int) tc->get_selection();
                    unsigned int row;

                    fmt::format_to(std::back_inserter(linestr),
                                   FMT_STRING("{}"),
                                   line_number);
                    linestr.push_back('\0');
                    for (row = 0; row < dls.dls_rows.size(); row++) {
                        if (strcmp(dls.dls_rows[row][log_line_index.value()],
                                   linestr.data())
                            == 0)
                        {
                            vis_line_t db_line(row);

                            db_tc->set_selection(db_line);
                            db_tc->set_needs_update();
                            break;
                        }
                    }
                }
            } else if (db_tc->get_inner_height() > 0) {
                int db_row = db_tc->get_selection();
                tc = &lnav_data.ld_views[LNV_LOG];
                auto log_line_index = dls.column_name_to_index("log_line");

                if (!log_line_index) {
                    log_line_index = dls.column_name_to_index("min(log_line)");
                }

                if (log_line_index) {
                    unsigned int line_number;

                    if (sscanf(dls.dls_rows[db_row][log_line_index.value()],
                               "%d",
                               &line_number)
                        && line_number < tc->listview_rows(*tc))
                    {
                        tc->set_selection(vis_line_t(line_number));
                        tc->set_needs_update();
                    }
                } else {
                    for (size_t lpc = 0; lpc < dls.dls_headers.size(); lpc++) {
                        date_time_scanner dts;
                        struct timeval tv;
                        struct exttm tm;
                        const char* col_value = dls.dls_rows[db_row][lpc];
                        size_t col_len = strlen(col_value);

                        if (dts.scan(col_value, col_len, nullptr, &tm, tv)
                            != nullptr)
                        {
                            lnav_data.ld_log_source.find_from_time(tv) |
                                [tc](auto vl) {
                                    tc->set_selection(vl);
                                    tc->set_needs_update();
                                };
                            break;
                        }
                    }
                }
            }
        } break;

        case '\t':
        case KEY_BTAB:
            if (tc == &lnav_data.ld_views[LNV_DB]) {
            } else if (tc == &lnav_data.ld_views[LNV_SPECTRO]) {
                lnav_data.ld_mode = ln_mode_t::SPECTRO_DETAILS;
            } else if (tc_tss != nullptr && tc_tss->tss_supports_filtering) {
                lnav_data.ld_mode = lnav_data.ld_last_config_mode;
                lnav_data.ld_filter_view.reload_data();
                lnav_data.ld_files_view.reload_data();
                if (tc->get_inner_height() > 0_vl) {
                    std::vector<attr_line_t> rows(1);

                    tc->get_data_source()->listview_value_for_rows(
                        *tc, tc->get_top(), rows);
                    string_attrs_t& sa = rows[0].get_attrs();
                    auto line_attr_opt = get_string_attr(sa, logline::L_FILE);
                    if (line_attr_opt) {
                        const auto& fc = lnav_data.ld_active_files;
                        auto lf = line_attr_opt.value().get();
                        auto index_opt
                            = fc.fc_files | lnav::itertools::find(lf);
                        if (index_opt) {
                            auto index_vl = vis_line_t(index_opt.value());

                            lnav_data.ld_files_view.set_top(index_vl);
                            lnav_data.ld_files_view.set_selection(index_vl);
                        }
                    }
                }
            } else {
                alerter::singleton().chime(
                    "no configuration panels in this view");
            }
            break;

        case 'r':
        case 'R':
            if (lss != nullptr) {
                const auto& last_time = injector::get<const relative_time&,
                                                      last_relative_time_tag>();

                if (last_time.empty()) {
                    lnav_data.ld_rl_view->set_value(
                        "Use the 'goto' command to set the relative time to "
                        "move by");
                } else {
                    vis_line_t vl = tc->get_selection(), new_vl;
                    relative_time rt = last_time;
                    content_line_t cl;
                    struct exttm tm;
                    bool done = false;

                    if (ch == 'r') {
                        if (rt.is_negative()) {
                            rt.negate();
                        }
                    } else if (ch == 'R') {
                        if (!rt.is_negative()) {
                            rt.negate();
                        }
                    }

                    cl = lnav_data.ld_log_source.at(vl);
                    logline* ll = lnav_data.ld_log_source.find_line(cl);
                    ll->to_exttm(tm);
                    do {
                        tm = rt.adjust(tm);
                        auto new_vl_opt
                            = lnav_data.ld_log_source.find_from_time(tm);
                        if (!new_vl_opt) {
                            break;
                        }
                        new_vl = new_vl_opt.value();

                        if (new_vl == 0_vl || new_vl != vl || !rt.is_relative())
                        {
                            vl = new_vl;
                            done = true;
                        }
                    } while (!done);
                    tc->set_selection(vl);
                    lnav_data.ld_rl_view->set_value(" " + rt.to_string());
                }
            }
            break;

        case KEY_CTRL('p'):
            lnav_data.ld_preview_hidden = !lnav_data.ld_preview_hidden;
            break;

        default:
            log_debug("key sequence %x", ch);
            return false;
    }
    return true;
}
