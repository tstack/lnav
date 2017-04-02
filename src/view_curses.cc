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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file view_curses.cc
 */

#include "config.h"

#include <string>
#include <algorithm>

#include "auto_mem.hh"
#include "lnav_log.hh"
#include "view_curses.hh"
#include "ansi_scrubber.hh"
#include "lnav_config.hh"
#include "attr_line.hh"

using namespace std;

string_attr_type view_curses::VC_STYLE("style");
string_attr_type view_curses::VC_GRAPHIC("graphic");

const struct itimerval ui_periodic_timer::INTERVAL = {
    { 0, 350 * 1000 },
    { 0, 350 * 1000 }
};

ui_periodic_timer::ui_periodic_timer()
        : upt_counter(0)
{
    struct sigaction sa;

    sa.sa_handler = ui_periodic_timer::sigalrm;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    if (setitimer(ITIMER_REAL, &INTERVAL, NULL) == -1) {
        perror("setitimer");
    }
}

ui_periodic_timer &ui_periodic_timer::singleton()
{
    static ui_periodic_timer retval;

    return retval;
}

void ui_periodic_timer::sigalrm(int sig)
{
    singleton().upt_counter += 1;
}

alerter &alerter::singleton() {
    static alerter retval;

    return retval;
}

attr_line_t &attr_line_t::with_ansi_string(const char *str, ...)
{
    auto_mem<char> formatted_str;
    va_list args;

    va_start(args, str);
    vasprintf(formatted_str.out(), str, args);
    va_end(args);

    if (formatted_str != NULL) {
        this->al_string = formatted_str;
        scrub_ansi_string(this->al_string, this->al_attrs);
    }

    return *this;
}

attr_line_t &attr_line_t::append(const attr_line_t &al, text_wrap_settings *tws)
{
    size_t start_len = this->al_string.length();

    this->al_string.append(al.al_string);

    for (auto &sa : al.al_attrs) {
        this->al_attrs.emplace_back(sa);

        line_range &lr = this->al_attrs.back().sa_range;

        lr.shift(0, start_len);
        if (lr.lr_end == -1) {
            lr.lr_end = this->al_string.length();
        }
    }

    if (tws != nullptr && this->al_string.length() > tws->tws_width) {
        ssize_t start_pos = start_len;
        ssize_t line_start = this->al_string.rfind('\n', start_pos);

        if (line_start == string::npos) {
            line_start = 0;
        } else {
            line_start += 1;
        }

        ssize_t line_len = start_len - line_start;
        ssize_t usable_width = tws->tws_width - tws->tws_indent;
        ssize_t avail = max((ssize_t) 0, (ssize_t) tws->tws_width - line_len);

        while (start_pos < this->al_string.length()) {
            ssize_t lpc;

            // Find the end of a word or a breakpoint.
            for (lpc = start_pos;
                 lpc < this->al_string.length() &&
                 (isalnum(this->al_string[lpc]) ||
                  this->al_string[lpc] == ',' ||
                  this->al_string[lpc] == '_' ||
                  this->al_string[lpc] == '.' ||
                  this->al_string[lpc] == ';');
                 lpc++) {
                if (this->al_string[lpc] == '-' ||
                    this->al_string[lpc] == '.') {
                    lpc += 1;
                    break;
                }
            }

            if (lpc - start_pos > avail) {
                // Need to wrap the word.  Do the wrap.
                this->insert(start_pos, 1, '\n')
                    .insert(start_pos + 1, tws->tws_indent, ' ');
                start_pos += 1 + tws->tws_indent;
                avail = tws->tws_width - tws->tws_indent;
            } else {
                // There's still room to add stuff.
                avail -= (lpc - start_pos);
                while (lpc < this->al_string.length() && avail) {
                    if (this->al_string[lpc] == '\n') {
                        this->insert(lpc + 1, tws->tws_indent, ' ');
                        avail = usable_width;
                        lpc += 1 + tws->tws_indent;
                        break;
                    }
                    if (isalnum(this->al_string[lpc]) ||
                        this->al_string[lpc] == '_') {
                        break;
                    }
                    avail -= 1;
                    lpc += 1;
                }
                start_pos = lpc;
                if (!avail) {
                    this->insert(start_pos, 1, '\n')
                        .insert(start_pos + 1, tws->tws_indent, ' ');
                    start_pos += 1 + tws->tws_indent;
                    avail = usable_width;

                    for (lpc = start_pos;
                         lpc < this->al_string.length() &&
                         this->al_string[lpc] == ' ';
                         lpc++) {
                    }

                    if (lpc != start_pos) {
                        this->erase(start_pos, (lpc - start_pos));
                    }
                }
            }
        }
    }

    return *this;
}

