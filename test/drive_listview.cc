/**
 * Copyright (c) 2007-2012, Timothy Stack
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "base/injector.bind.hh"
#include "config.h"
#include "listview_curses.hh"
#include "xterm_mouse.hh"

using namespace std;

static listview_curses lv;

static auto bound_xterm_mouse = injector::bind<xterm_mouse>::to_singleton();


class my_source : public list_data_source {
public:
    my_source() : ms_rows(2) {}

    size_t listview_rows(const listview_curses& lv) { return this->ms_rows; }

    void listview_value_for_rows(const listview_curses& lv,
                                 vis_line_t row,
                                 vector<attr_line_t>& rows)
    {
        for (auto& value_out : rows) {
            value_out = (lv.is_selectable() && row == lv.get_selection()) ? "+"
                                                                          : "";

            if (row == 0) {
                value_out.al_string += " Hello";
                value_out.with_attr(string_attr(line_range{1, 3},
                    VC_STYLE.value(text_attrs::with_bold())));
            } else if (row == 1) {
                auto mixed_style = text_attrs::with_italic();
                mixed_style.ta_fg_color = rgb_color{255, 0, 0};
                mixed_style.ta_bg_color = rgb_color{0, 255, 0};
                mixed_style.ta_bg_color = palette_color{COLOR_GREEN};
                if (mixed_style.ta_bg_color.cu_value.is<palette_color>()) {
                    log_debug("wtf!");
                }
                value_out.al_string += "World!";
                value_out.with_attr(string_attr(line_range{1, 3},
                    VC_STYLE.value(mixed_style)));
            } else if (row < this->ms_rows) {
                value_out.al_string += std::to_string(static_cast<int>(row));
            } else {
                assert(0);
            }
            ++row;
        }
    }

    size_t listview_size_for_row(const listview_curses& lv, vis_line_t row)
    {
        return 100;
    }

    bool attrline_next_token(const view_curses& vc,
                             int line,
                             struct line_range& lr,
                             int& attrs_out)
    {
        return false;
    }

    int ms_rows;
};

int
main(int argc, char* argv[])
{
    int c, retval = EXIT_SUCCESS;
    bool wait_for_input = false, set_height = false;
    const char* keys = nullptr;
    my_source ms;

    setenv("DUMP_CRASH", "1", 1);
    setlocale(LC_ALL, "");
    log_install_handlers();
    lnav_log_crash_dir = "/tmp";
    lnav_log_file = fopen("/tmp/drive_listview.log", "w+");

    auto_fd errpipe[2];
    auto_fd::pipe(errpipe);

    errpipe[0].close_on_exec();
    errpipe[1].close_on_exec();
    auto pipe_err_handle
        = log_pipe_err(errpipe[0].release(), errpipe[1].release());

    auto nco = notcurses_options{};
    nco.flags |= NCOPTION_SUPPRESS_BANNERS;
    nco.loglevel = NCLOGLEVEL_DEBUG;
    auto sc = screen_curses::create(nco).unwrap();
    lv.set_data_source(&ms);
    lv.set_window(sc.get_std_plane());

    while ((c = getopt(argc, argv, "cy:t:k:l:r:h:w")) != -1) {
        switch (c) {
            case 'c':
                // Enable cursor mode
                lv.set_selectable(true);
                break;
            case 'y':
                lv.set_y(atoi(optarg));
                break;
            case 'h':
                lv.set_height(vis_line_t(atoi(optarg)));
                set_height = true;
                break;
            case 'k':
                keys = optarg;
                break;
            case 't':
                lv.set_selection(vis_line_t(atoi(optarg)));
                break;
            case 'l':
                lv.set_left(atoi(optarg));
                break;
            case 'w':
                wait_for_input = true;
                break;
            case 'r':
                ms.ms_rows = atoi(optarg);
                break;
        }
    }

    if (!set_height) {
        auto height = ncplane_dim_y(sc.get_std_plane());
        lv.set_height(vis_line_t(height - lv.get_y()));
    }

    ncinput nci;
    if (keys != nullptr) {
        // Treats the string argument as a sequence of key presses (only
        // individual characters supported as key input)
        for (const char* ptr = keys; ptr != nullptr && *ptr != '\0'; ++ptr) {
            lv.do_update();
            if (wait_for_input) {
                notcurses_render(sc.get_notcurses());
                notcurses_get_blocking(sc.get_notcurses(), &nci);
            }
            ncinput nci;
            nci.id = static_cast<uint32_t>(*ptr);
            nci.eff_text[0] = *ptr;
            nci.eff_text[1] = '\0';
            lv.handle_key(nci);
        }
    }

    lv.do_update();
    notcurses_render(sc.get_notcurses());
    if (wait_for_input) {
        notcurses_get_blocking(sc.get_notcurses(), &nci);
    }

    return retval;
}
