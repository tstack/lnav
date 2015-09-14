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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "lnav.hh"
#include "bookmarks.hh"
#include "sql_util.hh"
#include "environ_vtab.hh"
#include "plain_text_source.hh"
#include "pretty_printer.hh"
#include "sysclip.hh"
#include "log_data_helper.hh"
#include "session_data.hh"
#include "command_executor.hh"
#include "termios_guard.hh"
#include "readline_possibilities.hh"
#include "field_overlay_source.hh"
#include "hotkeys.hh"

using namespace std;

static bookmark_type_t BM_EXAMPLE("");

/* XXX For one, this code is kinda crappy.  For two, we should probably link
 * directly with X so we don't need to have xclip installed and it'll work if
 * we're ssh'd into a box.
 */
static void copy_to_xclip(void)
{
    textview_curses *tc = lnav_data.ld_view_stack.top();

    bookmark_vector<vis_line_t> &bv =
            tc->get_bookmarks()[&textview_curses::BM_USER];
    bookmark_vector<vis_line_t>::iterator iter;
    auto_mem<FILE> pfile(pclose);
    int    line_count = 0;
    string line;

    pfile = open_clipboard(CT_GENERAL);

    if (!pfile.in()) {
        alerter::singleton().chime();
        lnav_data.ld_rl_view->set_value(
                "error: Unable to copy to clipboard.  "
                        "Make sure xclip or pbcopy is installed.");
        return;
    }

    for (iter = bv.begin(); iter != bv.end(); iter++) {
        tc->grep_value_for_line(*iter, line);
        fprintf(pfile, "%s\n", line.c_str());
        line_count += 1;
    }

    char buffer[128];

    snprintf(buffer, sizeof(buffer),
             "Copied " ANSI_BOLD("%d") " lines to the clipboard",
             line_count);
    lnav_data.ld_rl_view->set_value(buffer);
}

static void back_ten(int ten_minute)
{
    textview_curses *   tc  = lnav_data.ld_view_stack.top();
    logfile_sub_source *lss;

    lss = dynamic_cast<logfile_sub_source *>(tc->get_sub_source());

    if (!lss)
        return;

    time_t hour = rounddown_offset(lnav_data.ld_top_time,
                                   60 * 60,
                                   ten_minute * 10 * 60);
    vis_line_t line = lss->find_from_time(hour);

    --line;
    lnav_data.ld_view_stack.top()->set_top(line);
}

void update_view_name(void)
{
    status_field &sf = lnav_data.ld_top_source.statusview_value_for_field(
            top_status_source::TSF_VIEW_NAME);
    textview_curses * tc = lnav_data.ld_view_stack.top();
    struct line_range lr(0);

    sf.set_value("% 5s ", tc->get_title().c_str());
    sf.get_value().get_attrs().push_back(
            string_attr(lr, &view_curses::VC_STYLE,
                        A_REVERSE | view_colors::ansi_color_pair(COLOR_BLUE, COLOR_WHITE)));
}