attr_line_t attr_line_t::subline(size_t start, size_t len) const
{
    line_range lr{(int) start, (int) (start + len)};
    attr_line_t retval;

    retval.al_string = this->al_string.substr(start, len);
    for (auto &sa : this->al_attrs) {
        if (!lr.intersects(sa.sa_range)) {
            continue;
        }

        retval.al_attrs.emplace_back(lr.intersection(sa.sa_range)
                                       .shift(lr.lr_start, -lr.lr_start),
                                     sa.sa_type,
                                     sa.sa_value);
    }

    return retval;
}

void attr_line_t::split_lines(std::vector<attr_line_t> &lines) const
{
    size_t pos = 0, next_line;

    while ((next_line = this->al_string.find('\n', pos)) != std::string::npos) {
        lines.emplace_back(this->subline(pos, next_line - pos));
        pos = next_line + 1;
    }
    lines.emplace_back(this->subline(pos));
}

void view_curses::mvwattrline(WINDOW *window,
                              int y,
                              int x,
                              attr_line_t &al,
                              const struct line_range &lr,
                              view_colors::role_t base_role)
{
    int text_attrs, attrs, line_width;
    string_attrs_t &         sa   = al.get_attrs();
    string &                 line = al.get_string();
    string_attrs_t::iterator iter;
    std::map<size_t, size_t, std::greater<size_t> > tab_list;
    int    tab_count = 0;
    char  *expanded_line;
    size_t exp_index = 0;
    string full_line;

    require(lr.lr_end >= 0);

    line_width    = lr.length();
    tab_count     = count(line.begin(), line.end(), '\t');
    expanded_line = (char *)alloca(line.size() + tab_count * 8 + 1);

    for (size_t lpc = 0; lpc < line.size(); lpc++) {
        switch (line[lpc]) {
        case '\t':
            do {
                expanded_line[exp_index] = ' ';
                exp_index += 1;
            } while (exp_index % 8);
            tab_list[lpc] = exp_index;
            break;

        case '\r':
            /* exp_index = -1; */
            break;

        case '\n':
            expanded_line[exp_index] = ' ';
            exp_index += 1;
            break;

        default:
            expanded_line[exp_index] = line[lpc];
            exp_index += 1;
            break;
        }
    }

    expanded_line[exp_index] = '\0';
    full_line = string(expanded_line);

    text_attrs = view_colors::singleton().attrs_for_role(base_role);
    attrs      = text_attrs;
    wmove(window, y, x);
    wattron(window, attrs);
    if (lr.lr_start < (int)full_line.size()) {
        waddnstr(window, &full_line.c_str()[lr.lr_start], line_width);
    }
    if (lr.lr_end > (int)full_line.size()) {
        whline(window, ' ', lr.lr_end - full_line.size());
    }
    wattroff(window, attrs);

    std::vector<line_range> graphic_range;
    std::vector<int>        graphic_in;

    stable_sort(sa.begin(), sa.end());
    for (iter = sa.begin(); iter != sa.end(); ++iter) {
        struct line_range attr_range = iter->sa_range;
        std::map<size_t, size_t>::iterator tab_iter;

        require(attr_range.lr_start >= 0);
        require(attr_range.lr_end >= -1);

        tab_iter = tab_list.lower_bound(attr_range.lr_start);
        if (tab_iter != tab_list.end()) {
            if ((size_t)attr_range.lr_start > tab_iter->first) {
                attr_range.lr_start += (tab_iter->second - tab_iter->first) - 1;
            }
        }

        if (attr_range.lr_end != -1) {
            tab_iter = tab_list.lower_bound(attr_range.lr_end);
            if (tab_iter != tab_list.end()) {
                if ((size_t)attr_range.lr_end > tab_iter->first) {
                    attr_range.lr_end += (
                        tab_iter->second - tab_iter->first) - 1;
                }
            }
        }

        attr_range.lr_start = max(0, attr_range.lr_start - lr.lr_start);
        if (attr_range.lr_end == -1) {
            attr_range.lr_end = lr.lr_start + line_width;
        }

        attr_range.lr_end = min((int)line_width,
            attr_range.lr_end - lr.lr_start);

        if (attr_range.lr_end > attr_range.lr_start) {
            string_attrs_t::iterator range_iter;
            int awidth = attr_range.length();
            int color_pair = -1;

            attrs = 0;
            for (range_iter = iter;
                 range_iter != sa.end() && range_iter->sa_range == iter->sa_range;
                 ++range_iter) {
                if (range_iter->sa_type == &VC_STYLE) {
                    if (color_pair <= 0) {
                        color_pair = PAIR_NUMBER(range_iter->sa_value.sav_int);
                    }
                    attrs |= range_iter->sa_value.sav_int;
                }
            }

            if (attrs != 0) {
                int x_pos = x + attr_range.lr_start;
                int ch_width = min(awidth, (line_width - attr_range.lr_start));
                chtype row_ch[ch_width + 1];

                mvwinchnstr(window, y, x_pos, row_ch, ch_width);
                for (int lpc = 0; lpc < ch_width; lpc++) {
                    if (color_pair > 0) {
                        row_ch[lpc] &= ~A_COLOR;
                        row_ch[lpc] |= (attrs & ~A_COLOR) | COLOR_PAIR(color_pair);
                    } else {
                        row_ch[lpc] |= (attrs);
                    }
                }
                mvwaddchnstr(window, y, x_pos, row_ch, ch_width);
            }
            for (range_iter = iter;
                 range_iter != sa.end() && range_iter->sa_range == iter->sa_range;
                 ++range_iter) {
                if (range_iter->sa_type == &VC_GRAPHIC) {
                    graphic_range.push_back(attr_range);
                    graphic_in.push_back(range_iter->sa_value.sav_int | attrs);
                }
            }
        }
    }

    for (size_t lpc = 0; lpc < graphic_range.size(); lpc++) {
        for (int lpc2 = graphic_range[lpc].lr_start;
             lpc2 < graphic_range[lpc].lr_end;
             lpc2++) {
            mvwaddch(window, y, x + lpc2, graphic_in[lpc]);
        }
    }
}

