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

#include "auto_mem.hh"
#include "lnav_log.hh"
#include "view_curses.hh"
#include "ansi_scrubber.hh"
#include "lnav_config.hh"
#include "yajlpp.hh"
#include "xterm-palette.hh"

using namespace std;

struct xterm_color {
    short xc_id;
    string xc_name;
    rgb_color xc_color;
    lab_color xc_lab_color;
};

static struct json_path_handler xterm_color_handler[] = {
    json_path_handler("colorId")
        .for_field(&nullobj<xterm_color>()->xc_id),
    json_path_handler("name")
        .for_field(&nullobj<xterm_color>()->xc_name),
    json_path_handler("rgb/r")
        .for_field(&nullobj<xterm_color>()->xc_color.rc_r),
    json_path_handler("rgb/g")
        .for_field(&nullobj<xterm_color>()->xc_color.rc_g),
    json_path_handler("rgb/b")
        .for_field(&nullobj<xterm_color>()->xc_color.rc_b),

    json_path_handler()
};

static struct json_path_handler root_color_handler[] = {
    json_path_handler("#/")
        .with_obj_provider<xterm_color, vector<xterm_color>>(
            [](const yajlpp_provider_context &ypc, vector<xterm_color> *palette) {
                palette->resize(ypc.ypc_index + 1);
                return &((*palette)[ypc.ypc_index]);
            })
        .with_children(xterm_color_handler),

    json_path_handler()
};

static struct _xterm_colors {
    _xterm_colors() {
        yajlpp_parse_context ypc_xterm("xterm-palette.json", root_color_handler);
        yajl_handle handle;

        handle = yajl_alloc(&ypc_xterm.ypc_callbacks, NULL, &ypc_xterm);
        ypc_xterm
            .with_ignore_unused(true)
            .with_obj(this->xc_palette)
            .with_handle(handle);
        ypc_xterm.parse((const unsigned char *) xterm_palette_json,
                        strlen(xterm_palette_json));
        ypc_xterm.complete_parse();
        yajl_free(handle);

        for (auto &xc : this->xc_palette) {
            xc.xc_lab_color = lab_color(xc.xc_color);
        }
    };

    short match_color(const lab_color &to_match) {
        double lowest = 1000.0;
        short lowest_id = -1;

        for (auto &xc : this->xc_palette) {
            double xc_delta = xc.xc_lab_color.deltaE(to_match);

            if (lowest_id == -1) {
                lowest = xc_delta;
                lowest_id = xc.xc_id;
                continue;
            }

            if (xc_delta < lowest) {
                lowest = xc_delta;
                lowest_id = xc.xc_id;
            }
        }

        return lowest_id;
    };

    vector<xterm_color> xc_palette;
} xterm_colors;

bool rgb_color::from_str(const string_fragment &color,
                         rgb_color &rgb_out,
                         std::string &errmsg)
{
    if (color[0] == '#') {
        switch (color.length()) {
            case 4:
                if (sscanf(color.data(), "#%1hx%1hx%1hx",
                           &rgb_out.rc_r, &rgb_out.rc_g, &rgb_out.rc_b) == 3) {
                    rgb_out.rc_r |= rgb_out.rc_r << 4;
                    rgb_out.rc_g |= rgb_out.rc_g << 4;
                    rgb_out.rc_b |= rgb_out.rc_b << 4;
                    return true;
                }
                break;
            case 7:
                if (sscanf(color.data(), "#%2hx%2hx%2hx",
                           &rgb_out.rc_r, &rgb_out.rc_g, &rgb_out.rc_b) == 3) {
                    return true;
                }
                break;
        }
        errmsg = "Could not parse color: " + color.to_string();
        return false;
    }

    for (const auto &xc : xterm_colors.xc_palette) {
        if (color == xc.xc_name) {
            rgb_out = xc.xc_color;
            return true;
        }
    }

    return false;
}

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

