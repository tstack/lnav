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

#include <cmath>
#include <string>

#include "auto_mem.hh"
#include "base/lnav_log.hh"
#include "view_curses.hh"
#include "ansi_scrubber.hh"
#include "lnav_config.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"
#include "xterm-palette.hh"
#include "attr_line.hh"
#include "shlex.hh"

using namespace std;

struct term_color {
    short xc_id;
    string xc_name;
    rgb_color xc_color;
    lab_color xc_lab_color;
};

static struct json_path_handler term_color_rgb_handler[] = {
    json_path_handler("r")
        .FOR_FIELD(rgb_color, rc_r),
    json_path_handler("g")
        .FOR_FIELD(rgb_color, rc_g),
    json_path_handler("b")
        .FOR_FIELD(rgb_color, rc_b),

    json_path_handler()
};

static struct json_path_handler term_color_handler[] = {
    json_path_handler("colorId")
        .FOR_FIELD(term_color, xc_id),
    json_path_handler("name")
        .FOR_FIELD(term_color, xc_name),
    json_path_handler("rgb/")
        .with_obj_provider<rgb_color, term_color>([](const auto &pc, term_color *xc) { return &xc->xc_color; })
        .with_children(term_color_rgb_handler),

    json_path_handler()
};

static struct json_path_handler root_color_handler[] = {
    json_path_handler("#/")
        .with_obj_provider<term_color, vector<term_color>>(
            [](const yajlpp_provider_context &ypc, vector<term_color> *palette) {
                palette->resize(ypc.ypc_index + 1);
                return &((*palette)[ypc.ypc_index]);
            })
        .with_children(term_color_handler),

    json_path_handler()
};

struct term_color_palette {
    term_color_palette(const unsigned char *json) {
        yajlpp_parse_context ypc_xterm("palette.json", root_color_handler);
        yajl_handle handle;

        handle = yajl_alloc(&ypc_xterm.ypc_callbacks, nullptr, &ypc_xterm);
        ypc_xterm
            .with_ignore_unused(true)
            .with_obj(this->tc_palette)
            .with_handle(handle);
        yajl_status st = ypc_xterm.parse(json, strlen((const char *) json));
        ensure(st == yajl_status_ok);
        st = ypc_xterm.complete_parse();
        ensure(st == yajl_status_ok);
        yajl_free(handle);

        for (auto &xc : this->tc_palette) {
            xc.xc_lab_color = lab_color(xc.xc_color);
        }
    };

