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
 *
 * @file view_curses.cc
 */

#ifdef __CYGWIN__
#    include <alloca.h>
#endif

#include <chrono>
#include <cmath>
#include <string>

#include "base/ansi_scrubber.hh"
#include "base/attr_line.hh"
#include "base/itertools.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "lnav_config.hh"
#include "shlex.hh"
#include "view_curses.hh"

using namespace std::chrono_literals;

const struct itimerval ui_periodic_timer::INTERVAL = {
    {0, std::chrono::duration_cast<std::chrono::microseconds>(350ms).count()},
    {0, std::chrono::duration_cast<std::chrono::microseconds>(350ms).count()},
};

ui_periodic_timer::ui_periodic_timer() : upt_counter(0)
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

ui_periodic_timer&
ui_periodic_timer::singleton()
{
    static ui_periodic_timer retval;

    return retval;
}

void
ui_periodic_timer::sigalrm(int sig)
{
    singleton().upt_counter += 1;
}

alerter&
alerter::singleton()
{
    static alerter retval;

    return retval;
}

struct utf_to_display_adjustment {
    int uda_origin;
    int uda_offset;

    utf_to_display_adjustment(int utf_origin, int offset)
        : uda_origin(utf_origin), uda_offset(offset){

                                  };
};

void
view_curses::awaiting_user_input()
{
    static const bool enabled = getenv("lnav_test") != nullptr;
    static const char OSC_INPUT[] = "\x1b]999;send-input\a";

    if (enabled) {
        write(STDOUT_FILENO, OSC_INPUT, sizeof(OSC_INPUT) - 1);
    }
}