class color_listener : public lnav_config_listener {
    void reload_config() {
        view_colors::singleton().init_roles();
    }
};

static color_listener _COLOR_LISTENER;

int view_colors::BASIC_HL_PAIRS[view_colors::BASIC_COLOR_COUNT] = {
    ansi_color_pair(COLOR_BLUE, COLOR_BLACK),
    ansi_color_pair(COLOR_CYAN, COLOR_BLACK),
    ansi_color_pair(COLOR_GREEN, COLOR_BLACK),
    ansi_color_pair(COLOR_MAGENTA, COLOR_BLACK),
    ansi_color_pair(COLOR_BLUE, COLOR_WHITE),
    ansi_color_pair(COLOR_CYAN, COLOR_BLACK),
    ansi_color_pair(COLOR_GREEN, COLOR_WHITE),
    ansi_color_pair(COLOR_MAGENTA, COLOR_WHITE),
};

view_colors &view_colors::singleton(void)
{
    static view_colors s_vc;

    return s_vc;
}

view_colors::view_colors()
{
}

bool view_colors::initialized = false;

void view_colors::init(void)
{
    if (has_colors()) {
        static int ansi_colors_to_curses[] = {
            COLOR_BLACK,
            COLOR_RED,
            COLOR_GREEN,
            COLOR_YELLOW,
            COLOR_BLUE,
            COLOR_MAGENTA,
            COLOR_CYAN,
            COLOR_WHITE,
        };

        start_color();

        /* use_default_colors(); */
        for (int fg = 0; fg < 8; fg++) {
            for (int bg = 0; bg < 8; bg++) {
                if (fg == 0 && bg == 0)
                    continue;
                init_pair(ansi_color_pair_index(fg, bg),
                          ansi_colors_to_curses[fg],
                          ansi_colors_to_curses[bg]);
            }
        }

        if (COLORS == 256) {
            int color_pair_base = VC_ANSI_END;

            for (int z = 0; z < 6; z++) {
                for (int x = 1; x < 6; x += 2) {
                    for (int y = 1; y < 6; y += 2) {
                        int fg = 16 + x + (y * 6) + (z * 6 * 6);

                        init_pair(color_pair_base, fg, COLOR_BLACK);
                        color_pair_base += 1;
                    }
                }
            }
        }
    }

    singleton().init_roles();

    initialized = true;
}

