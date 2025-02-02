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
#include <vector>

#include <termios.h>

#include "base/auto_fd.hh"
#include "base/injector.bind.hh"
#include "base/injector.hh"
#include "base/itertools.hh"
#include "base/lnav.console.hh"
#include "command_executor.hh"
#include "itertools.similar.hh"
#include "lnav_config.hh"
#include "sql_util.hh"
#include "sqlitepp.hh"
#include "termios_guard.hh"
#include "textfile_highlighters.hh"
#include "textinput_curses.hh"
#include "xterm_mouse.hh"

using namespace std::chrono_literals;

constexpr char EMPTY[] = "";

constexpr char LOREM[]
    = R"(Lorem ipsum dolor sit amet, consectetur adipiscing elit,
sed do eiusmod tempor incididunt ut labore et dolore
magna aliqua. Ut enim ad minim veniam, quis nostrud
exercitation ullamco laboris nisi ut aliquip ex ea
commodo consequat.
)";

constexpr char SQL1_CONTENT[] = R"(SELECT * FROM access_log
  WHERE cs_uri_stem LIKE '%foo%'
)";

constexpr char MD1_CONTENT[] = R"(
# Markdown test

A list:
- abc
- def

Steps to reproduce:
1. one
2. two
3. three

This is **bold** and this is *italic*.

So-and-so said:

> Hello, World!
> Goodbye, World!

)";

static auto bound_xterm_mouse = injector::bind<xterm_mouse>::to_singleton();

struct drive_textinput_data_t {
    auto_sqlite3 dtd_db;
    exec_context dtd_exec_context;
} drive_textinput_data;

static auto bound_lnav_exec_context = injector::bind<exec_context>::to_instance(
    &drive_textinput_data.dtd_exec_context);

static auto bound_sqlite_db
    = injector::bind<auto_sqlite3>::to_instance(&drive_textinput_data.dtd_db);

static const std::map<std::string, std::pair<text_format_t, const char*>>
    CONTENT_MAP = {
        {"empty", {text_format_t::TF_UNKNOWN, EMPTY}},
        {"lorem", {text_format_t::TF_UNKNOWN, LOREM}},
        {"sql1", {text_format_t::TF_SQL, SQL1_CONTENT}},
        {"md1", {text_format_t::TF_MARKDOWN, MD1_CONTENT}},
};

class drive_behavior : public mouse_behavior {
public:
    void mouse_event(
        notcurses* nc, int button, bool release, int x, int y) override
    {
        static const auto CLICK_INTERVAL = 333ms;

        struct mouse_event me;

        switch (button & xterm_mouse::XT_BUTTON__MASK) {
            case xterm_mouse::XT_BUTTON1:
                me.me_button = mouse_button_t::BUTTON_LEFT;
                break;
            case xterm_mouse::XT_BUTTON2:
                me.me_button = mouse_button_t::BUTTON_MIDDLE;
                break;
            case xterm_mouse::XT_BUTTON3:
                me.me_button = mouse_button_t::BUTTON_RIGHT;
                break;
            case xterm_mouse::XT_SCROLL_UP:
                me.me_button = mouse_button_t::BUTTON_SCROLL_UP;
                break;
            case xterm_mouse::XT_SCROLL_DOWN:
                me.me_button = mouse_button_t::BUTTON_SCROLL_DOWN;
                break;
        }

        gettimeofday(&me.me_time, nullptr);
        me.me_modifiers = button & xterm_mouse::XT_MODIFIER_MASK;

        if (release
            && (to_mstime(me.me_time)
                - to_mstime(this->db_last_release_event.me_time))
                < CLICK_INTERVAL.count())
        {
            me.me_state = mouse_button_state_t::BUTTON_STATE_DOUBLE_CLICK;
        } else if (button & xterm_mouse::XT_DRAG_FLAG) {
            me.me_state = mouse_button_state_t::BUTTON_STATE_DRAGGED;
        } else if (release) {
            me.me_state = mouse_button_state_t::BUTTON_STATE_RELEASED;
        } else {
            me.me_state = mouse_button_state_t::BUTTON_STATE_PRESSED;
        }

        auto width = ncplane_dim_x(this->db_window);

        me.me_x = x;
        if (me.me_x >= width) {
            me.me_x = width - 1;
        }
        me.me_y = y - 1;
        if (me.me_state == mouse_button_state_t::BUTTON_STATE_PRESSED) {
            me.me_press_x = me.me_x;
            me.me_press_y = me.me_y;
        } else {
            me.me_press_x = this->db_last_event.me_press_x;
            me.me_press_y = this->db_last_event.me_press_y;
        }

        switch (me.me_state) {
            case mouse_button_state_t::BUTTON_STATE_PRESSED:
            case mouse_button_state_t::BUTTON_STATE_DOUBLE_CLICK: {
                if (this->db_input->contains(me.me_x, me.me_y)) {
                    me.me_press_y = me.me_y - this->db_input->get_y();
                    me.me_press_x = me.me_x - this->db_input->get_x();
                    this->db_input->handle_mouse(me);
                }
                break;
            }
            case mouse_button_state_t::BUTTON_STATE_DRAGGED:
                break;
            case mouse_button_state_t::BUTTON_STATE_RELEASED: {
                this->db_last_release_event = me;
                break;
            }
        }

        me.me_y -= this->db_input->get_y();
        me.me_x -= this->db_input->get_x();
        this->db_input->handle_mouse(me);

        this->db_last_event = me;
    }

    ncplane* db_window;
    textinput_curses* db_input;
    struct mouse_event db_last_event;
    struct mouse_event db_last_release_event;
};