void
view_curses::mvwattrline(WINDOW* window,
                         int y,
                         int x,
                         attr_line_t& al,
                         const struct line_range& lr_chars,
                         role_t base_role)
{
    auto& sa = al.get_attrs();
    auto& line = al.get_string();
    std::vector<utf_to_display_adjustment> utf_adjustments;
    int exp_offset = 0;
    std::string full_line;

    require(lr_chars.lr_end >= 0);

    auto line_width_chars = lr_chars.length();
    std::string expanded_line;

    short* fg_color = (short*) alloca(line_width_chars * sizeof(short));
    bool has_fg = false;
    short* bg_color = (short*) alloca(line_width_chars * sizeof(short));
    bool has_bg = false;
    line_range lr_bytes{lr_chars.lr_start, lr_chars.lr_end};
    int char_index = 0;

    for (size_t lpc = 0; lpc < line.size(); lpc++) {
        int exp_start_index = expanded_line.size();
        auto ch = static_cast<unsigned char>(line[lpc]);

        switch (ch) {
            case '\t':
                do {
                    expanded_line.push_back(' ');
                    char_index += 1;
                } while (expanded_line.size() % 8);
                utf_adjustments.emplace_back(
                    lpc, expanded_line.size() - exp_start_index - 1);
                break;

            case '\r':
            case '\n':
                expanded_line.push_back(' ');
                char_index += 1;
                break;

            default: {
                auto size_result = ww898::utf::utf8::char_size([&line, lpc]() {
                    return std::make_pair(line[lpc], line.length() - lpc - 1);
                });

                if (size_result.isErr()) {
                    expanded_line.push_back('?');
                } else {
                    auto offset = 1 - (int) size_result.unwrap();

                    expanded_line.push_back(ch);
                    if (offset) {
                        if (char_index < lr_chars.lr_start) {
                            lr_bytes.lr_start += abs(offset);
                        }
                        if (char_index < lr_chars.lr_end) {
                            lr_bytes.lr_end += abs(offset);
                        }
                        exp_offset += offset;
                        utf_adjustments.emplace_back(lpc, offset);
                        for (; offset && (lpc + 1) < line.size();
                             lpc++, offset++) {
                            expanded_line.push_back(line[lpc + 1]);
                        }
                    }
                }
                char_index += 1;
                break;
            }
        }
    }

    full_line = expanded_line;

    auto& vc = view_colors::singleton();
    auto text_attrs = vc.attrs_for_role(role_t::VCR_TEXT);
    short text_role_fg, text_role_bg;
    auto text_color_pair = PAIR_NUMBER(text_attrs);
    pair_content(text_color_pair, &text_role_fg, &text_role_bg);
    auto attrs = vc.attrs_for_role(base_role);
    wmove(window, y, x);
    wattron(window, attrs);
    if (lr_bytes.lr_start < (int) full_line.size()) {
        waddnstr(
            window, &full_line.c_str()[lr_bytes.lr_start], lr_bytes.length());
    }
    if (lr_bytes.lr_end > (int) full_line.size()) {
        whline(window, ' ', lr_bytes.lr_end - (full_line.size() + exp_offset));
    }
    wattroff(window, attrs);

    stable_sort(sa.begin(), sa.end());
    for (auto iter = sa.begin(); iter != sa.end(); ++iter) {
        struct line_range attr_range = iter->sa_range;

        require(attr_range.lr_start >= 0);
        require(attr_range.lr_end >= -1);

        if (!(iter->sa_type == &VC_ROLE || iter->sa_type == &VC_ROLE_FG
              || iter->sa_type == &VC_STYLE || iter->sa_type == &VC_GRAPHIC
              || iter->sa_type == &SA_LEVEL || iter->sa_type == &VC_FOREGROUND
              || iter->sa_type == &VC_BACKGROUND))
        {
            continue;
        }

        for (const auto& adj : utf_adjustments) {
            // If the UTF adjustment is in the viewport, we need to adjust this
            // attribute.
            if (adj.uda_origin < iter->sa_range.lr_start) {
                attr_range.lr_start += adj.uda_offset;
            }
        }

        if (attr_range.lr_end != -1) {
            for (const auto& adj : utf_adjustments) {
                if (adj.uda_origin < iter->sa_range.lr_end) {
                    attr_range.lr_end += adj.uda_offset;
                }
            }
        }

        if (attr_range.lr_end == -1) {
            attr_range.lr_end = lr_chars.lr_start + line_width_chars;
        }
        if (attr_range.lr_end < lr_chars.lr_start) {
            continue;
        }
        attr_range.lr_start
            = std::max(0, attr_range.lr_start - lr_chars.lr_start);
        if (attr_range.lr_start > line_width_chars) {
            continue;
        }

        attr_range.lr_end
            = std::min(line_width_chars, attr_range.lr_end - lr_chars.lr_start);

        if (iter->sa_type == &VC_FOREGROUND) {
            if (!has_fg) {
                memset(fg_color, -1, line_width_chars * sizeof(short));
            }
            short attr_fg = iter->sa_value.get<int64_t>();
            if (attr_fg == view_colors::MATCH_COLOR_SEMANTIC) {
                attr_fg = vc.color_for_ident(al.to_string_fragment(iter));
            } else if (attr_fg < 8) {
                attr_fg = vc.ansi_to_theme_color(attr_fg);
            }
            std::fill(&fg_color[attr_range.lr_start],
                      &fg_color[attr_range.lr_end],
                      attr_fg);
            has_fg = true;
            continue;
        }

        if (iter->sa_type == &VC_BACKGROUND) {
            if (!has_bg) {
                memset(bg_color, -1, line_width_chars * sizeof(short));
            }
            short attr_bg = iter->sa_value.get<int64_t>();
            if (attr_bg == view_colors::MATCH_COLOR_SEMANTIC) {
                attr_bg = vc.color_for_ident(al.to_string_fragment(iter));
            }
            std::fill(bg_color + attr_range.lr_start,
                      bg_color + attr_range.lr_end,
                      attr_bg);
            has_bg = true;
            continue;
        }

        if (attr_range.lr_end > attr_range.lr_start) {
            int awidth = attr_range.length();
            nonstd::optional<char> graphic;
            short color_pair = 0;

            if (iter->sa_type == &VC_GRAPHIC) {
                graphic = iter->sa_value.get<int64_t>();
            } else if (iter->sa_type == &VC_STYLE) {
                attrs = iter->sa_value.get<int64_t>() & ~A_COLOR;
                color_pair = PAIR_NUMBER(iter->sa_value.get<int64_t>());
            } else if (iter->sa_type == &SA_LEVEL) {
                attrs = vc.vc_level_attrs[iter->sa_value.get<int64_t>()].first;
                color_pair = PAIR_NUMBER(attrs);
                attrs = attrs & ~A_COLOR;
            } else if (iter->sa_type == &VC_ROLE) {
                attrs = vc.attrs_for_role(iter->sa_value.get<role_t>());
                color_pair = PAIR_NUMBER(attrs);
                attrs = attrs & ~A_COLOR;
            } else if (iter->sa_type == &VC_ROLE_FG) {
                short role_fg, role_bg;
                attrs = vc.attrs_for_role(iter->sa_value.get<role_t>());
                color_pair = PAIR_NUMBER(attrs);
                pair_content(color_pair, &role_fg, &role_bg);
                attrs = attrs & ~A_COLOR;
                if (!has_fg) {
                    memset(fg_color, -1, line_width_chars * sizeof(short));
                }
                std::fill(&fg_color[attr_range.lr_start],
                          &fg_color[attr_range.lr_end],
                          (short) role_fg);
                has_fg = true;
                color_pair = 0;
            }

            if (graphic || attrs || color_pair > 0) {
                int x_pos = x + attr_range.lr_start;
                int ch_width = std::min(
                    awidth, (line_width_chars - attr_range.lr_start));
                cchar_t row_ch[ch_width + 1];

                if (attrs & (A_LEFT | A_RIGHT)) {
                    short pair_fg, pair_bg;

                    pair_content(color_pair, &pair_fg, &pair_bg);
                    if (attrs & A_LEFT) {
                        pair_fg
                            = vc.color_for_ident(al.to_string_fragment(iter));
                    }
                    if (attrs & A_RIGHT) {
                        pair_bg
                            = vc.color_for_ident(al.to_string_fragment(iter));
                    }
                    color_pair = vc.ensure_color_pair(pair_fg, pair_bg);

                    attrs &= ~(A_LEFT | A_RIGHT);
                }

                if (color_pair > 0) {
                    short pair_fg, pair_bg;
                    pair_content(color_pair, &pair_fg, &pair_bg);

                    if ((pair_fg == -1 || pair_fg == text_role_fg)
                        && (pair_bg == -1 || pair_bg == text_role_bg))
                    {
                        color_pair = 0;
                    } else if (pair_bg == -1 || pair_bg == text_role_bg) {
                        if (!has_fg) {
                            memset(
                                fg_color, -1, line_width_chars * sizeof(short));
                        }
                        std::fill(&fg_color[attr_range.lr_start],
                                  &fg_color[attr_range.lr_end],
                                  (short) pair_fg);
                        has_fg = true;
                        color_pair = 0;
                    }
                }

                mvwin_wchnstr(window, y, x_pos, row_ch, ch_width);
                for (int lpc = 0; lpc < ch_width; lpc++) {
                    bool clear_rev = false;

                    if (graphic) {
                        row_ch[lpc].chars[0] = graphic.value();
                        row_ch[lpc].attr |= A_ALTCHARSET;
                    }
                    if (row_ch[lpc].attr & A_REVERSE && attrs & A_REVERSE) {
                        clear_rev = true;
                    }
                    if (color_pair > 0) {
                        row_ch[lpc].attr
                            = attrs | (row_ch[lpc].attr & ~A_COLOR);
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
            memset(fg_color, -1, line_width_chars * sizeof(short));
        }
        if (!has_bg) {
            memset(bg_color, -1, line_width_chars * sizeof(short));
        }

        int ch_width = lr_chars.length();
        cchar_t row_ch[ch_width + 1];

        mvwin_wchnstr(window, y, x, row_ch, ch_width);
        for (int lpc = 0; lpc < ch_width; lpc++) {
            if (fg_color[lpc] == -1 && bg_color[lpc] == -1) {
                continue;
            }

            auto cur_pair = PAIR_NUMBER(row_ch[lpc].attr);
            short cur_fg, cur_bg;
            pair_content(cur_pair, &cur_fg, &cur_bg);
            if (fg_color[lpc] == -1) {
                fg_color[lpc] = cur_fg;
            }
            if (bg_color[lpc] == -1) {
                bg_color[lpc] = cur_bg;
            }

            int color_pair = vc.ensure_color_pair(fg_color[lpc], bg_color[lpc]);

            row_ch[lpc].attr = row_ch[lpc].attr & ~A_COLOR;
#    ifdef NCURSES_EXT_COLORS
            row_ch[lpc].ext_color = color_pair;
#    else
            row_ch[lpc].attr |= COLOR_PAIR(color_pair);
#    endif
        }
        mvwadd_wchnstr(window, y, x, row_ch, ch_width);
    }
#endif
}

view_colors&
view_colors::singleton()
{
    static view_colors s_vc;

    return s_vc;
}

view_colors::view_colors() : vc_dyn_pairs(0)
{
    size_t color_index = 0;
    for (int z = 0; z < 6; z++) {
        for (int x = 1; x < 6; x += 2) {
            for (int y = 1; y < 6; y += 2) {
                short fg = 16 + x + (y * 6) + (z * 6 * 6);

                this->vc_highlight_colors[color_index++] = fg;
            }
        }
    }
}

bool view_colors::initialized = false;

static std::string COLOR_NAMES[] = {
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
    void reload_config(error_reporter& reporter) override
    {
        if (!view_colors::initialized) {
            return;
        }

        auto& vc = view_colors::singleton();

        for (const auto& pair : lnav_config.lc_ui_theme_defs) {
            vc.init_roles(pair.second, reporter);
        }

        auto iter = lnav_config.lc_ui_theme_defs.find(lnav_config.lc_ui_theme);

        if (iter == lnav_config.lc_ui_theme_defs.end()) {
            auto theme_names
                = lnav_config.lc_ui_theme_defs | lnav::itertools::first();

            reporter(&lnav_config.lc_ui_theme,
                     lnav::console::user_message::error(
                         attr_line_t("unknown theme -- ")
                             .append_quoted(lnav_config.lc_ui_theme))
                         .with_help(attr_line_t("The available themes are: ")
                                        .join(theme_names, ", ")));

            vc.init_roles(lnav_config.lc_ui_theme_defs["default"], reporter);
            return;
        }

        if (view_colors::initialized) {
            vc.init_roles(iter->second, reporter);
        }
    }
};

static color_listener _COLOR_LISTENER;
term_color_palette* view_colors::vc_active_palette;

void
view_colors::init()
{
    vc_active_palette = ansi_colors();
    if (has_colors()) {
        static const int ansi_colors_to_curses[] = {
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
                if (fg == 0 && bg == 0) {
                    continue;
                }

                if (lnav_config.lc_ui_default_colors
                    && ansi_colors_to_curses[fg] == COLOR_WHITE
                    && ansi_colors_to_curses[bg] == COLOR_BLACK)
                {
                    init_pair(ansi_color_pair_index(fg, bg), -1, -1);
                } else {
                    auto curs_bg = ansi_colors_to_curses[bg];

                    if (lnav_config.lc_ui_default_colors
                        && curs_bg == COLOR_BLACK) {
                        curs_bg = -1;
                    }
                    init_pair(ansi_color_pair_index(fg, bg),
                              ansi_colors_to_curses[fg],
                              curs_bg);
                }
            }
        }
        if (COLORS >= 256) {
            vc_active_palette = xterm_colors();
        }
    }

    log_debug("COLOR_PAIRS = %d", COLOR_PAIRS);
    singleton().vc_dyn_pairs.set_max_size(COLOR_PAIRS);

    initialized = true;

    {
        auto reporter = [](const void*, const lnav::console::user_message&) {

        };

        _COLOR_LISTENER.reload_config(reporter);
    }
}

inline attr_t
attr_for_colors(int& pair_base, short fg, short bg)
{
    if (fg == -1) {
        fg = COLOR_WHITE;
    }
    if (bg == -1) {
        bg = COLOR_BLACK;
    }
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

    require(pair_base < COLOR_PAIRS);

    int pair = pair_base;
    pair_base += 1;

    if (view_colors::initialized) {
        init_pair(pair, fg, bg);
    }

    auto retval = COLOR_PAIR(pair);

    if (fg == view_colors::MATCH_COLOR_SEMANTIC) {
        retval |= A_LEFT;
    }
    if (bg == view_colors::MATCH_COLOR_SEMANTIC) {
        retval |= A_RIGHT;
    }

    return retval;
}

std::pair<attr_t, attr_t>
view_colors::to_attrs(int& pair_base,
                      const lnav_theme& lt,
                      const style_config& sc,
                      const style_config& fallback_sc,
                      lnav_config_listener::error_reporter& reporter)
{
    std::string fg1, bg1, fg_color, bg_color;

    fg1 = sc.sc_color;
    if (fg1.empty()) {
        fg1 = fallback_sc.sc_color;
    }
    bg1 = sc.sc_background_color;
    if (bg1.empty()) {
        bg1 = fallback_sc.sc_background_color;
    }
    shlex(fg1).eval(fg_color, lt.lt_vars);
    shlex(bg1).eval(bg_color, lt.lt_vars);

    auto fg = styling::color_unit::from_str(fg_color).unwrapOrElse(
        [&](const auto& msg) {
            reporter(
                &sc.sc_color,
                lnav::console::user_message::error(
                    attr_line_t("invalid color -- ").append_quoted(sc.sc_color))
                    .with_reason(msg));
            return styling::color_unit::make_empty();
        });
    auto bg = styling::color_unit::from_str(bg_color).unwrapOrElse(
        [&](const auto& msg) {
            reporter(&sc.sc_background_color,
                     lnav::console::user_message::error(
                         attr_line_t("invalid background color -- ")
                             .append_quoted(sc.sc_background_color))
                         .with_reason(msg));
            return styling::color_unit::make_empty();
        });

    attr_t retval1 = attr_for_colors(
        pair_base, this->match_color(fg), this->match_color(bg));
    attr_t retval2 = 0;

    if (sc.sc_underline) {
        retval1 |= A_UNDERLINE;
        retval2 |= A_UNDERLINE;
    }
    if (sc.sc_bold) {
        retval1 |= A_BOLD;
        retval2 |= A_BOLD;
    }

    return {retval1, retval2};
}

void
view_colors::init_roles(const lnav_theme& lt,
                        lnav_config_listener::error_reporter& reporter)
{
    int color_pair_base = VC_ANSI_END + 1;
    rgb_color fg, bg;
    std::string err;

    if (COLORS == 256) {
        const auto& ident_sc = lt.lt_style_identifier;

        if (!ident_sc.sc_background_color.empty()) {
            std::string bg_color;

            shlex(ident_sc.sc_background_color).eval(bg_color, lt.lt_vars);
            auto rgb_bg = rgb_color::from_str(bg_color).unwrapOrElse(
                [&](const auto& msg) {
                    reporter(
                        &ident_sc.sc_background_color,
                        lnav::console::user_message::error(
                            attr_line_t("invalid background color -- ")
                                .append_quoted(ident_sc.sc_background_color))
                            .with_reason(msg));
                    return rgb_color{};
                });
        }
    } else {
        color_pair_base = VC_ANSI_END + HI_COLOR_COUNT;
    }

    /* Setup the mappings from roles to actual colors. */
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_TEXT)]
        = this->to_attrs(
            color_pair_base, lt, lt.lt_style_text, lt.lt_style_text, reporter);

    {
        int pnum = PAIR_NUMBER(
            this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_TEXT)]
                .first);
        short text_fg, text_bg;

        pair_content(pnum, &text_fg, &text_bg);
        for (int ansi_fg = 0; ansi_fg < 8; ansi_fg++) {
            for (int ansi_bg = 0; ansi_bg < 8; ansi_bg++) {
                if (ansi_fg == 0 && ansi_bg == 0) {
                    continue;
                }

                auto fg_iter = lt.lt_vars.find(COLOR_NAMES[ansi_fg]);
                auto bg_iter = lt.lt_vars.find(COLOR_NAMES[ansi_bg]);
                auto fg_str = fg_iter == lt.lt_vars.end() ? ""
                                                          : fg_iter->second;
                auto bg_str = bg_iter == lt.lt_vars.end() ? ""
                                                          : bg_iter->second;

                auto rgb_fg = rgb_color::from_str(fg_str).unwrapOrElse(
                    [&](const auto& msg) {
                        reporter(&fg_str,
                                 lnav::console::user_message::error(
                                     attr_line_t("invalid color -- ")
                                         .append_quoted(fg_str))
                                     .with_reason(msg));
                        return rgb_color{};
                    });
                auto rgb_bg = rgb_color::from_str(bg_str).unwrapOrElse(
                    [&](const auto& msg) {
                        reporter(&bg_str,
                                 lnav::console::user_message::error(
                                     attr_line_t("invalid background color -- ")
                                         .append_quoted(bg_str))
                                     .with_reason(msg));
                        return rgb_color{};
                    });

                short fg = vc_active_palette->match_color(lab_color(rgb_fg));
                short bg = vc_active_palette->match_color(lab_color(rgb_bg));

                if (rgb_fg.empty()) {
                    fg = ansi_fg;
                }
                if (rgb_bg.empty()) {
                    bg = ansi_bg;
                }

                this->vc_ansi_to_theme[ansi_fg] = fg;
                if (lnav_config.lc_ui_default_colors && bg == COLOR_BLACK) {
                    bg = -1;
                    if (fg == COLOR_WHITE) {
                        fg = -1;
                    }
                }
                init_pair(ansi_color_pair_index(ansi_fg, ansi_bg), fg, bg);
            }
        }
    }
    if (lnav_config.lc_ui_dim_text) {
        this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_TEXT)].first
            |= A_DIM;
        this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_TEXT)]
            .second
            |= A_DIM;
    }
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_SEARCH)]
        = std::make_pair(A_REVERSE, A_REVERSE);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_IDENTIFIER)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_identifier,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_OK)]
        = this->to_attrs(
            color_pair_base, lt, lt.lt_style_ok, lt.lt_style_text, reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_ERROR)]
        = this->to_attrs(
            color_pair_base, lt, lt.lt_style_error, lt.lt_style_text, reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_WARNING)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_warning,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_ALT_ROW)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_alt_text,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_HIDDEN)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_hidden,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_ADJUSTED_TIME)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_adjusted_time,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_SKEWED_TIME)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_skewed_time,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_OFFSET_TIME)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_offset_time,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_INVALID_MSG)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_invalid_msg,
                         lt.lt_style_text,
                         reporter);

    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_STATUS)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_status,
                         lt.lt_style_status,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_WARN_STATUS)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_warn_status,
                         lt.lt_style_status,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_ALERT_STATUS)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_alert_status,
                         lt.lt_style_status,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_ACTIVE_STATUS)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_active_status,
                         lt.lt_style_status,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_ACTIVE_STATUS2)]
        = std::make_pair(this->vc_role_colors[lnav::enums::to_underlying(
                                                  role_t::VCR_ACTIVE_STATUS)]
                                 .first
                             | A_BOLD,
                         this->vc_role_colors[lnav::enums::to_underlying(
                                                  role_t::VCR_ACTIVE_STATUS)]
                                 .second
                             | A_BOLD);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_STATUS_TITLE)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_status_title,
                         lt.lt_style_status,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(
        role_t::VCR_STATUS_SUBTITLE)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_status_subtitle,
                         lt.lt_style_status,
                         reporter);

    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_STATUS_HOTKEY)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_status_hotkey,
                         lt.lt_style_status,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(
        role_t::VCR_STATUS_TITLE_HOTKEY)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_status_title_hotkey,
                         lt.lt_style_status,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(
        role_t::VCR_STATUS_DISABLED_TITLE)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_status_disabled_title,
                         lt.lt_style_status,
                         reporter);

    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_H1)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_header[0],
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_H2)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_header[1],
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_H3)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_header[2],
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_H4)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_header[3],
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_H5)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_header[4],
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_H6)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_header[5],
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_HR)]
        = this->to_attrs(
            color_pair_base, lt, lt.lt_style_hr, lt.lt_style_text, reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_HYPERLINK)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_hyperlink,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_LIST_GLYPH)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_list_glyph,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_BREADCRUMB)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_breadcrumb,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_TABLE_BORDER)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_table_border,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_TABLE_HEADER)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_table_header,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_QUOTE_BORDER)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_quote_border,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_QUOTED_TEXT)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_quoted_text,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(
        role_t::VCR_FOOTNOTE_BORDER)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_footnote_border,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_FOOTNOTE_TEXT)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_footnote_text,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_SNIPPET_BORDER)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_snippet_border,
                         lt.lt_style_text,
                         reporter);

    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status_subtitle.sc_background_color;
        stitch_sc.sc_background_color
            = lt.lt_style_status_title.sc_background_color;
        this->vc_role_colors[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_TITLE_TO_SUB)]
            = this->to_attrs(
                color_pair_base, lt, stitch_sc, lt.lt_style_status, reporter);
    }
    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status_title.sc_background_color;
        stitch_sc.sc_background_color
            = lt.lt_style_status_subtitle.sc_background_color;
        this->vc_role_colors[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_SUB_TO_TITLE)]
            = this->to_attrs(
                color_pair_base, lt, stitch_sc, lt.lt_style_status, reporter);
    }

    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status.sc_background_color;
        stitch_sc.sc_background_color
            = lt.lt_style_status_subtitle.sc_background_color;
        this->vc_role_colors[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_SUB_TO_NORMAL)]
            = this->to_attrs(
                color_pair_base, lt, stitch_sc, lt.lt_style_status, reporter);
    }
    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status_subtitle.sc_background_color;
        stitch_sc.sc_background_color = lt.lt_style_status.sc_background_color;
        this->vc_role_colors[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_NORMAL_TO_SUB)]
            = this->to_attrs(
                color_pair_base, lt, stitch_sc, lt.lt_style_status, reporter);
    }

    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status.sc_background_color;
        stitch_sc.sc_background_color
            = lt.lt_style_status_title.sc_background_color;
        this->vc_role_colors[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_TITLE_TO_NORMAL)]
            = this->to_attrs(
                color_pair_base, lt, stitch_sc, lt.lt_style_status, reporter);
    }
    {
        style_config stitch_sc;

        stitch_sc.sc_color = lt.lt_style_status_title.sc_background_color;
        stitch_sc.sc_background_color = lt.lt_style_status.sc_background_color;
        this->vc_role_colors[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_NORMAL_TO_TITLE)]
            = this->to_attrs(
                color_pair_base, lt, stitch_sc, lt.lt_style_status, reporter);
    }

    this->vc_role_colors[lnav::enums::to_underlying(
        role_t::VCR_INACTIVE_STATUS)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_inactive_status,
                         lt.lt_style_status,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(
        role_t::VCR_INACTIVE_ALERT_STATUS)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_inactive_alert_status,
                         lt.lt_style_alert_status,
                         reporter);

    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_POPUP)]
        = this->to_attrs(
            color_pair_base, lt, lt.lt_style_popup, lt.lt_style_text, reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_FOCUSED)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_focused,
                         lt.lt_style_focused,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(
        role_t::VCR_DISABLED_FOCUSED)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_disabled_focused,
                         lt.lt_style_disabled_focused,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_COLOR_HINT)]
        = std::make_pair(COLOR_PAIR(color_pair_base),
                         COLOR_PAIR(color_pair_base + 1));
    color_pair_base += 2;

    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_SCROLLBAR)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_scrollbar,
                         lt.lt_style_status,
                         reporter);
    {
        style_config bar_sc;

        bar_sc.sc_color = lt.lt_style_error.sc_color;
        bar_sc.sc_background_color = lt.lt_style_scrollbar.sc_background_color;
        this->vc_role_colors[lnav::enums::to_underlying(
            role_t::VCR_SCROLLBAR_ERROR)]
            = this->to_attrs(color_pair_base,
                             lt,
                             bar_sc,
                             lt.lt_style_alert_status,
                             reporter);
    }
    {
        style_config bar_sc;

        bar_sc.sc_color = lt.lt_style_warning.sc_color;
        bar_sc.sc_background_color = lt.lt_style_scrollbar.sc_background_color;
        this->vc_role_colors[lnav::enums::to_underlying(
            role_t::VCR_SCROLLBAR_WARNING)]
            = this->to_attrs(
                color_pair_base, lt, bar_sc, lt.lt_style_warn_status, reporter);
    }

    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_QUOTED_CODE)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_quoted_code,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_CODE_BORDER)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_code_border,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_KEYWORD)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_keyword,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_STRING)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_string,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_COMMENT)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_comment,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_DOC_DIRECTIVE)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_doc_directive,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_VARIABLE)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_variable,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_SYMBOL)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_symbol,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_NUMBER)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_number,
                         lt.lt_style_text,
                         reporter);

    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_RE_SPECIAL)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_re_special,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_RE_REPEAT)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_re_repeat,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_FILE)]
        = this->to_attrs(
            color_pair_base, lt, lt.lt_style_file, lt.lt_style_text, reporter);

    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_DIFF_DELETE)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_diff_delete,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_DIFF_ADD)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_diff_add,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_DIFF_SECTION)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_diff_section,
                         lt.lt_style_text,
                         reporter);

    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_LOW_THRESHOLD)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_low_threshold,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_MED_THRESHOLD)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_med_threshold,
                         lt.lt_style_text,
                         reporter);
    this->vc_role_colors[lnav::enums::to_underlying(role_t::VCR_HIGH_THRESHOLD)]
        = this->to_attrs(color_pair_base,
                         lt,
                         lt.lt_style_high_threshold,
                         lt.lt_style_text,
                         reporter);

    for (auto level = static_cast<log_level_t>(LEVEL_UNKNOWN + 1);
         level < LEVEL__MAX;
         level = static_cast<log_level_t>(level + 1))
    {
        auto level_iter = lt.lt_level_styles.find(level);

        if (level_iter == lt.lt_level_styles.end()) {
            this->vc_level_attrs[level] = this->to_attrs(color_pair_base,
                                                         lt,
                                                         lt.lt_style_text,
                                                         lt.lt_style_text,
                                                         reporter);
        } else {
            this->vc_level_attrs[level] = this->to_attrs(color_pair_base,
                                                         lt,
                                                         level_iter->second,
                                                         lt.lt_style_text,
                                                         reporter);
        }
    }

    if (initialized && this->vc_color_pair_end == 0) {
        this->vc_color_pair_end = color_pair_base + 1;
    }
    this->vc_dyn_pairs.clear();
}