void handle_paging_key(int ch)
{
    textview_curses *   tc  = lnav_data.ld_view_stack.top();
    logfile_sub_source *lss = NULL;
    bookmarks<vis_line_t>::type &     bm  = tc->get_bookmarks();

    if (tc->handle_key(ch)) {
        return;
    }

    lss = dynamic_cast<logfile_sub_source *>(tc->get_sub_source());

    /* process the command keystroke */
    switch (ch) {
        case 'q':
        case 'Q':
        {
            string msg = "";

            if (tc == &lnav_data.ld_views[LNV_DB]) {
                msg = HELP_MSG_2(v, V, "to switch to the SQL result view");
            }
            else if (tc == &lnav_data.ld_views[LNV_HISTOGRAM]) {
                msg = HELP_MSG_2(i, I, "to switch to the histogram view");
            }
            else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
                msg = HELP_MSG_1(t, "to switch to the text file view");
            }
            else if (tc == &lnav_data.ld_views[LNV_GRAPH]) {
                msg = HELP_MSG_1(g, "to switch to the graph view");
            }

            lnav_data.ld_rl_view->set_alt_value(msg);
        }
            lnav_data.ld_view_stack.pop();
            if (lnav_data.ld_view_stack.empty() ||
                (lnav_data.ld_view_stack.size() == 1 &&
                 lnav_data.ld_log_source.text_line_count() == 0)) {
                lnav_data.ld_looping = false;
            }
            else {
                tc = lnav_data.ld_view_stack.top();
                tc->set_needs_update();
                lnav_data.ld_scroll_broadcaster.invoke(tc);
                update_view_name();
            }
            break;

        case KEY_F(2):
            if (xterm_mouse::is_available()) {
                lnav_data.ld_mouse.set_enabled(!lnav_data.ld_mouse.is_enabled());
            }
            else {
                lnav_data.ld_rl_view->set_value(
                        "error: mouse support is not available, make sure your TERM is set to "
                                "xterm or xterm-256color");
            }
            break;

        case 'c':
            copy_to_xclip();
            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                    C, "to clear marked messages"));
            break;

        case 'C':
            if (lss) {
                lss->text_clear_marks(&textview_curses::BM_USER);
            }

            lnav_data.ld_select_start.erase(tc);
            lnav_data.ld_last_user_mark.erase(tc);
            tc->get_bookmarks()[&textview_curses::BM_USER].clear();
            tc->reload_data();

            lnav_data.ld_rl_view->set_value("Cleared bookmarks");
            break;

        case 'e':
            moveto_cluster(&bookmark_vector<vis_line_t>::next,
                           &logfile_sub_source::BM_ERRORS,
                           tc->get_top());
            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                    w, W,
                    "to move forward/backward through warning messages"));
            break;

        case 'E':
            moveto_cluster(&bookmark_vector<vis_line_t>::prev,
                           &logfile_sub_source::BM_ERRORS,
                           tc->get_top());
            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                    w, W,
                    "to move forward/backward through warning messages"));
            break;

        case 'w':
            moveto_cluster(&bookmark_vector<vis_line_t>::next,
                           &logfile_sub_source::BM_WARNINGS,
                           tc->get_top());
            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                    o, O,
                    "to move forward/backward an hour"));
            break;

        case 'W':
            moveto_cluster(&bookmark_vector<vis_line_t>::prev,
                           &logfile_sub_source::BM_WARNINGS,
                           tc->get_top());
            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                    o, O,
                    "to move forward/backward an hour"));
            break;

        case 'n':
            tc->set_top(bm[&textview_curses::BM_SEARCH].next(tc->get_top()));
            lnav_data.ld_bottom_source.grep_error("");
            lnav_data.ld_rl_view->set_alt_value(
                    "Press '" ANSI_BOLD(">") "' or '" ANSI_BOLD("<")
            "' to scroll horizontally to a search result");
            break;

        case 'N':
            tc->set_top(bm[&textview_curses::BM_SEARCH].prev(tc->get_top()));
            lnav_data.ld_bottom_source.grep_error("");
            lnav_data.ld_rl_view->set_alt_value(
                    "Press '" ANSI_BOLD(">") "' or '" ANSI_BOLD("<")
            "' to scroll horizontally to a search result");
            break;

        case 'y':
            tc->set_top(bm[&BM_QUERY].next(tc->get_top()));
            break;

        case 'Y':
            tc->set_top(bm[&BM_QUERY].prev(tc->get_top()));
            break;

        case '>':
        {
            std::pair<int, int> range;

            tc->horiz_shift(tc->get_top(),
                            tc->get_bottom(),
                            tc->get_left(),
                            "$search",
                            range);
            if (range.second != INT_MAX) {
                tc->set_left(range.second);
                lnav_data.ld_rl_view->set_alt_value(
                        HELP_MSG_1(m, "to bookmark a line"));
            }
            else{
                alerter::singleton().chime();
            }
        }
            break;

        case '<':
            if (tc->get_left() == 0) {
                alerter::singleton().chime();
            }
            else {
                std::pair<int, int> range;

                tc->horiz_shift(tc->get_top(),
                                tc->get_bottom(),
                                tc->get_left(),
                                "$search",
                                range);
                if (range.first != -1) {
                    tc->set_left(range.first);
                }
                else{
                    tc->set_left(0);
                }
                lnav_data.ld_rl_view->set_alt_value(
                        HELP_MSG_1(m, "to bookmark a line"));
            }
            break;

        case 'f':
            if (tc == &lnav_data.ld_views[LNV_LOG]) {
                tc->set_top(bm[&logfile_sub_source::BM_FILES].next(tc->get_top()));
            }
            else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
                textfile_sub_source &tss = lnav_data.ld_text_source;

                if (!tss.empty()) {
                    tss.rotate_right();
                    redo_search(LNV_TEXT);
                }
            }
            break;

        case 'F':
            if (tc == &lnav_data.ld_views[LNV_LOG]) {
                tc->set_top(bm[&logfile_sub_source::BM_FILES].prev(tc->get_top()));
            }
            else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
                textfile_sub_source &tss = lnav_data.ld_text_source;

                if (!tss.empty()) {
                    tss.rotate_left();
                    redo_search(LNV_TEXT);
                }
            }
            break;

        case 'z':
            if (tc == &lnav_data.ld_views[LNV_HISTOGRAM]) {
                if ((lnav_data.ld_hist_zoom + 1) >= HIST_ZOOM_LEVELS) {
                    alerter::singleton().chime();
                }
                else {
                    lnav_data.ld_hist_zoom += 1;
                    rebuild_hist(0, true);
                }

                lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                        I,
                        "to switch to the log view at the top displayed time"));
            }
            break;

        case 'Z':
            if (tc == &lnav_data.ld_views[LNV_HISTOGRAM]) {
                if (lnav_data.ld_hist_zoom == 0) {
                    alerter::singleton().chime();
                }
                else {
                    lnav_data.ld_hist_zoom -= 1;
                    rebuild_hist(0, true);
                }

                lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                        I,
                        "to switch to the log view at the top displayed time"));
            }
            break;

        case 'u': {
            vis_line_t user_top, part_top;

            lnav_data.ld_rl_view->set_alt_value(
                    HELP_MSG_1(c, "to copy marked lines to the clipboard; ")
            HELP_MSG_1(C, "to clear marked lines"));

            user_top = next_cluster(&bookmark_vector<vis_line_t>::next,
                                    &textview_curses::BM_USER,
                                    tc->get_top());
            part_top = next_cluster(&bookmark_vector<vis_line_t>::next,
                                    &textview_curses::BM_PARTITION,
                                    tc->get_top());
            if (part_top == -1 && user_top == -1) {
                alerter::singleton().chime();
            }
            else if (part_top == -1) {
                tc->set_top(user_top);
            }
            else if (user_top == -1) {
                tc->set_top(part_top);
            }
            else {
                tc->set_top(min(user_top, part_top));
            }
            break;
        }

        case 'U': {
            vis_line_t user_top, part_top;

            user_top = next_cluster(&bookmark_vector<vis_line_t>::prev,
                                    &textview_curses::BM_USER,
                                    tc->get_top());
            part_top = next_cluster(&bookmark_vector<vis_line_t>::prev,
                                    &textview_curses::BM_PARTITION,
                                    tc->get_top());
            if (part_top == -1 && user_top == -1) {
                alerter::singleton().chime();
            }
            else if (part_top == -1) {
                tc->set_top(user_top);
            }
            else if (user_top == -1) {
                tc->set_top(part_top);
            }
            else {
                tc->set_top(max(user_top, part_top));
            }
            break;
        }

        case 'm':
            lnav_data.ld_last_user_mark[tc] = tc->get_top();
            tc->toggle_user_mark(&textview_curses::BM_USER,
                                 vis_line_t(lnav_data.ld_last_user_mark[tc]));
            tc->reload_data();

            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                    u, U,
                    "to move forward/backward through user bookmarks"));
            break;

        case 'J':
            if (lnav_data.ld_last_user_mark.find(tc) ==
                lnav_data.ld_last_user_mark.end() ||
                !tc->is_visible(vis_line_t(lnav_data.ld_last_user_mark[tc]))) {
                lnav_data.ld_select_start[tc] = tc->get_top();
                lnav_data.ld_last_user_mark[tc] = tc->get_top();
            }
            else {
                vis_line_t    height;
                unsigned long width;

                tc->get_dimensions(height, width);
                if (lnav_data.ld_last_user_mark[tc] > tc->get_bottom() - 2 &&
                    tc->get_top() + height < tc->get_inner_height()) {
                    tc->shift_top(vis_line_t(1));
                }
                if (lnav_data.ld_last_user_mark[tc] + 1 >=
                    tc->get_inner_height()) {
                    break;
                }
                lnav_data.ld_last_user_mark[tc] += 1;
            }
            tc->toggle_user_mark(&textview_curses::BM_USER,
                                 vis_line_t(lnav_data.ld_last_user_mark[tc]));
            tc->reload_data();

            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                    c,
                    "to copy marked lines to the clipboard"));
            break;

        case 'K':
        {
            int new_mark;

            if (lnav_data.ld_last_user_mark.find(tc) ==
                lnav_data.ld_last_user_mark.end() ||
                !tc->is_visible(vis_line_t(lnav_data.ld_last_user_mark[tc]))) {
                new_mark = tc->get_top();
            }
            else {
                new_mark = lnav_data.ld_last_user_mark[tc];
            }

            tc->toggle_user_mark(&textview_curses::BM_USER,
                                 vis_line_t(new_mark));
            if (new_mark == tc->get_top()) {
                tc->shift_top(vis_line_t(-1));
            }
            if (new_mark > 0) {
                lnav_data.ld_last_user_mark[tc] = new_mark - 1;
            }
            else {
                lnav_data.ld_last_user_mark[tc] = new_mark;
                alerter::singleton().chime();
            }
            lnav_data.ld_select_start[tc] = tc->get_top();
            tc->reload_data();

            lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(
                    c,
                    "to copy marked lines to the clipboard"));
        }
            break;

        case KEY_CTRL_L: {
            vis_line_t top = tc->get_top();
            vis_line_t bottom = tc->get_bottom();
            char line_break[120];

            nodelay(lnav_data.ld_window, 0);
            endwin();
            {
                guard_termios tguard(STDOUT_FILENO);
                struct termios new_termios = *tguard.get_termios();
                new_termios.c_oflag |= ONLCR | OPOST;
                tcsetattr(STDOUT_FILENO, TCSANOW, &new_termios);
                snprintf(line_break, sizeof(line_break),
                         "\n---------------- Lines %'d-%'d, "
                                 "press any key to exit lo-fi mode "
                                 "----------------\n\n",
                         (int) top, (int) bottom);
                write(STDOUT_FILENO, line_break, strlen(line_break));
                for (; top <= bottom; ++top) {
                    attr_line_t al;
                    tc->listview_value_for_row(*tc, top, al);
                    struct line_range lr = find_string_attr_range(
                            al.get_attrs(), &textview_curses::SA_ORIGINAL_LINE);
                    write(STDOUT_FILENO, lr.substr(al.get_string()),
                          lr.sublen(al.get_string()));
                    write(STDOUT_FILENO, "\n", 1);
                }
            }
            cbreak();
            getch();
            refresh();
            nodelay(lnav_data.ld_window, 1);
            break;
        }

        case 'M':
            if (lnav_data.ld_last_user_mark.find(tc) ==
                lnav_data.ld_last_user_mark.end()) {
                alerter::singleton().chime();
            }
            else {
                int start_line = min((int)tc->get_top(),
                                     lnav_data.ld_last_user_mark[tc] + 1);
                int end_line = max((int)tc->get_top(),
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
            if (lss) {
                vis_line_t next_top = vis_line_t(tc->get_top() + 2);

                if (!lss->is_time_offset_enabled()) {
                    lnav_data.ld_rl_view->set_alt_value(
                            HELP_MSG_1(T, "to disable elapsed-time mode"));
                }
                lss->set_time_offset(true);
                while (next_top < tc->get_inner_height()) {
                    if (lss->find_line(lss->at(next_top))->is_continued()) {
                    }
                    else if (lss->get_line_accel_direction(next_top) ==
                             log_accel::A_DECEL) {
                        --next_top;
                        tc->set_top(next_top);
                        break;
                    }

                    ++next_top;
                }
            }
            break;

        case 'S':
            if (lss) {
                vis_line_t next_top = tc->get_top();

                if (!lss->is_time_offset_enabled()) {
                    lnav_data.ld_rl_view->set_alt_value(
                            HELP_MSG_1(T, "to disable elapsed-time mode"));
                }
                lss->set_time_offset(true);
                while (0 <= next_top && next_top < tc->get_inner_height()) {
                    if (lss->find_line(lss->at(next_top))->is_continued()) {
                    }
                    else if (lss->get_line_accel_direction(next_top) ==
                             log_accel::A_DECEL) {
                        --next_top;
                        tc->set_top(next_top);
                        break;
                    }

                    --next_top;
                }
            }
            break;

        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
            if (lss) {
                int    ten_minute = (ch - '0') * 10 * 60;
                time_t hour       = rounddown(lnav_data.ld_top_time +
                                              (60 * 60) -
                                              ten_minute +
                                              1,
                                              60 * 60);
                vis_line_t line = lss->find_from_time(hour + ten_minute);

                tc->set_top(line);
            }
            break;

        case '!':
            back_ten(1);
            break;

        case '@':
            back_ten(2);
            break;

        case '#':
            back_ten(3);
            break;

        case '$':
            back_ten(4);
            break;

        case '%':
            back_ten(5);
            break;

        case '^':
            back_ten(6);
            break;

        case '9':
            if (lss) {
                double tenth = ((double)tc->get_inner_height()) / 10.0;

                tc->shift_top(vis_line_t(tenth));
            }
            break;

        case '(':
            if (lss) {
                double tenth = ((double)tc->get_inner_height()) / 10.0;

                tc->shift_top(vis_line_t(-tenth));
            }
            break;

        case '0':
            if (lss) {
                time_t     first_time = lnav_data.ld_top_time;
                int        step       = 24 * 60 * 60;
                vis_line_t line       =
                        lss->find_from_time(roundup_size(first_time, step));

                tc->set_top(line);
            }
            break;

        case ')':
            if (lss) {
                time_t     day  = rounddown(lnav_data.ld_top_time, 24 * 60 * 60);
                vis_line_t line = lss->find_from_time(day);

                --line;
                tc->set_top(line);
            }
            break;

        case 'D':
        case 'O':
            if (tc->get_top() == 0) {
                alerter::singleton().chime();
            }
            else if (lss) {
                int        step     = ch == 'D' ? (24 * 60 * 60) : (60 * 60);
                time_t     top_time = lnav_data.ld_top_time;
                vis_line_t line     = lss->find_from_time(top_time - step);

                if (line != 0) {
                    --line;
                }
                tc->set_top(line);

                lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(/, "to search"));
            }
            break;

        case 'd':
        case 'o':
            if (lss) {
                int        step = ch == 'd' ? (24 * 60 * 60) : (60 * 60);
                vis_line_t line =
                        lss->find_from_time(lnav_data.ld_top_time + step);

                tc->set_top(line);

                lnav_data.ld_rl_view->set_alt_value(HELP_MSG_1(/, "to search"));
            }
            break;

        case ':':
            if (lnav_data.ld_views[LNV_LOG].get_inner_height() > 0) {
                logfile_sub_source &lss      = lnav_data.ld_log_source;
                textview_curses &   log_view = lnav_data.ld_views[LNV_LOG];
                content_line_t      cl       = lss.at(log_view.get_top());
                logfile *           lf       = lss.find(cl);
                logfile::iterator ll = lf->begin() + cl;
                log_data_helper ldh(lss);

                lnav_data.ld_rl_view->clear_possibilities(LNM_COMMAND, "colname");

                ldh.parse_line(log_view.get_top(), true);

                for (vector<string>::iterator iter = ldh.ldh_namer->cn_names.begin();
                     iter != ldh.ldh_namer->cn_names.end();
                     ++iter) {
                    lnav_data.ld_rl_view->add_possibility(LNM_COMMAND, "colname", *iter);
                }

                ldh.clear();

                lnav_data.ld_rl_view->clear_possibilities(LNM_COMMAND, "line-time");
                {
                    struct timeval tv = lf->get_time_offset();
                    char buffer[64];

                    sql_strftime(buffer, sizeof(buffer),
                                 ll->get_time(), ll->get_millis(), 'T');
                    lnav_data.ld_rl_view->add_possibility(LNM_COMMAND,
                                                          "line-time",
                                                          buffer);
                    sql_strftime(buffer, sizeof(buffer),
                                 ll->get_time() - tv.tv_sec,
                                 ll->get_millis() - (tv.tv_usec / 1000),
                                 'T');
                    lnav_data.ld_rl_view->add_possibility(LNM_COMMAND,
                                                          "line-time",
                                                          buffer);
                }
            }

            add_view_text_possibilities(LNM_COMMAND, "filter", tc);
            lnav_data.ld_rl_view->
                    add_possibility(LNM_COMMAND, "filter",
                                    lnav_data.ld_last_search[tc - lnav_data.ld_views]);
            add_filter_possibilities(tc);
            add_mark_possibilities();
            lnav_data.ld_mode = LNM_COMMAND;
            lnav_data.ld_rl_view->focus(LNM_COMMAND, ":");
            break;

        case '/':
            lnav_data.ld_mode = LNM_SEARCH;
            lnav_data.ld_previous_search = lnav_data.ld_last_search[tc - lnav_data.ld_views];
            lnav_data.ld_search_start_line = tc->get_top();
            add_view_text_possibilities(LNM_SEARCH, "*", tc);
            lnav_data.ld_rl_view->focus(LNM_SEARCH, "/");
            lnav_data.ld_bottom_source.set_prompt(
                    "Enter a regular expression to search for: "
                            "(Press " ANSI_BOLD("CTRL+]") " to abort)");
            break;

        case ';':
            if (tc == &lnav_data.ld_views[LNV_LOG] ||
                tc == &lnav_data.ld_views[LNV_DB] ||
                tc == &lnav_data.ld_views[LNV_SCHEMA]) {
                textview_curses &log_view = lnav_data.ld_views[LNV_LOG];

                lnav_data.ld_mode = LNM_SQL;
                setup_logline_table();
                lnav_data.ld_rl_view->focus(LNM_SQL, ";");

                lnav_data.ld_bottom_source.update_loading(0, 0);
                lnav_data.ld_status[LNS_BOTTOM].do_update();

                field_overlay_source *fos;

                fos = (field_overlay_source *)log_view.get_overlay_source();
                fos->fos_active_prev = fos->fos_active;
                if (!fos->fos_active) {
                    fos->fos_active = true;
                    tc->reload_data();
                }
                lnav_data.ld_bottom_source.set_prompt("Enter an SQL query: (Press "
                ANSI_BOLD("CTRL+]") " to abort)");
            }
            break;

        case 'p':
            field_overlay_source *fos;

            fos =
                    (field_overlay_source *)lnav_data.ld_views[LNV_LOG].
                            get_overlay_source();
            fos->fos_active = !fos->fos_active;
            tc->reload_data();
            break;

        case 'P':
            toggle_view(&lnav_data.ld_views[LNV_PRETTY]);
            break;

        case 't':
            if (lnav_data.ld_text_source.current_file() == NULL) {
                alerter::singleton().chime();
                lnav_data.ld_rl_view->set_value("No text files loaded");
            }
            else if (toggle_view(&lnav_data.ld_views[LNV_TEXT])) {
                lnav_data.ld_rl_view->set_alt_value(HELP_MSG_2(
                        f, F,
                        "to switch to the next/previous file"));
            }
            break;

        case 'T':
            lnav_data.ld_log_source.toggle_time_offset();
            if (lss && lss->is_time_offset_enabled()) {
                lnav_data.ld_rl_view->set_alt_value(
                        HELP_MSG_2(s, S, "to move forward/backward through slow downs"));
            }
            tc->reload_data();
            break;

        case 'i':
            if (toggle_view(&lnav_data.ld_views[LNV_HISTOGRAM])) {
                lnav_data.ld_rl_view->set_alt_value(
                        HELP_MSG_2(z, Z, "to zoom in/out"));
            }
            else {
                lnav_data.ld_rl_view->set_alt_value("");
            }
            break;

        case 'I':
        {
            time_t log_top = lnav_data.ld_top_time;

            if (toggle_view(&lnav_data.ld_views[LNV_HISTOGRAM])) {
                hist_source &hs = lnav_data.ld_hist_source;

                tc = lnav_data.ld_view_stack.top();
                tc->set_top(hs.row_for_value(log_top));
            }
            else {
                textview_curses &hist_tc = lnav_data.ld_views[LNV_HISTOGRAM];
                textview_curses &log_tc = lnav_data.ld_views[LNV_LOG];
                time_t hist_top =
                        lnav_data.ld_hist_source.value_for_row(hist_tc.get_top());
                lss = &lnav_data.ld_log_source;
                log_tc.set_top(lss->find_from_time(hist_top));
                log_tc.set_needs_update();
            }
        }
            break;

        case KEY_CTRL_G:
            toggle_view(&lnav_data.ld_views[LNV_GRAPH]);
            break;

        case '?':
            toggle_view(&lnav_data.ld_views[LNV_HELP]);
            break;

        case 'v':
            toggle_view(&lnav_data.ld_views[LNV_DB]);
            break;

        case 'V':
        {
            textview_curses *db_tc = &lnav_data.ld_views[LNV_DB];
            db_label_source &dls   = lnav_data.ld_db_rows;
            hist_source &    hs    = lnav_data.ld_db_source;

            if (toggle_view(db_tc)) {
                unsigned int lpc;

                for (lpc = 0; lpc < dls.dls_headers.size(); lpc++) {
                    if (dls.dls_headers[lpc] != "log_line") {
                        continue;
                    }

                    char         linestr[64];
                    int          line_number = (int)tc->get_top();
                    unsigned int row;

                    snprintf(linestr, sizeof(linestr), "%d", line_number);
                    for (row = 0; row < dls.dls_rows.size(); row++) {
                        if (strcmp(dls.dls_rows[row][lpc],
                                   linestr) == 0) {
                            vis_line_t db_line(hs.row_for_value(row));

                            db_tc->set_top(db_line);
                            db_tc->set_needs_update();
                            break;
                        }
                    }
                    break;
                }
            }
            else {
                int          db_row = hs.value_for_row(db_tc->get_top());
                unsigned int lpc;

                for (lpc = 0; lpc < dls.dls_headers.size(); lpc++) {
                    if (dls.dls_headers[lpc] != "log_line") {
                        continue;
                    }

                    unsigned int line_number;

                    tc = &lnav_data.ld_views[LNV_LOG];
                    if (sscanf(dls.dls_rows[db_row][lpc],
                               "%d",
                               &line_number) &&
                        line_number < tc->listview_rows(*tc)) {
                        tc->set_top(vis_line_t(line_number));
                        tc->set_needs_update();
                    }
                    break;
                }
            }
        }
            break;

        case '\t':
            if (tc == &lnav_data.ld_views[LNV_DB])
            {
                hist_source &hs = lnav_data.ld_db_source;
                db_label_source &dls   = lnav_data.ld_db_rows;
                std::vector<bucket_type_t> &displayed = hs.get_displayed_buckets();
                std::vector<int>::iterator start_iter, iter;

                start_iter = dls.dls_headers_to_graph.begin();
                if (!displayed.empty()) {
                    advance(start_iter, (int)displayed[0] + 1);
                }
                displayed.clear();
                iter = find(start_iter,
                            dls.dls_headers_to_graph.end(),
                            true);
                if (iter != dls.dls_headers_to_graph.end()) {
                    bucket_type_t type;

                    type = bucket_type_t(distance(dls.dls_headers_to_graph.begin(), iter));
                    displayed.push_back(type);
                }
                if (displayed.empty()) {
                    lnav_data.ld_rl_view->set_value("Graphing all values");
                }
                else {
                    int index = displayed[0];

                    lnav_data.ld_rl_view->set_value("Graphing column " ANSI_BOLD_START +
                                                                       dls.dls_headers[index] + ANSI_NORM);
                }
                tc->reload_data();
            }
            break;

            // XXX I'm sure there must be a better way to handle the difference between
            // iterator and reverse_iterator.
        case KEY_BTAB:
            if (tc == &lnav_data.ld_views[LNV_DB])
            {
                hist_source &hs = lnav_data.ld_db_source;
                db_label_source &dls   = lnav_data.ld_db_rows;
                std::vector<bucket_type_t> &displayed = hs.get_displayed_buckets();
                std::vector<int>::reverse_iterator start_iter, iter;

                start_iter = dls.dls_headers_to_graph.rbegin();
                if (!displayed.empty()) {
                    advance(start_iter, dls.dls_headers_to_graph.size() - (int)displayed[0]);
                }
                displayed.clear();
                iter = find(start_iter,
                            dls.dls_headers_to_graph.rend(),
                            true);
                if (iter != dls.dls_headers_to_graph.rend()) {
                    bucket_type_t type;

                    type = bucket_type_t(distance(dls.dls_headers_to_graph.begin(), --iter.base()));
                    displayed.push_back(type);
                }
                tc->reload_data();
            }
            break;

        case 'X':
            lnav_data.ld_rl_view->set_value(execute_command("close"));
            break;

        case '\\':
        {
            vis_bookmarks &bm = tc->get_bookmarks();
            string         ex;

            for (bookmark_vector<vis_line_t>::iterator iter =
                    bm[&BM_EXAMPLE].begin();
                 iter != bm[&BM_EXAMPLE].end();
                 ++iter) {
                string line;

                tc->get_sub_source()->text_value_for_line(*tc, *iter, line);
                ex += line + "\n";
            }
            lnav_data.ld_views[LNV_EXAMPLE].set_sub_source(new plain_text_source(
                    ex));
            ensure_view(&lnav_data.ld_views[LNV_EXAMPLE]);
        }
            break;

        case 'r':
        case 'R':
            if (lss) {
                if (lnav_data.ld_last_relative_time.empty()) {
                    lnav_data.ld_rl_view->set_value(
                            "Use the 'goto' command to set the relative time to move by");
                }
                else {
                    vis_line_t vl = tc->get_top();
                    relative_time rt = lnav_data.ld_last_relative_time;
                    struct timeval tv;
                    content_line_t cl;
                    struct exttm tm;

                    if (ch == 'R') {
                        rt.negate();
                    }

                    cl = lnav_data.ld_log_source.at(vl);
                    logline *ll = lnav_data.ld_log_source.find_line(cl);
                    ll->to_exttm(tm);
                    rt.add(tm);
                    tv.tv_sec = timegm(&tm.et_tm);
                    tv.tv_usec = tm.et_nsec / 1000;
                    vl = lnav_data.ld_log_source.find_from_time(tv);
                    if (rt.is_negative() && (vl > vis_line_t(0))) {
                        --vl;
                        if (vl == tc->get_top()) {
                            vl = vis_line_t(0);
                        }
                    }
                    tc->set_top(vl);
                }
            }
            break;

        case KEY_CTRL_R:
            reset_session();
            rebuild_indexes(true);
            break;

        case KEY_CTRL_W:
            execute_command(lnav_data.ld_views[LNV_LOG].get_word_wrap() ?
                            "disable-word-wrap" : "enable-word-wrap");
            break;

        default:
            log_warning("unhandled %d", ch);
            lnav_data.ld_rl_view->set_value("Unrecognized keystroke, press "
            ANSI_BOLD("?")
            " to view help");
            alerter::singleton().chime();
            break;
    }
}