int
main(int argc, char** argv)
{
    std::string content = "lorem";
    int c;
    int x = 0;
    int y = 0;
    int width = 40;
    int height = 10;

    setenv("DUMP_CRASH", "1", 1);
    setlocale(LC_ALL, "");
    log_install_handlers();
    lnav_log_crash_dir = "/tmp";
    lnav_log_file = fopen("/tmp/drive_textinput.log", "w+");

    auto_fd errpipe[2];
    auto_fd::pipe(errpipe);

    errpipe[0].close_on_exec();
    errpipe[1].close_on_exec();
    auto pipe_err_handle
        = log_pipe_err(errpipe[0].release(), errpipe[1].release());

    while ((c = getopt(argc, argv, "x:y:h:w:c:")) != -1) {
        switch (c) {
            case 'x':
                x = atoi(optarg);
                break;
            case 'y':
                y = atoi(optarg);
                break;
            case 'h':
                height = atoi(optarg);
                break;
            case 'w':
                width = atoi(optarg);
                break;
            case 'c':
                content = std::string(optarg);
                if (CONTENT_MAP.count(content) == 0) {
                    fprintf(stderr, "content not found: %s\n", content.c_str());
                    return EXIT_FAILURE;
                }
                break;
        }
    }

    if (sqlite3_open(":memory:", drive_textinput_data.dtd_db.out())
        != SQLITE_OK)
    {
        fprintf(stderr, "error: unable to create sqlite memory database\n");
        exit(EXIT_FAILURE);
    }

    std::vector<lnav::console::user_message> errors;
    load_config({}, errors);

    std::string new_content;

    guard_termios gt(STDIN_FILENO);
    {
#ifdef VDSUSP
        {
            struct termios tio;

            tcgetattr(STDIN_FILENO, &tio);
            tio.c_cc[VDSUSP] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &tio);
        }
#endif

        auto nco = notcurses_options{};
        nco.flags |= NCOPTION_SUPPRESS_BANNERS;
        nco.loglevel = NCLOGLEVEL_INFO;
        auto sc = screen_curses::create(nco).unwrap();
        view_colors::singleton().init(sc.get_notcurses());
        auto looping = true;

        textinput_curses tc;
        tc.set_x(x);
        tc.set_y(y);
        tc.set_width(width);
        tc.tc_height = height;
        tc.tc_window = sc.get_std_plane();
        setup_highlights(tc.tc_highlights);
        {
            auto iter = CONTENT_MAP.find(content);

            tc.tc_text_format = iter->second.first;
            tc.set_content(iter->second.second);
        }
        tc.tc_on_abort = [&looping](auto& tc) { looping = false; };
        tc.tc_on_change = [](textinput_curses& tc) {
            auto& al = tc.tc_lines[tc.tc_cursor_y];
            auto word_start_col = string_fragment::from_str(al.al_string)
                                      .prev_word(tc.tc_cursor_x);
            auto word_start_index
                = al.column_to_byte_index(word_start_col.value_or(0));
            auto word_end_index = al.column_to_byte_index(tc.tc_cursor_x);
            auto prefix = al.subline(word_start_index,
                                     word_end_index - word_start_index);
            log_debug("prefix %s", prefix.al_string.c_str());
            if (prefix.empty()) {
                return;
            }

            auto poss
                = sql_keywords | lnav::itertools::similar_to(prefix.al_string);
            tc.open_popup_for_completion(
                word_start_col.value_or(0),
                poss | lnav::itertools::map([](const auto& s) {
                    return attr_line_t(s);
                }));
            log_debug("poss %d", poss.size());
        };
        tc.tc_on_completion = [](textinput_curses& tc) {
            auto& al = tc.tc_lines[tc.tc_cursor_y];
            auto word_start_col = string_fragment::from_str(al.al_string)
                                      .prev_word(tc.tc_cursor_x);
            auto word_start_index
                = al.column_to_byte_index(word_start_col.value_or(0));
            auto word_end_index = al.column_to_byte_index(tc.tc_cursor_x);
            const auto& repl
                = tc.tc_popup_source.get_lines()[tc.tc_popup.get_selection()]
                      .tl_value.al_string;
            al.erase(word_start_index, word_end_index - word_start_index);
            al.insert(word_start_index, repl);
            tc.tc_cursor_x = word_start_col.value_or(0)
                + string_fragment::from_str(repl).column_width();
            tc.update_lines();
        };

        auto& mouse_i = injector::get<xterm_mouse&>();
        drive_behavior db;
        db.db_window = sc.get_std_plane();
        db.db_input = &tc;
        mouse_i.set_behavior(&db);
        mouse_i.set_enabled(sc.get_notcurses(), true);

        while (looping) {
            tc.do_update();
            log_debug("doing render");
            notcurses_render(sc.get_notcurses());
            tc.focus();

            log_debug("waiting for input");
            ncinput nci;
            notcurses_get_blocking(sc.get_notcurses(), &nci);
            if (ncinput_ctrl_p(&nci) && nci.id == 'X') {
                looping = false;
                continue;
            }
            log_debug("got input shift=%d alt=%d ctrl=%d",
                      ncinput_shift_p(&nci),
                      ncinput_alt_p(&nci),
                      ncinput_ctrl_p(&nci));
            if (ncinput_mouse_p(&nci)) {
                mouse_i.handle_mouse(sc.get_notcurses(), nci);
            } else {
                tc.handle_key(nci);
            }
        }

        new_content = tc.get_content();
    }

    printf("%s", new_content.c_str());
}