int
view_colors::ensure_color_pair(short fg, short bg)
{
    require(fg >= -100);
    require(bg >= -100);

    auto index_pair = std::make_pair(fg, bg);
    auto existing = this->vc_dyn_pairs.get(index_pair);

    if (existing) {
        auto retval = existing.value().dp_color_pair;

        return retval;
    }

    short def_pair = PAIR_NUMBER(this->attrs_for_role(role_t::VCR_TEXT));
    short def_fg, def_bg;

    pair_content(def_pair, &def_fg, &def_bg);

    int new_pair = this->vc_color_pair_end + this->vc_dyn_pairs.size();
    auto retval = PAIR_NUMBER(attr_for_colors(
        new_pair, fg == -1 ? def_fg : fg, bg == -1 ? def_bg : bg));

    if (initialized) {
        struct dyn_pair dp = {(int) retval};

        this->vc_dyn_pairs.put(index_pair, dp);
    }

    return retval;
}

int
view_colors::ensure_color_pair(const styling::color_unit& rgb_fg,
                               const styling::color_unit& rgb_bg)
{
    auto fg = this->match_color(rgb_fg);
    auto bg = this->match_color(rgb_bg);

    return this->ensure_color_pair(fg, bg);
}

short
view_colors::match_color(const styling::color_unit& color) const
{
    return color.cu_value.match(
        [](styling::semantic) { return MATCH_COLOR_SEMANTIC; },
        [](const rgb_color& color) {
            if (color.empty()) {
                return MATCH_COLOR_DEFAULT;
            }

            return vc_active_palette->match_color(lab_color(color));
        });
}

