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

#include <vector>

#include <termios.h>

#include "base/auto_fd.hh"
#include "base/injector.bind.hh"
#include "base/lnav.console.hh"
#include "command_executor.hh"
#include "lnav_config.hh"
#include "sqlitepp.hh"
#include "termios_guard.hh"
#include "textinput_curses.hh"
#include "xterm_mouse.hh"

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

        textinput_curses tc;
        tc.set_x(x);
        tc.set_y(y);
        tc.set_width(width);
        tc.tc_height = height;
        tc.tc_window = sc.get_std_plane();
        {
            auto iter = CONTENT_MAP.find(content);

            tc.tc_text_format = iter->second.first;
            tc.set_content(iter->second.second);
        }

        while (true) {
            tc.do_update();
            log_debug("doing render");
            notcurses_render(sc.get_notcurses());
            tc.focus();

            log_debug("waiting for input");
            ncinput nci;
            notcurses_get_blocking(sc.get_notcurses(), &nci);
            log_debug("got input %d %c", ncinput_ctrl_p(&nci), nci.id);
            if (ncinput_ctrl_p(&nci) && nci.id == 'X') {
                break;
            }
            log_debug("got input");
            tc.handle_key(nci);
        }
    }
}