    short match_color(const lab_color &to_match) {
        double lowest = 1000.0;
        short lowest_id = -1;

        for (auto &xc : this->tc_palette) {
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

    vector<term_color> tc_palette;
};

term_color_palette xterm_colors(xterm_palette_json);
term_color_palette ansi_colors(ansi_palette_json);

term_color_palette *ACTIVE_PALETTE = &ansi_colors;

bool rgb_color::from_str(const string_fragment &color,
                         rgb_color &rgb_out,
                         std::string &errmsg)
{
    if (color.empty()) {
        return true;
    }

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

    for (const auto &xc : xterm_colors.tc_palette) {
        if (color.iequal(xc.xc_name)) {
            rgb_out = xc.xc_color;
            return true;
        }
    }

    errmsg = "Unknown color: '" + color.to_string() +
        "'.  See https://jonasjacek.github.io/colors/ for a list of supported color names";
    return false;
}

string_attr_type view_curses::VC_ROLE("role");
string_attr_type view_curses::VC_STYLE("style");
string_attr_type view_curses::VC_GRAPHIC("graphic");
string_attr_type view_curses::VC_SELECTED("selected");
string_attr_type view_curses::VC_FOREGROUND("foreground");
string_attr_type view_curses::VC_BACKGROUND("background");

const struct itimerval ui_periodic_timer::INTERVAL = {    { 0, 350 * 1000 },
    { 0, 350 * 1000 }
};

ui_periodic_timer::ui_periodic_timer()
        : upt_counter(0)
{
    struct sigaction sa;

    sa.sa_handler = ui_periodic_timer::sigalrm;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, nullptr);
    if (setitimer(ITIMER_REAL, &INTERVAL, nullptr) == -1) {
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
    auto ret = vasprintf(formatted_str.out(), str, args);
    va_end(args);

    if (ret >= 0 && formatted_str != nullptr) {
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

attr_line_t &attr_line_t::insert(size_t index, const attr_line_t &al, text_wrap_settings *tws)
{
    if (index < this->al_string.length()) {
        shift_string_attrs(this->al_attrs, index, al.al_string.length());
    }

    this->al_string.insert(index, al.al_string);

    for (auto &sa : al.al_attrs) {
        this->al_attrs.emplace_back(sa);

        line_range &lr = this->al_attrs.back().sa_range;

        lr.shift(0, index);
        if (lr.lr_end == -1) {
            lr.lr_end = index + al.al_string.length();
        }
    }

    if (tws != nullptr && (int)this->al_string.length() > tws->tws_width) {
        ssize_t start_pos = index;
        ssize_t line_start = this->al_string.rfind('\n', start_pos);

        if (line_start == (ssize_t)string::npos) {
            line_start = 0;
        } else {
            line_start += 1;
        }

        ssize_t line_len = index - line_start;
        ssize_t usable_width = tws->tws_width - tws->tws_indent;
        ssize_t avail = max((ssize_t) 0, (ssize_t) tws->tws_width - line_len);

        if (avail == 0) {
            avail = INT_MAX;
        }

        while (start_pos < (int)this->al_string.length()) {
            ssize_t lpc;

            // Find the end of a word or a breakpoint.
            for (lpc = start_pos;
                 lpc < (int)this->al_string.length() &&
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
                while (lpc < (int)this->al_string.length() && avail) {
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
                         lpc < (int)this->al_string.length() &&
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
        len = this->al_string.length() - start;
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
                                     sa.sa_type, sa.sa_value);

        line_range &last_lr = retval.al_attrs.back().sa_range;

        ensure(last_lr.lr_end <= retval.al_string.length());
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

struct utf_to_display_adjustment {
    int uda_origin;
    int uda_offset;

    utf_to_display_adjustment(int utf_origin, int offset)
        : uda_origin(utf_origin), uda_offset(offset) {

    };
};

void view_curses::mvwattrline(WINDOW *window,
                              int y,
                              int x,
                              attr_line_t &al,
                              const struct line_range &lr,
                              view_colors::role_t base_role)
{
    attr_t text_attrs, attrs;
    int line_width;
    string_attrs_t &         sa   = al.get_attrs();
    string &                 line = al.get_string();
    string_attrs_t::const_iterator iter;
    vector<utf_to_display_adjustment> utf_adjustments;
    int tab_count = 0;
    char *expanded_line;
    int exp_index = 0;
    int exp_offset = 0;
    string full_line;

    require(lr.lr_end >= 0);

    line_width    = lr.length();
    tab_count     = count(line.begin(), line.end(), '\t');
    expanded_line = (char *)alloca(line.size() + tab_count * 8 + 1);

    unsigned char *fg_color = (unsigned char *) alloca(line_width);
    bool has_fg = false;
    unsigned char *bg_color = (unsigned char *) alloca(line_width);
    bool has_bg = false;

    for (size_t lpc = 0; lpc < line.size(); lpc++) {
        int exp_start_index = exp_index;
        unsigned char ch = static_cast<unsigned char>(line[lpc]);

        switch (ch) {
        case '\t':
            do {
                expanded_line[exp_index] = ' ';
                exp_index += 1;
            } while (exp_index % 8);
            utf_adjustments.emplace_back(lpc, exp_index - exp_start_index - 1);
            break;

        case '\r':
            /* exp_index = -1; */
            break;

        case '\n':
            expanded_line[exp_index] = ' ';
            exp_index += 1;
            break;

        default: {
            int offset = 0;

            expanded_line[exp_index] = line[lpc];
            exp_index += 1;
            if ((ch & 0xf8) == 0xf0) {
                offset = -3;
            } else if ((ch & 0xf0) == 0xe0) {
                offset = -2;
            } else if ((ch & 0xe0) == 0xc0) {
                offset = -1;
            }

            if (offset) {
                exp_offset += offset;
                utf_adjustments.emplace_back(lpc, offset);
                for (; offset && (lpc + 1) < line.size(); lpc++, offset++) {
                    expanded_line[exp_index] = line[lpc + 1];
                    exp_index += 1;
                }
            }
            break;
        }
        }
    }

    expanded_line[exp_index] = '\0';
    full_line = string(expanded_line);

    view_colors &vc = view_colors::singleton();
    text_attrs = vc.attrs_for_role(base_role);
    attrs      = text_attrs;
    wmove(window, y, x);
    wattron(window, attrs);
    if (lr.lr_start < (int)full_line.size()) {
        waddnstr(window, &full_line.c_str()[lr.lr_start], line_width);
    }
    if (lr.lr_end > (int)full_line.size()) {
        whline(window, ' ', lr.lr_end - (full_line.size() + exp_offset));
    }
    wattroff(window, attrs);

    stable_sort(sa.begin(), sa.end());
    for (iter = sa.begin(); iter != sa.end(); ++iter) {
        struct line_range attr_range = iter->sa_range;

        require(attr_range.lr_start >= 0);
        require(attr_range.lr_end >= -1);

        if (!(iter->sa_type == &VC_ROLE ||
              iter->sa_type == &VC_STYLE ||
              iter->sa_type == &VC_GRAPHIC ||
              iter->sa_type == &VC_FOREGROUND ||
              iter->sa_type == &VC_BACKGROUND)) {
            continue;
        }

        for (const auto &adj : utf_adjustments) {
            if (adj.uda_origin < iter->sa_range.lr_start) {
                attr_range.lr_start += adj.uda_offset;
            }
        }

        if (attr_range.lr_end != -1) {
            for (const auto &adj : utf_adjustments) {
                if (adj.uda_origin < iter->sa_range.lr_end) {
                    attr_range.lr_end += adj.uda_offset;
                }
            }
        }

        attr_range.lr_start = max(0, attr_range.lr_start - lr.lr_start);
        if (attr_range.lr_end == -1) {
            attr_range.lr_end = lr.lr_start + line_width;
        }

        attr_range.lr_end = min(line_width, attr_range.lr_end - lr.lr_start);

        if (iter->sa_type == &VC_GRAPHIC) {
            for (int index = attr_range.lr_start;
                index < attr_range.lr_end;
                index++) {
                mvwaddch(window, y, x + index, iter->sa_value.sav_int | text_attrs);
            }
            continue;
        }

        if (iter->sa_type == &VC_FOREGROUND) {
            if (!has_fg) {
                memset(fg_color, COLOR_WHITE, line_width);
            }
            fill(fg_color + attr_range.lr_start, fg_color + attr_range.lr_end, iter->sa_value.sav_int);
            has_fg = true;
            continue;
        }

        if (iter->sa_type == &VC_BACKGROUND) {
            if (!has_bg) {
                memset(bg_color, COLOR_BLACK, line_width);
            }
            fill(bg_color + attr_range.lr_start, bg_color + attr_range.lr_end, iter->sa_value.sav_int);
            has_bg = true;
            continue;
        }

        if (attr_range.lr_end > attr_range.lr_start) {
            int awidth = attr_range.length();
            int color_pair;

            if (iter->sa_type == &VC_STYLE) {
                attrs = iter->sa_value.sav_int & ~A_COLOR;
                color_pair = PAIR_NUMBER(iter->sa_value.sav_int);
            } else {
                attrs = vc.attrs_for_role((view_colors::role_t) iter->sa_value.sav_int);
                color_pair = PAIR_NUMBER(attrs);
                attrs = attrs & ~A_COLOR;
            }

            if (attrs || color_pair > 0) {
                int x_pos = x + attr_range.lr_start;
                int ch_width = min(awidth, (line_width - attr_range.lr_start));
                cchar_t row_ch[ch_width + 1];

                mvwin_wchnstr(window, y, x_pos, row_ch, ch_width);
                for (int lpc = 0; lpc < ch_width; lpc++) {
                    bool clear_rev = false;

                    if (row_ch[lpc].attr & A_REVERSE && attrs & A_REVERSE) {
                        clear_rev = true;
                    }
                    if (color_pair > 0) {
                        row_ch[lpc].attr =
                            attrs | (row_ch[lpc].attr & ~A_COLOR);
#ifdef NCURSES_EXT_COLORS
                        row_ch[lpc].ext_color = color_pair;
#else
                        row_ch[lpc].attr |= COLOR_PAIR(color_pair);
#endif
                    } else {
                        row_ch[lpc].attr |= attrs;
                    }
                    if (clear_rev) {
                        row_ch[lpc].attr &= ~A_REVERSE;
                    }
                }
                mvwadd_wchnstr(window, y, x_pos, row_ch, ch_width);
            }
        }
    }

#if 1
    if (has_fg || has_bg) {
        if (!has_fg) {
            memset(fg_color, COLOR_WHITE, line_width);
        }
        if (!has_bg) {
            memset(bg_color, COLOR_BLACK, line_width);
        }

        int x_pos = x + lr.lr_start;
        int ch_width = lr.length();
        cchar_t row_ch[ch_width + 1];

        mvwin_wchnstr(window, y, x_pos, row_ch, ch_width);
        for (int lpc = 0; lpc < ch_width; lpc++) {
            int color_pair = view_colors::ansi_color_pair_index(fg_color[lpc], bg_color[lpc]);

            row_ch[lpc].attr = row_ch[lpc].attr & ~A_COLOR;
#ifdef NCURSES_EXT_COLORS
            row_ch[lpc].ext_color = color_pair;
#else
            row_ch[lpc].attr |= COLOR_PAIR(color_pair);
#endif
        }
        mvwadd_wchnstr(window, y, x_pos, row_ch, ch_width);
    }
#endif
}

attr_t view_colors::BASIC_HL_PAIRS[view_colors::BASIC_COLOR_COUNT] = {
    ansi_color_pair(COLOR_BLUE, COLOR_BLACK),
    ansi_color_pair(COLOR_CYAN, COLOR_BLACK),
    ansi_color_pair(COLOR_GREEN, COLOR_BLACK),
    ansi_color_pair(COLOR_MAGENTA, COLOR_BLACK),
    ansi_color_pair(COLOR_BLACK, COLOR_WHITE),
    ansi_color_pair(COLOR_CYAN, COLOR_BLACK),
    ansi_color_pair(COLOR_YELLOW, COLOR_MAGENTA) | A_BOLD,
    ansi_color_pair(COLOR_MAGENTA, COLOR_CYAN) | A_BOLD,
};

view_colors &view_colors::singleton()
{
    static view_colors s_vc;

    return s_vc;
}

view_colors::view_colors() : vc_color_pair_end(0)
{
}

bool view_colors::initialized = false;

static string COLOR_NAMES[] = {
    "black",
    "red",
    "green",
    "yellow",
    "blue",
    "magenta",
    "cyan",
    "white",
};

class color_listener : public lnav_config_listener {
public:
    void reload_config(error_reporter &reporter) {
        view_colors &vc = view_colors::singleton();

        for (const auto &pair : lnav_config.lc_ui_theme_defs) {
            vc.init_roles(pair.second, reporter);
        }

        auto iter = lnav_config.lc_ui_theme_defs.find(lnav_config.lc_ui_theme);

        if (iter == lnav_config.lc_ui_theme_defs.end()) {
            reporter(&lnav_config.lc_ui_theme,
                     "unknown theme -- " + lnav_config.lc_ui_theme);
            return;
        }

        if (view_colors::initialized) {
            vc.init_roles(iter->second, reporter);
        }
    }
};

static color_listener _COLOR_LISTENER;

void view_colors::init()
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
        if (COLORS >= 256) {
            ACTIVE_PALETTE = &xterm_colors;
        }
    }

    initialized = true;

    {
        auto reporter = [](const void *, const std::string &) {

        };

        _COLOR_LISTENER.reload_config(reporter);
    }
}

inline attr_t attr_for_colors(int &pair_base, short fg, short bg)
{
    if (COLOR_PAIRS <= 64) {
        return view_colors::ansi_color_pair(fg, bg);
    } else {
        if (lnav_config.lc_ui_default_colors) {
            if (fg == COLOR_WHITE) {
                fg = -1;
            }
            if (bg == COLOR_BLACK) {
                bg = -1;
            }
        }
    }

    int pair = ++pair_base;

    if (view_colors::initialized) {
        init_pair(pair, fg, bg);
    }

    return COLOR_PAIR(pair);
}

pair<attr_t, attr_t> view_colors::to_attrs(
    int &pair_base,
    const lnav_theme &lt, const style_config &sc,
    lnav_config_listener::error_reporter &reporter)
{
    rgb_color fg, bg, sbg;
    string fg1, bg1, sbg1, fg_color, bg_color, sbg_color, errmsg;

    fg1 = sc.sc_color;
    if (fg1.empty()) {
        fg1 = lt.lt_style_text.sc_color;
    }
    bg1 = sc.sc_background_color;
    if (bg1.empty()) {
        bg1 = lt.lt_style_text.sc_background_color;
    }
    sbg1 = sc.sc_selected_color;
    if (sbg1.empty()) {
        sbg1 = lt.lt_style_text.sc_selected_color;
    }
    shlex(fg1).eval(fg_color, lt.lt_vars);
    shlex(bg1).eval(bg_color, lt.lt_vars);
    shlex(sbg1).eval(sbg_color, lt.lt_vars);

    if (!rgb_color::from_str(fg_color, fg, errmsg)) {
        reporter(&sc.sc_color, errmsg);
    }
    if (!rgb_color::from_str(bg_color, bg, errmsg)) {
        reporter(&sc.sc_background_color, errmsg);
    }
    if (!rgb_color::from_str(sbg_color, sbg, errmsg)) {
        reporter(&sc.sc_selected_color, errmsg);
    }

    attr_t retval1 = this->ensure_color_pair(pair_base, fg, bg);
    attr_t retval2 = this->ensure_color_pair(pair_base, fg, sbg);

    if (sc.sc_underline) {
        retval1 |= A_UNDERLINE;
        retval2 |= A_UNDERLINE;
    }
    if (sc.sc_bold) {
        retval1 |= A_BOLD;
        retval2 |= A_BOLD;
    }

    return make_pair(retval1, retval2);
}

void view_colors::init_roles(const lnav_theme &lt,
    lnav_config_listener::error_reporter &reporter)
{
    int color_pair_base = VC_ANSI_END;
    rgb_color fg, bg;
    string err;

    if (COLORS == 256) {
        const style_config &ident_sc = lt.lt_style_identifier;
        int ident_bg = (lnav_config.lc_ui_default_colors ? -1 : COLOR_BLACK);

        if (!ident_sc.sc_background_color.empty()) {
            string bg_color, errmsg;
            rgb_color rgb_bg;

            shlex(ident_sc.sc_background_color).eval(bg_color, lt.lt_vars);
            if (!rgb_color::from_str(bg_color, rgb_bg, errmsg)) {
                reporter(&ident_sc.sc_background_color, errmsg);
            }
            ident_bg = ACTIVE_PALETTE->match_color(rgb_bg);
        }
        for (int z = 0; z < 6; z++) {
            for (int x = 1; x < 6; x += 2) {
                for (int y = 1; y < 6; y += 2) {
                    int fg = 16 + x + (y * 6) + (z * 6 * 6);

                    init_pair(color_pair_base, fg, ident_bg);
                    color_pair_base += 1;
                }
            }
        }
    } else {
        color_pair_base = VC_ANSI_END + HI_COLOR_COUNT;
    }

    /* Setup the mappings from roles to actual colors. */
    this->vc_role_colors[VCR_TEXT] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_text, reporter);

    {
        int pnum = PAIR_NUMBER(this->vc_role_colors[VCR_TEXT].first);
        short text_fg, text_bg;

        pair_content(pnum, &text_fg, &text_bg);
        for (int ansi_fg = 0; ansi_fg < 8; ansi_fg++) {
            for (int ansi_bg = 0; ansi_bg < 8; ansi_bg++) {
                if (ansi_fg == 0 && ansi_bg == 0) {
                    continue;
                }

                auto fg_str = lt.lt_vars.find(COLOR_NAMES[ansi_fg]);
                auto bg_str = lt.lt_vars.find(COLOR_NAMES[ansi_bg]);
                rgb_color rgb_fg, rgb_bg;
                string errmsg;

                if (fg_str != lt.lt_vars.end() &&
                    !rgb_color::from_str(fg_str->second, rgb_fg, errmsg)) {
                    reporter(&fg_str->second, errmsg);
                    return;
                }
                if (bg_str != lt.lt_vars.end() &&
                    !rgb_color::from_str(bg_str->second, rgb_bg, errmsg)) {
                    reporter(&bg_str->second, errmsg);
                    return;
                }

                short fg = ACTIVE_PALETTE->match_color(rgb_fg);
                short bg = ACTIVE_PALETTE->match_color(rgb_bg);

                if (rgb_fg.empty()) {
                    fg = ansi_fg;
                }
                if (rgb_bg.empty()) {
                    bg = ansi_bg;
                }

                init_pair(ansi_color_pair_index(ansi_fg, ansi_bg), fg, bg);
            }
        }
    }
    if (lnav_config.lc_ui_dim_text) {
        this->vc_role_colors[VCR_TEXT].first |= A_DIM;
        this->vc_role_colors[VCR_TEXT].second |= A_DIM;
    }
    this->vc_role_colors[VCR_SEARCH] = make_pair(A_REVERSE, A_REVERSE);
    this->vc_role_colors[VCR_OK] = this->to_attrs(color_pair_base,
                                                  lt, lt.lt_style_ok,
                                                  reporter);
    this->vc_role_colors[VCR_ERROR] = this->to_attrs(color_pair_base,
                                                     lt, lt.lt_style_error,
                                                     reporter);
    this->vc_role_colors[VCR_WARNING] = this->to_attrs(color_pair_base,
                                                       lt, lt.lt_style_warning,
                                                       reporter);
    this->vc_role_colors[VCR_ALT_ROW] = this->to_attrs(color_pair_base,
                                                       lt, lt.lt_style_alt_text,
                                                       reporter);
    this->vc_role_colors[VCR_HIDDEN] = this->to_attrs(color_pair_base,
                                                      lt, lt.lt_style_hidden,
                                                      reporter);
    this->vc_role_colors[VCR_ADJUSTED_TIME] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_adjusted_time, reporter);
    this->vc_role_colors[VCR_SKEWED_TIME] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_skewed_time, reporter);
    this->vc_role_colors[VCR_OFFSET_TIME] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_offset_time, reporter);

    this->vc_role_colors[VCR_STATUS] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_status, reporter);
    this->vc_role_colors[VCR_WARN_STATUS] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_warn_status, reporter);
    this->vc_role_colors[VCR_ALERT_STATUS] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_alert_status, reporter);
    this->vc_role_colors[VCR_ACTIVE_STATUS] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_active_status, reporter);
    this->vc_role_colors[VCR_ACTIVE_STATUS2] =
        make_pair(this->vc_role_colors[VCR_ACTIVE_STATUS].first | A_BOLD,
                  this->vc_role_colors[VCR_ACTIVE_STATUS].second | A_BOLD);
    this->vc_role_colors[VCR_STATUS_TITLE] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_status_title, reporter);
    this->vc_role_colors[VCR_STATUS_SUBTITLE] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_status_subtitle, reporter);

    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status_subtitle.sc_background_color;
        stitch_sc.sc_background_color =
            lt.lt_style_status_title.sc_background_color;
        this->vc_role_colors[VCR_STATUS_STITCH_TITLE_TO_SUB] =
            this->to_attrs(color_pair_base, lt, stitch_sc, reporter);
    }
    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status_title.sc_background_color;
        stitch_sc.sc_background_color =
            lt.lt_style_status_subtitle.sc_background_color;
        this->vc_role_colors[VCR_STATUS_STITCH_SUB_TO_TITLE] =
            this->to_attrs(color_pair_base, lt, stitch_sc, reporter);
    }

    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status.sc_background_color;
        stitch_sc.sc_background_color =
            lt.lt_style_status_subtitle.sc_background_color;
        this->vc_role_colors[VCR_STATUS_STITCH_SUB_TO_NORMAL] =
            this->to_attrs(color_pair_base, lt, stitch_sc, reporter);
    }
    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status_subtitle.sc_background_color;
        stitch_sc.sc_background_color =
            lt.lt_style_status.sc_background_color;
        this->vc_role_colors[VCR_STATUS_STITCH_NORMAL_TO_SUB] =
            this->to_attrs(color_pair_base, lt, stitch_sc, reporter);
    }

    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status.sc_background_color;
        stitch_sc.sc_background_color =
            lt.lt_style_status_title.sc_background_color;
        this->vc_role_colors[VCR_STATUS_STITCH_TITLE_TO_NORMAL] =
            this->to_attrs(color_pair_base, lt, stitch_sc, reporter);
    }
    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status_title.sc_background_color;
        stitch_sc.sc_background_color =
            lt.lt_style_status.sc_background_color;
        this->vc_role_colors[VCR_STATUS_STITCH_NORMAL_TO_TITLE] =
            this->to_attrs(color_pair_base, lt, stitch_sc, reporter);
    }

    this->vc_role_colors[VCR_INACTIVE_STATUS] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_inactive_status, reporter);

    this->vc_role_colors[VCR_POPUP] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_popup, reporter);
    this->vc_role_colors[VCR_COLOR_HINT] = make_pair(
        COLOR_PAIR(color_pair_base), COLOR_PAIR(color_pair_base + 1));
    color_pair_base += 2;

    this->vc_role_colors[VCR_KEYWORD] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_keyword, reporter);
    this->vc_role_colors[VCR_STRING] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_string, reporter);
    this->vc_role_colors[VCR_COMMENT] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_comment, reporter);
    this->vc_role_colors[VCR_VARIABLE] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_variable, reporter);
    this->vc_role_colors[VCR_SYMBOL] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_symbol, reporter);
    this->vc_role_colors[VCR_NUMBER] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_number, reporter);

    this->vc_role_colors[VCR_RE_SPECIAL] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_re_special, reporter);
    this->vc_role_colors[VCR_RE_REPEAT] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_re_repeat, reporter);
    this->vc_role_colors[VCR_FILE] = this->to_attrs(color_pair_base,
        lt, lt.lt_style_file, reporter);

    this->vc_role_colors[VCR_DIFF_DELETE]  = this->to_attrs(
        color_pair_base, lt, lt.lt_style_diff_delete, reporter);
    this->vc_role_colors[VCR_DIFF_ADD]     = this->to_attrs(
        color_pair_base, lt, lt.lt_style_diff_add, reporter);
    this->vc_role_colors[VCR_DIFF_SECTION] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_diff_section, reporter);

    this->vc_role_colors[VCR_LOW_THRESHOLD] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_low_threshold, reporter);
    this->vc_role_colors[VCR_MED_THRESHOLD] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_med_threshold, reporter);
    this->vc_role_colors[VCR_HIGH_THRESHOLD] = this->to_attrs(
        color_pair_base, lt, lt.lt_style_high_threshold, reporter);

    for (log_level_t level = static_cast<log_level_t>(LEVEL_UNKNOWN + 1);
         level < LEVEL__MAX;
         level = static_cast<log_level_t>(level + 1)) {
        auto level_iter = lt.lt_level_styles.find(level);

        if (level_iter == lt.lt_level_styles.end()) {
            this->vc_level_attrs[level] = this->to_attrs(
                color_pair_base, lt, lt.lt_style_text, reporter);
        } else {
            this->vc_level_attrs[level] = this->to_attrs(
                color_pair_base, lt, level_iter->second, reporter);
        }
    }

    if (initialized && this->vc_color_pair_end == 0) {
        this->vc_color_pair_end = color_pair_base + 1;
    }
}