int
view_colors::color_for_ident(const char* str, size_t len) const
{
    unsigned long index = crc32(1, (const Bytef*) str, len);
    int retval;

    if (COLORS >= 256) {
        if (str[0] == '#' && (len == 4 || len == 7)) {
            auto fg_res
                = styling::color_unit::from_str(string_fragment(str, 0, len));
            if (fg_res.isOk()) {
                return this->match_color(fg_res.unwrap());
            }
        }

        unsigned long offset = index % HI_COLOR_COUNT;
        retval = this->vc_highlight_colors[offset];
    } else {
        retval = -1;
    }

    return retval;
}

attr_t
view_colors::attrs_for_ident(const char* str, size_t len)
{
    auto retval = this->attrs_for_role(role_t::VCR_IDENTIFIER);

    if (retval & (A_LEFT | A_RIGHT)) {
        auto color_pair = PAIR_NUMBER(retval);
        short pair_fg, pair_bg;

        pair_content(color_pair, &pair_fg, &pair_bg);
        if (retval & A_LEFT) {
            pair_fg = this->color_for_ident(str, len);
        }
        if (retval & A_RIGHT) {
            pair_bg = this->color_for_ident(str, len);
        }
        color_pair = this->ensure_color_pair(pair_fg, pair_bg);
        retval &= ~(A_COLOR | A_LEFT | A_RIGHT);
        retval |= COLOR_PAIR(color_pair);
    }

    return retval;
}