void view_colors::init_roles(void)
{
    /* Setup the mappings from roles to actual colors. */
    this->vc_role_colors[VCR_TEXT]   =
        ansi_color_pair(COLOR_WHITE, COLOR_BLACK);
    if (lnav_config.lc_ui_dim_text) {
        this->vc_role_colors[VCR_TEXT] |= A_DIM;
    }
    this->vc_role_colors[VCR_SEARCH] =
        this->vc_role_colors[VCR_TEXT] | A_REVERSE;
    this->vc_role_colors[VCR_OK]      = ansi_color_pair(COLOR_GREEN, COLOR_BLACK) | A_BOLD;
    this->vc_role_colors[VCR_ERROR]   = ansi_color_pair(COLOR_RED, COLOR_BLACK) | A_BOLD;
    this->vc_role_colors[VCR_WARNING] = ansi_color_pair(COLOR_YELLOW, COLOR_BLACK) | A_BOLD;
    this->vc_role_colors[VCR_ALT_ROW] = ansi_color_pair(COLOR_WHITE, COLOR_BLACK) | A_BOLD;
    this->vc_role_colors[VCR_ADJUSTED_TIME] = ansi_color_pair(COLOR_MAGENTA, COLOR_BLACK);
    this->vc_role_colors[VCR_SKEWED_TIME] = ansi_color_pair(COLOR_YELLOW, COLOR_BLACK) | A_UNDERLINE;

    this->vc_role_colors[VCR_STATUS] =
        ansi_color_pair(COLOR_BLACK, COLOR_WHITE);
    this->vc_role_colors[VCR_WARN_STATUS] =
        ansi_color_pair(COLOR_YELLOW, COLOR_WHITE) | A_BOLD;
    this->vc_role_colors[VCR_ALERT_STATUS] =
        ansi_color_pair(COLOR_RED, COLOR_WHITE) | A_BOLD;
    this->vc_role_colors[VCR_ACTIVE_STATUS] =
        ansi_color_pair(COLOR_GREEN, COLOR_WHITE);
    this->vc_role_colors[VCR_ACTIVE_STATUS2] =
        ansi_color_pair(COLOR_GREEN, COLOR_WHITE) | A_BOLD;
    this->vc_role_colors[VCR_BOLD_STATUS] =
        ansi_color_pair(COLOR_BLACK, COLOR_WHITE) | A_BOLD;
    this->vc_role_colors[VCR_VIEW_STATUS] =
        ansi_color_pair(COLOR_WHITE, COLOR_BLUE) | A_BOLD;

    this->vc_role_colors[VCR_KEYWORD] = ansi_color_pair(COLOR_BLUE, COLOR_BLACK);
    this->vc_role_colors[VCR_STRING] = ansi_color_pair(COLOR_GREEN, COLOR_BLACK) | A_BOLD;
    this->vc_role_colors[VCR_COMMENT] = ansi_color_pair(COLOR_GREEN, COLOR_BLACK);
    this->vc_role_colors[VCR_VARIABLE] = ansi_color_pair(COLOR_CYAN, COLOR_BLACK);
    this->vc_role_colors[VCR_FILE] = ansi_color_pair(COLOR_BLUE, COLOR_BLACK);

    this->vc_role_colors[VCR_DIFF_DELETE]  = ansi_color_pair(COLOR_RED, COLOR_BLACK);
    this->vc_role_colors[VCR_DIFF_ADD]     = ansi_color_pair(COLOR_GREEN, COLOR_BLACK);
    this->vc_role_colors[VCR_DIFF_SECTION] = ansi_color_pair(COLOR_MAGENTA, COLOR_BLACK);

    this->vc_role_colors[VCR_LOW_THRESHOLD] = ansi_color_pair(COLOR_BLACK, COLOR_GREEN);
    this->vc_role_colors[VCR_MED_THRESHOLD] = ansi_color_pair(COLOR_BLACK, COLOR_YELLOW);
    this->vc_role_colors[VCR_HIGH_THRESHOLD] = ansi_color_pair(COLOR_BLACK, COLOR_RED);
}