attr_line_t &attr_line_t::with_ansi_string(const std::string &str)
{
    this->al_string = str;
    scrub_ansi_string(this->al_string, this->al_attrs);

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

        if (avail == 0) {
            avail = INT_MAX;
        }

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

            if ((avail != usable_width) && (lpc - start_pos > avail)) {
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
    if (len == std::string::npos) {
        len = this->length();
    }

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

struct tab_mapping {
    size_t tm_origin;
    size_t tm_dst_start;
    size_t tm_dst_end;

    tab_mapping(size_t origin, size_t dst_start, size_t dst_end)
        : tm_origin(origin), tm_dst_start(dst_start), tm_dst_end(dst_end) {

    };

    size_t length() const {
        return this->tm_dst_end - this->tm_dst_start;
    };
};

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
    string_attrs_t::const_iterator iter;
    vector<tab_mapping> tab_list;
    int    tab_count = 0;
    char  *expanded_line;
    size_t exp_index = 0;
    string full_line;

    require(lr.lr_end >= 0);

    line_width    = lr.length();
    tab_count     = count(line.begin(), line.end(), '\t');
    expanded_line = (char *)alloca(line.size() + tab_count * 8 + 1);

    for (size_t lpc = 0; lpc < line.size(); lpc++) {
        int exp_start_index = exp_index;

        switch (line[lpc]) {
        case '\t':
            do {
                expanded_line[exp_index] = ' ';
                exp_index += 1;
            } while (exp_index % 8);
            tab_list.emplace_back(lpc, exp_start_index, exp_index);
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

        require(attr_range.lr_start >= 0);
        require(attr_range.lr_end >= -1);

        for (auto tab_iter = tab_list.rbegin();
             tab_iter != tab_list.rend();
             ++tab_iter) {
            if (tab_iter->tm_origin < attr_range.lr_start) {
                attr_range.lr_start += tab_iter->length() - 1;
            }
        }

        if (attr_range.lr_end != -1) {
            for (auto tab_iter = tab_list.rbegin();
                 tab_iter != tab_list.rend();
                 ++tab_iter) {
                if (tab_iter->tm_origin < attr_range.lr_end) {
                    attr_range.lr_end += tab_iter->length() - 1;
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
            string_attrs_t::const_iterator range_iter;
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
                cchar_t row_ch[ch_width + 1];

                mvwin_wchnstr(window, y, x_pos, row_ch, ch_width);
                for (int lpc = 0; lpc < ch_width; lpc++) {
                    if (color_pair > 0) {
                        row_ch[lpc].attr = attrs & ~A_COLOR;
#ifdef NCURSES_EXT_COLORS
                        row_ch[lpc].ext_color = color_pair;
#else
                        row_ch[lpc].attr |= COLOR_PAIR(color_pair);
#endif
                    } else {
                        row_ch[lpc].attr = attrs;
                    }
                }
                mvwadd_wchnstr(window, y, x_pos, row_ch, ch_width);
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
        view_colors::singleton().init_roles(0);
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

view_colors::view_colors() : vc_color_pair_end(0)
{
}

bool view_colors::initialized = false;

void view_colors::init(void)
{
    int color_pair_base = VC_ANSI_END;

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

        if (lnav_config.lc_ui_default_colors) {
            use_default_colors();
        }
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
            int bg = (lnav_config.lc_ui_default_colors ? -1 : COLOR_BLACK);

            for (int z = 0; z < 6; z++) {
                for (int x = 1; x < 6; x += 2) {
                    for (int y = 1; y < 6; y += 2) {
                        int fg = 16 + x + (y * 6) + (z * 6 * 6);

                        init_pair(color_pair_base, fg, bg);
                        color_pair_base += 1;
                    }
                }
            }
        }
    }

    singleton().init_roles(color_pair_base);

    initialized = true;
}

inline int attr_for_colors(int &pair_base, short fg, short bg)
{
    int pair = ++pair_base;

    if (lnav_config.lc_ui_default_colors) {
        if (fg == COLOR_WHITE) {
            fg = -1;
        }
        if (bg == COLOR_BLACK) {
            bg = -1;
        }
    }

    init_pair(pair, fg, bg);

    return COLOR_PAIR(pair);
}

void view_colors::init_roles(int color_pair_base)
{
    /* Setup the mappings from roles to actual colors. */
    this->vc_role_colors[VCR_TEXT] =
        attr_for_colors(color_pair_base, COLOR_WHITE, COLOR_BLACK);
    if (lnav_config.lc_ui_dim_text) {
        this->vc_role_colors[VCR_TEXT] |= A_DIM;
    }
    this->vc_role_colors[VCR_SEARCH] =
        this->vc_role_colors[VCR_TEXT] | A_REVERSE;
    this->vc_role_colors[VCR_OK]      = attr_for_colors(color_pair_base, COLOR_GREEN, COLOR_BLACK) | A_BOLD;
    this->vc_role_colors[VCR_ERROR]   = attr_for_colors(color_pair_base, COLOR_RED, COLOR_BLACK) | A_BOLD;
    this->vc_role_colors[VCR_WARNING] = attr_for_colors(color_pair_base, COLOR_YELLOW, COLOR_BLACK) | A_BOLD;
    this->vc_role_colors[VCR_ALT_ROW] = this->vc_role_colors[VCR_TEXT] | A_BOLD;
    this->vc_role_colors[VCR_HIDDEN] = attr_for_colors(color_pair_base, COLOR_YELLOW, COLOR_BLACK);
    this->vc_role_colors[VCR_ADJUSTED_TIME] = attr_for_colors(color_pair_base, COLOR_MAGENTA, COLOR_BLACK);
    this->vc_role_colors[VCR_SKEWED_TIME] = attr_for_colors(color_pair_base, COLOR_YELLOW, COLOR_BLACK) | A_UNDERLINE;
    this->vc_role_colors[VCR_OFFSET_TIME] = attr_for_colors(color_pair_base, COLOR_CYAN, COLOR_BLACK);

    this->vc_role_colors[VCR_STATUS] =
        attr_for_colors(color_pair_base, COLOR_BLACK, COLOR_WHITE);
    this->vc_role_colors[VCR_WARN_STATUS] =
        attr_for_colors(color_pair_base, COLOR_YELLOW, COLOR_WHITE) | A_BOLD;
    this->vc_role_colors[VCR_ALERT_STATUS] =
        attr_for_colors(color_pair_base, COLOR_RED, COLOR_WHITE) | A_BOLD;
    this->vc_role_colors[VCR_ACTIVE_STATUS] =
        attr_for_colors(color_pair_base, COLOR_GREEN, COLOR_WHITE);
    this->vc_role_colors[VCR_ACTIVE_STATUS2] =
        attr_for_colors(color_pair_base, COLOR_GREEN, COLOR_WHITE) | A_BOLD;
    this->vc_role_colors[VCR_BOLD_STATUS] =
        attr_for_colors(color_pair_base, COLOR_BLACK, COLOR_WHITE) | A_BOLD;
    this->vc_role_colors[VCR_VIEW_STATUS] =
        attr_for_colors(color_pair_base, COLOR_WHITE, COLOR_BLUE) | A_BOLD;

    this->vc_role_colors[VCR_KEYWORD] = attr_for_colors(color_pair_base, COLOR_BLUE, COLOR_BLACK);
    this->vc_role_colors[VCR_STRING] = attr_for_colors(color_pair_base, COLOR_GREEN, COLOR_BLACK) | A_BOLD;
    this->vc_role_colors[VCR_COMMENT] = attr_for_colors(color_pair_base, COLOR_GREEN, COLOR_BLACK);
    this->vc_role_colors[VCR_VARIABLE] = attr_for_colors(color_pair_base, COLOR_CYAN, COLOR_BLACK);
    this->vc_role_colors[VCR_SYMBOL] = attr_for_colors(color_pair_base, COLOR_MAGENTA, COLOR_BLACK);
    this->vc_role_colors[VCR_RE_SPECIAL] = attr_for_colors(color_pair_base, COLOR_CYAN, COLOR_BLACK);
    this->vc_role_colors[VCR_RE_REPEAT] = attr_for_colors(color_pair_base, COLOR_YELLOW, COLOR_BLACK);
    this->vc_role_colors[VCR_FILE] = attr_for_colors(color_pair_base, COLOR_BLUE, COLOR_BLACK);

    this->vc_role_colors[VCR_DIFF_DELETE]  = attr_for_colors(color_pair_base, COLOR_RED, COLOR_BLACK);
    this->vc_role_colors[VCR_DIFF_ADD]     = attr_for_colors(color_pair_base, COLOR_GREEN, COLOR_BLACK);
    this->vc_role_colors[VCR_DIFF_SECTION] = attr_for_colors(color_pair_base, COLOR_MAGENTA, COLOR_BLACK);

    this->vc_role_colors[VCR_LOW_THRESHOLD] = attr_for_colors(color_pair_base, COLOR_BLACK, COLOR_GREEN);
    this->vc_role_colors[VCR_MED_THRESHOLD] = attr_for_colors(color_pair_base, COLOR_BLACK, COLOR_YELLOW);
    this->vc_role_colors[VCR_HIGH_THRESHOLD] = attr_for_colors(color_pair_base, COLOR_BLACK, COLOR_RED);

    this->vc_color_pair_end = color_pair_base + 1;
}

int view_colors::ensure_color_pair(const rgb_color &rgb_fg, const rgb_color &rgb_bg)
{
    return attr_for_colors(
        this->vc_color_pair_end,
        xterm_colors.match_color(rgb_fg),
        rgb_bg.empty() ? (short) COLOR_BLACK : xterm_colors.match_color(rgb_bg));
}