lab_color::lab_color(const rgb_color& rgb)
{
    double r = rgb.rc_r / 255.0, g = rgb.rc_g / 255.0, b = rgb.rc_b / 255.0, x,
           y, z;

    r = (r > 0.04045) ? pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
    g = (g > 0.04045) ? pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
    b = (b > 0.04045) ? pow((b + 0.055) / 1.055, 2.4) : b / 12.92;

    x = (r * 0.4124 + g * 0.3576 + b * 0.1805) / 0.95047;
    y = (r * 0.2126 + g * 0.7152 + b * 0.0722) / 1.00000;
    z = (r * 0.0193 + g * 0.1192 + b * 0.9505) / 1.08883;

    x = (x > 0.008856) ? pow(x, 1.0 / 3.0) : (7.787 * x) + 16.0 / 116.0;
    y = (y > 0.008856) ? pow(y, 1.0 / 3.0) : (7.787 * y) + 16.0 / 116.0;
    z = (z > 0.008856) ? pow(z, 1.0 / 3.0) : (7.787 * z) + 16.0 / 116.0;

    this->lc_l = (116.0 * y) - 16;
    this->lc_a = 500.0 * (x - y);
    this->lc_b = 200.0 * (y - z);
}

double
lab_color::deltaE(const lab_color& other) const
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
    double i = deltaLKlsl * deltaLKlsl + deltaCkcsc * deltaCkcsc
        + deltaHkhsh * deltaHkhsh;
    return i < 0.0 ? 0.0 : sqrt(i);
}

bool
lab_color::operator<(const lab_color& rhs) const
{
    if (lc_l < rhs.lc_l)
        return true;
    if (rhs.lc_l < lc_l)
        return false;
    if (lc_a < rhs.lc_a)
        return true;
    if (rhs.lc_a < lc_a)
        return false;
    return lc_b < rhs.lc_b;
}

bool
lab_color::operator>(const lab_color& rhs) const
{
    return rhs < *this;
}

bool
lab_color::operator<=(const lab_color& rhs) const
{
    return !(rhs < *this);
}

bool
lab_color::operator>=(const lab_color& rhs) const
{
    return !(*this < rhs);
}

bool
lab_color::operator==(const lab_color& rhs) const
{
    return lc_l == rhs.lc_l && lc_a == rhs.lc_a && lc_b == rhs.lc_b;
}

bool
lab_color::operator!=(const lab_color& rhs) const
{
    return !(rhs == *this);
}

string_attr_pair
view_colors::roles::file()
{
    return VC_ROLE.value(role_t::VCR_FILE);
}