int view_colors::ensure_color_pair(int &pair_base, const rgb_color &rgb_fg, const rgb_color &rgb_bg)
{
    return attr_for_colors(
        pair_base,
        rgb_fg.empty() ? (short) COLOR_WHITE : ACTIVE_PALETTE->match_color(rgb_fg),
        rgb_bg.empty() ? (short) COLOR_BLACK : ACTIVE_PALETTE->match_color(rgb_bg));
}

attr_t view_colors::attrs_for_ident(const char *str, size_t len) const
{
    unsigned long index = crc32(1, (const Bytef*)str, len);
    attr_t retval;

    if (COLORS >= 256) {
        unsigned long offset = index % HI_COLOR_COUNT;
        retval = COLOR_PAIR(VC_ANSI_END + offset);

        short fg, bg;
        int pnum = PAIR_NUMBER(retval);
        pair_content(pnum, &fg, &bg);
    }
    else {
        retval = BASIC_HL_PAIRS[index % BASIC_COLOR_COUNT];
    }

    return retval;
}

lab_color::lab_color(const rgb_color &rgb)
{
    double r = rgb.rc_r / 255.0,
        g = rgb.rc_g / 255.0,
        b = rgb.rc_b / 255.0,
        x, y, z;

    r = (r > 0.04045) ? pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
    g = (g > 0.04045) ? pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
    b = (b > 0.04045) ? pow((b + 0.055) / 1.055, 2.4) : b / 12.92;

    x = (r * 0.4124 + g * 0.3576 + b * 0.1805) / 0.95047;
    y = (r * 0.2126 + g * 0.7152 + b * 0.0722) / 1.00000;
    z = (r * 0.0193 + g * 0.1192 + b * 0.9505) / 1.08883;

    x = (x > 0.008856) ? pow(x, 1.0/3.0) : (7.787 * x) + 16.0/116.0;
    y = (y > 0.008856) ? pow(y, 1.0/3.0) : (7.787 * y) + 16.0/116.0;
    z = (z > 0.008856) ? pow(z, 1.0/3.0) : (7.787 * z) + 16.0/116.0;

    this->lc_l = (116.0 * y) - 16;
    this->lc_a = 500.0 * (x - y);
    this->lc_b = 200.0 * (y - z);
}

double lab_color::deltaE(const lab_color &other) const
{
    double deltaL = this->lc_l - other.lc_l;
    double deltaA = this->lc_a - other.lc_a;
    double deltaB = this->lc_b - other.lc_b;
    double c1 = sqrt(this->lc_a * this->lc_a + this->lc_b * this->lc_b);
    double c2 = sqrt(other.lc_a * other.lc_a + other.lc_b * other.lc_b);
    double deltaC = c1 - c2;
    double deltaH = deltaA * deltaA + deltaB * deltaB - deltaC * deltaC;
    deltaH = deltaH < 0.0 ? 0.0 : sqrt(deltaH);
    double sc = 1.0 + 0.045 * c1;
    double sh = 1.0 + 0.015 * c1;
    double deltaLKlsl = deltaL / (1.0);
    double deltaCkcsc = deltaC / (sc);
    double deltaHkhsh = deltaH / (sh);
    double i = deltaLKlsl * deltaLKlsl + deltaCkcsc * deltaCkcsc + deltaHkhsh * deltaHkhsh;
    return i < 0.0 ? 0.0 : sqrt(i);
}
