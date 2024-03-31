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
#include "base/from_trait.hh"
#include "base/itertools.hh"
#include "base/lnav_log.hh"
#include "config.h"
#include "lnav_config.hh"
#include "shlex.hh"
#include "view_curses.hh"

#if defined HAVE_NCURSESW_CURSES_H
#    include <ncursesw/term.h>
#elif defined HAVE_NCURSESW_H
#    include <term.h>
#elif defined HAVE_NCURSES_CURSES_H
#    include <ncurses/term.h>
#elif defined HAVE_NCURSES_H
#    include <term.h>
#elif defined HAVE_CURSES_H
#    include <term.h>
#else
#    error "SysV or X/Open-compatible Curses header file required"
#endif

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

bool
alerter::chime(std::string msg)
{
    if (!this->a_enabled) {
        return true;
    }

    bool retval = this->a_do_flash;
    if (this->a_do_flash) {
        log_warning("chime message: %s", msg.c_str());
        ::flash();
    }
    this->a_do_flash = false;
    return retval;
}

struct utf_to_display_adjustment {
    int uda_origin;
    int uda_offset;

    utf_to_display_adjustment(int utf_origin, int offset)
        : uda_origin(utf_origin), uda_offset(offset)
    {
    }
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

size_t
view_curses::mvwattrline(WINDOW* window,
                         int y,
                         const int x,
                         attr_line_t& al,
                         const struct line_range& lr_chars,
                         role_t base_role)
{
    auto& sa = al.get_attrs();
    auto& line = al.get_string();
    std::vector<utf_to_display_adjustment> utf_adjustments;
    std::string full_line;

    require(lr_chars.lr_end >= 0);

    auto line_width_chars = lr_chars.length();
    std::string expanded_line;
    line_range lr_bytes;
    int char_index = 0;

    for (size_t lpc = 0; lpc < line.size();) {
        int exp_start_index = expanded_line.size();
        auto ch = static_cast<unsigned char>(line[lpc]);

        if (char_index == lr_chars.lr_start) {
            lr_bytes.lr_start = exp_start_index;
        } else if (char_index == lr_chars.lr_end) {
            lr_bytes.lr_end = exp_start_index;
        }

        switch (ch) {
            case '\t': {
                do {
                    expanded_line.push_back(' ');
                    char_index += 1;
                } while (expanded_line.size() % 8);
                utf_adjustments.emplace_back(
                    lpc, expanded_line.size() - exp_start_index - 1);
                lpc += 1;
                break;
            }

            case '\x1b':
                expanded_line.append("\u238b");
                utf_adjustments.emplace_back(lpc, -1);
                char_index += 1;
                lpc += 1;
                break;

            case '\b':
                expanded_line.append("\u232b");
                utf_adjustments.emplace_back(lpc, -1);
                char_index += 1;
                lpc += 1;
                break;

            case '\r':
            case '\n':
                expanded_line.push_back(' ');
                char_index += 1;
                lpc += 1;
                break;

            default: {
                auto exp_read_start = expanded_line.size();
                auto lpc_start = lpc;
                auto read_res
                    = ww898::utf::utf8::read([&line, &expanded_line, &lpc]() {
                          auto ch = line[lpc++];
                          expanded_line.push_back(ch);
                          return ch;
                      });

                if (read_res.isErr()) {
                    log_trace(
                        "error:%d:%d:%s", y, x + lpc, read_res.unwrapErr());
                    expanded_line.resize(exp_read_start);
                    expanded_line.push_back('?');
                    char_index += 1;
                    lpc = lpc_start + 1;
                } else {
                    if (lpc > (lpc_start + 1)) {
                        utf_adjustments.emplace_back(lpc_start,
                                                     1 - (lpc - lpc_start));
                    }
                    auto wch = read_res.unwrap();
                    char_index += wcwidth(wch);
                }
                break;
            }
        }
    }
    if (lr_bytes.lr_start == -1) {
        lr_bytes.lr_start = expanded_line.size();
    }
    if (lr_bytes.lr_end == -1) {
        lr_bytes.lr_end = expanded_line.size();
    }
    size_t retval = expanded_line.size() - lr_bytes.lr_end;

    full_line = expanded_line;

    auto& vc = view_colors::singleton();
    auto text_role_attrs = vc.attrs_for_role(role_t::VCR_TEXT);
    auto base_attrs = vc.attrs_for_role(base_role);
    wmove(window, y, x);
    wattr_set(
        window,
        base_attrs.ta_attrs,
        vc.ensure_color_pair(base_attrs.ta_fg_color, base_attrs.ta_bg_color),
        nullptr);
    if (lr_bytes.lr_start < (int) full_line.size()) {
        waddnstr(
            window, &full_line.c_str()[lr_bytes.lr_start], lr_bytes.length());
    }
    if (lr_chars.lr_end > char_index) {
        whline(window, ' ', lr_chars.lr_end - char_index);
    }

    cchar_t row_ch[line_width_chars + 1];
    short fg_color[line_width_chars + 1];
    short bg_color[line_width_chars + 1];
    memset(fg_color, -1, sizeof(fg_color));
    memset(bg_color, -1, sizeof(bg_color));

    mvwin_wchnstr(window, y, x, row_ch, line_width_chars);
    std::stable_sort(sa.begin(), sa.end());
    for (auto iter = sa.cbegin(); iter != sa.cend(); ++iter) {
        auto attr_range = iter->sa_range;

        require(attr_range.lr_start >= 0);
        require(attr_range.lr_end >= -1);

        if (!(iter->sa_type == &VC_ROLE || iter->sa_type == &VC_ROLE_FG
              || iter->sa_type == &VC_STYLE || iter->sa_type == &VC_GRAPHIC
              || iter->sa_type == &SA_LEVEL || iter->sa_type == &VC_FOREGROUND
              || iter->sa_type == &VC_BACKGROUND
              || iter->sa_type == &VC_BLOCK_ELEM))
        {
            continue;
        }

        if (attr_range.lr_unit == line_range::unit::bytes) {
            for (const auto& adj : utf_adjustments) {
                // If the UTF adjustment is in the viewport, we need to adjust
                // this attribute.
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
            short attr_fg = iter->sa_value.get<int64_t>();
            if (attr_fg == view_colors::MATCH_COLOR_SEMANTIC) {
                attr_fg = vc.color_for_ident(al.to_string_fragment(iter))
                              .value_or(view_colors::MATCH_COLOR_DEFAULT);
            } else if (attr_fg < 8) {
                attr_fg = vc.ansi_to_theme_color(attr_fg);
            }
            std::fill(&fg_color[attr_range.lr_start],
                      &fg_color[attr_range.lr_end],
                      attr_fg);
            continue;
        }

        if (iter->sa_type == &VC_BACKGROUND) {
            short attr_bg = iter->sa_value.get<int64_t>();
            if (attr_bg == view_colors::MATCH_COLOR_SEMANTIC) {
                attr_bg = vc.color_for_ident(al.to_string_fragment(iter))
                              .value_or(view_colors::MATCH_COLOR_DEFAULT);
            }
            std::fill(bg_color + attr_range.lr_start,
                      bg_color + attr_range.lr_end,
                      attr_bg);
            continue;
        }

        if (attr_range.lr_start < attr_range.lr_end) {
            auto attrs = text_attrs{};
            nonstd::optional<char> graphic;
            nonstd::optional<wchar_t> block_elem;

            if (iter->sa_type == &VC_GRAPHIC) {
                graphic = iter->sa_value.get<int64_t>();
                attrs = text_attrs{};
            } else if (iter->sa_type == &VC_BLOCK_ELEM) {
                auto be = iter->sa_value.get<block_elem_t>();
                block_elem = be.value;
                attrs = vc.attrs_for_role(be.role);
            } else if (iter->sa_type == &VC_STYLE) {
                attrs = iter->sa_value.get<text_attrs>();
            } else if (iter->sa_type == &SA_LEVEL) {
                attrs = vc.attrs_for_level(
                    (log_level_t) iter->sa_value.get<int64_t>());
            } else if (iter->sa_type == &VC_ROLE) {
                auto role = iter->sa_value.get<role_t>();
                attrs = vc.attrs_for_role(role);
            } else if (iter->sa_type == &VC_ROLE_FG) {
                auto role_attrs
                    = vc.attrs_for_role(iter->sa_value.get<role_t>());
                attrs.ta_fg_color = role_attrs.ta_fg_color;
            }

            if (graphic || block_elem || !attrs.empty()) {
                if (attrs.ta_attrs & (A_LEFT | A_RIGHT)) {
                    if (attrs.ta_attrs & A_LEFT) {
                        attrs.ta_fg_color
                            = vc.color_for_ident(al.to_string_fragment(iter));
                    }
                    if (attrs.ta_attrs & A_RIGHT) {
                        attrs.ta_bg_color
                            = vc.color_for_ident(al.to_string_fragment(iter));
                    }
                    attrs.ta_attrs &= ~(A_LEFT | A_RIGHT);
                }

                if (attrs.ta_fg_color) {
                    std::fill(&fg_color[attr_range.lr_start],
                              &fg_color[attr_range.lr_end],
                              attrs.ta_fg_color.value());
                }
                if (attrs.ta_bg_color) {
                    std::fill(&bg_color[attr_range.lr_start],
                              &bg_color[attr_range.lr_end],
                              attrs.ta_bg_color.value());
                }

                for (int lpc = attr_range.lr_start;
                     lpc < attr_range.lr_end && lpc < line_width_chars;
                     lpc++)
                {
                    bool clear_rev = false;

                    if (graphic) {
                        row_ch[lpc].chars[0] = graphic.value();
                        row_ch[lpc].attr |= A_ALTCHARSET;
                    }
                    if (block_elem) {
                        row_ch[lpc].chars[0] = block_elem.value();
                    }
                    if (row_ch[lpc].attr & A_REVERSE
                        && attrs.ta_attrs & A_REVERSE)
                    {
                        clear_rev = true;
                    }
                    row_ch[lpc].attr |= attrs.ta_attrs;
                    if (clear_rev) {
                        row_ch[lpc].attr &= ~A_REVERSE;
                    }
                }
            }
        }
    }

    for (int lpc = 0; lpc < line_width_chars; lpc++) {
        if (fg_color[lpc] == -1 && bg_color[lpc] == -1) {
            continue;
        }
#ifdef NCURSES_EXT_COLORS
        auto cur_pair = row_ch[lpc].ext_color;
#else
        auto cur_pair = PAIR_NUMBER(row_ch[lpc].attr);
#endif
        short cur_fg, cur_bg;
        pair_content(cur_pair, &cur_fg, &cur_bg);

        if (fg_color[lpc] >= 0
            && fg_color[lpc] < view_colors::vc_active_palette->tc_palette.size()
            && bg_color[lpc] == -1 && base_attrs.ta_bg_color.value_or(0) >= 0
            && base_attrs.ta_bg_color.value_or(0)
                < view_colors::vc_active_palette->tc_palette.size())
        {
            const auto& fg_color_info
                = view_colors::vc_active_palette->tc_palette.at(fg_color[lpc]);
            const auto& bg_color_info
                = view_colors::vc_active_palette->tc_palette.at(
                    base_attrs.ta_bg_color.value_or(0));

            if (!fg_color_info.xc_lab_color.sufficient_contrast(
                    bg_color_info.xc_lab_color))
            {
                auto adjusted_color = bg_color_info.xc_lab_color;
                adjusted_color.lc_l -= 40.0;
                auto new_bg = view_colors::vc_active_palette->match_color(
                    adjusted_color);
                for (int lpc2 = lpc; lpc2 < line_width_chars; lpc2++) {
                    if (fg_color[lpc2] == fg_color[lpc] && bg_color[lpc2] == -1)
                    {
                        bg_color[lpc2] = new_bg;
                    }
                }
            }
        }

        if (fg_color[lpc] == -1) {
            fg_color[lpc] = cur_fg;
        }
        if (bg_color[lpc] == -1) {
            bg_color[lpc] = cur_bg;
        }

        int color_pair = vc.ensure_color_pair(fg_color[lpc], bg_color[lpc]);

        row_ch[lpc].attr = row_ch[lpc].attr & ~A_COLOR;
#ifdef NCURSES_EXT_COLORS
        row_ch[lpc].ext_color = color_pair;
#else
        row_ch[lpc].attr |= COLOR_PAIR(color_pair);
#endif
    }
    mvwadd_wchnstr(window, y, x, row_ch, line_width_chars);

    return retval;
}

constexpr short view_colors::MATCH_COLOR_DEFAULT;
constexpr short view_colors::MATCH_COLOR_SEMANTIC;

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

static const std::string COLOR_NAMES[] = {
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
    color_listener() : lnav_config_listener(__FILE__) {}

    void reload_config(error_reporter& reporter) override
    {
        if (!view_colors::initialized) {
            view_colors::vc_active_palette = ansi_colors();
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
view_colors::init(bool headless)
{
    vc_active_palette = ansi_colors();
    if (!headless && has_colors()) {
        start_color();

        if (lnav_config.lc_ui_default_colors) {
            use_default_colors();
        }
        if (COLORS >= 256) {
            vc_active_palette = xterm_colors();
        }
    }

    log_debug("COLOR_PAIRS = %d", COLOR_PAIRS);

    initialized = true;

    {
        auto reporter
            = [](const void*, const lnav::console::user_message& um) {};

        _COLOR_LISTENER.reload_config(reporter);
    }
}

inline text_attrs
attr_for_colors(nonstd::optional<short> fg, nonstd::optional<short> bg)
{
    if (fg && fg.value() == -1) {
        fg = COLOR_WHITE;
    }
    if (bg && bg.value() == -1) {
        bg = COLOR_BLACK;
    }

    if (lnav_config.lc_ui_default_colors) {
        if (fg && fg.value() == COLOR_WHITE) {
            fg = -1;
        }
        if (bg && bg.value() == COLOR_BLACK) {
            bg = -1;
        }
    }

    text_attrs retval;

    if (fg && fg.value() == view_colors::MATCH_COLOR_SEMANTIC) {
        retval.ta_attrs |= A_LEFT;
    } else {
        retval.ta_fg_color = fg;
    }
    if (bg && bg.value() == view_colors::MATCH_COLOR_SEMANTIC) {
        retval.ta_attrs |= A_RIGHT;
    } else {
        retval.ta_bg_color = bg;
    }

    return retval;
}

view_colors::role_attrs
view_colors::to_attrs(const lnav_theme& lt,
                      const positioned_property<style_config>& pp_sc,
                      lnav_config_listener::error_reporter& reporter)
{
    const auto& sc = pp_sc.pp_value;
    std::string fg1, bg1, fg_color, bg_color;
    intern_string_t role_class;

    if (pp_sc.pp_path.empty()) {
#if 0
        // too slow to do this now
        reporter(&sc.sc_color, lnav::console::user_message::warning(""));
#endif
    } else {
        auto role_class_path
            = ghc::filesystem::path(pp_sc.pp_path.to_string()).parent_path();
        auto inner = role_class_path.filename().string();
        auto outer = role_class_path.parent_path().filename().string();

        role_class = intern_string::lookup(
            fmt::format(FMT_STRING("-lnav_{}_{}"), outer, inner));
    }

    fg1 = sc.sc_color;
    bg1 = sc.sc_background_color;
    std::map<std::string, scoped_value_t> vars;
    for (const auto& vpair : lt.lt_vars) {
        vars[vpair.first] = vpair.second;
    }
    shlex(fg1).eval(fg_color, scoped_resolver{&vars});
    shlex(bg1).eval(bg_color, scoped_resolver{&vars});

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

    text_attrs retval1
        = attr_for_colors(this->match_color(fg), this->match_color(bg));
    text_attrs retval2;

    if (sc.sc_underline) {
        retval1.ta_attrs |= A_UNDERLINE;
        retval2.ta_attrs |= A_UNDERLINE;
    }
    if (sc.sc_bold) {
        retval1.ta_attrs |= A_BOLD;
        retval2.ta_attrs |= A_BOLD;
    }

    return {retval1, retval2, role_class};
}

void
view_colors::init_roles(const lnav_theme& lt,
                        lnav_config_listener::error_reporter& reporter)
{
    rgb_color fg, bg;
    std::string err;

    /* Setup the mappings from roles to actual colors. */
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_TEXT)]
        = this->to_attrs(lt, lt.lt_style_text, reporter);

    for (int ansi_fg = 0; ansi_fg < 8; ansi_fg++) {
        for (int ansi_bg = 0; ansi_bg < 8; ansi_bg++) {
            if (ansi_fg == 0 && ansi_bg == 0) {
                continue;
            }

            auto fg_iter = lt.lt_vars.find(COLOR_NAMES[ansi_fg]);
            auto bg_iter = lt.lt_vars.find(COLOR_NAMES[ansi_bg]);
            auto fg_str = fg_iter == lt.lt_vars.end() ? "" : fg_iter->second;
            auto bg_str = bg_iter == lt.lt_vars.end() ? "" : bg_iter->second;

            auto rgb_fg = from<rgb_color>(string_fragment::from_str(fg_str))
                              .unwrapOrElse([&](const auto& msg) {
                                  reporter(&fg_str,
                                           lnav::console::user_message::error(
                                               attr_line_t("invalid color -- ")
                                                   .append_quoted(fg_str))
                                               .with_reason(msg));
                                  return rgb_color{};
                              });
            auto rgb_bg
                = from<rgb_color>(string_fragment::from_str(bg_str))
                      .unwrapOrElse([&](const auto& msg) {
                          reporter(
                              &bg_str,
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
        }
    }

    if (lnav_config.lc_ui_dim_text) {
        this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_TEXT)]
            .ra_normal.ta_attrs
            |= A_DIM;
        this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_TEXT)]
            .ra_reverse.ta_attrs
            |= A_DIM;
    }
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_SEARCH)]
        = role_attrs{text_attrs{A_REVERSE}, text_attrs{A_REVERSE}};
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_SEARCH)]
        .ra_class_name
        = intern_string::lookup("-lnav_styles_search");
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_IDENTIFIER)]
        = this->to_attrs(lt, lt.lt_style_identifier, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_OK)]
        = this->to_attrs(lt, lt.lt_style_ok, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_INFO)]
        = this->to_attrs(lt, lt.lt_style_info, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_ERROR)]
        = this->to_attrs(lt, lt.lt_style_error, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_WARNING)]
        = this->to_attrs(lt, lt.lt_style_warning, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_ALT_ROW)]
        = this->to_attrs(lt, lt.lt_style_alt_text, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_HIDDEN)]
        = this->to_attrs(lt, lt.lt_style_hidden, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_CURSOR_LINE)]
        = this->to_attrs(lt, lt.lt_style_cursor_line, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(
        role_t::VCR_DISABLED_CURSOR_LINE)]
        = this->to_attrs(lt, lt.lt_style_disabled_cursor_line, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_ADJUSTED_TIME)]
        = this->to_attrs(lt, lt.lt_style_adjusted_time, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_SKEWED_TIME)]
        = this->to_attrs(lt, lt.lt_style_skewed_time, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_OFFSET_TIME)]
        = this->to_attrs(lt, lt.lt_style_offset_time, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_FILE_OFFSET)]
        = this->to_attrs(lt, lt.lt_style_file_offset, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_INVALID_MSG)]
        = this->to_attrs(lt, lt.lt_style_invalid_msg, reporter);

    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_STATUS)]
        = this->to_attrs(lt, lt.lt_style_status, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_WARN_STATUS)]
        = this->to_attrs(lt, lt.lt_style_warn_status, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_ALERT_STATUS)]
        = this->to_attrs(lt, lt.lt_style_alert_status, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_ACTIVE_STATUS)]
        = this->to_attrs(lt, lt.lt_style_active_status, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_ACTIVE_STATUS2)]
        = role_attrs{
            this->vc_role_attrs[lnav::enums::to_underlying(
                                    role_t::VCR_ACTIVE_STATUS)]
                .ra_normal,
            this->vc_role_attrs[lnav::enums::to_underlying(
                                    role_t::VCR_ACTIVE_STATUS)]
                .ra_reverse,
        };
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_ACTIVE_STATUS2)]
        .ra_normal.ta_attrs
        |= A_BOLD;
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_ACTIVE_STATUS2)]
        .ra_reverse.ta_attrs
        |= A_BOLD;
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_STATUS_TITLE)]
        = this->to_attrs(lt, lt.lt_style_status_title, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_STATUS_SUBTITLE)]
        = this->to_attrs(lt, lt.lt_style_status_subtitle, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_STATUS_INFO)]
        = this->to_attrs(lt, lt.lt_style_status_info, reporter);

    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_STATUS_HOTKEY)]
        = this->to_attrs(lt, lt.lt_style_status_hotkey, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(
        role_t::VCR_STATUS_TITLE_HOTKEY)]
        = this->to_attrs(lt, lt.lt_style_status_title_hotkey, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(
        role_t::VCR_STATUS_DISABLED_TITLE)]
        = this->to_attrs(lt, lt.lt_style_status_disabled_title, reporter);

    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_H1)]
        = this->to_attrs(lt, lt.lt_style_header[0], reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_H2)]
        = this->to_attrs(lt, lt.lt_style_header[1], reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_H3)]
        = this->to_attrs(lt, lt.lt_style_header[2], reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_H4)]
        = this->to_attrs(lt, lt.lt_style_header[3], reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_H5)]
        = this->to_attrs(lt, lt.lt_style_header[4], reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_H6)]
        = this->to_attrs(lt, lt.lt_style_header[5], reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_HR)]
        = this->to_attrs(lt, lt.lt_style_hr, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_HYPERLINK)]
        = this->to_attrs(lt, lt.lt_style_hyperlink, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_LIST_GLYPH)]
        = this->to_attrs(lt, lt.lt_style_list_glyph, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_BREADCRUMB)]
        = this->to_attrs(lt, lt.lt_style_breadcrumb, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_TABLE_BORDER)]
        = this->to_attrs(lt, lt.lt_style_table_border, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_TABLE_HEADER)]
        = this->to_attrs(lt, lt.lt_style_table_header, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_QUOTE_BORDER)]
        = this->to_attrs(lt, lt.lt_style_quote_border, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_QUOTED_TEXT)]
        = this->to_attrs(lt, lt.lt_style_quoted_text, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_FOOTNOTE_BORDER)]
        = this->to_attrs(lt, lt.lt_style_footnote_border, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_FOOTNOTE_TEXT)]
        = this->to_attrs(lt, lt.lt_style_footnote_text, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_SNIPPET_BORDER)]
        = this->to_attrs(lt, lt.lt_style_snippet_border, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_INDENT_GUIDE)]
        = this->to_attrs(lt, lt.lt_style_indent_guide, reporter);

    {
        positioned_property<style_config> stitch_sc;

        stitch_sc.pp_value.sc_color
            = lt.lt_style_status_subtitle.pp_value.sc_background_color;
        stitch_sc.pp_value.sc_background_color
            = lt.lt_style_status_title.pp_value.sc_background_color;
        this->vc_role_attrs[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_TITLE_TO_SUB)]
            = this->to_attrs(lt, stitch_sc, reporter);
    }
    {
        positioned_property<style_config> stitch_sc;

        stitch_sc.pp_value.sc_color
            = lt.lt_style_status_title.pp_value.sc_background_color;
        stitch_sc.pp_value.sc_background_color
            = lt.lt_style_status_subtitle.pp_value.sc_background_color;
        this->vc_role_attrs[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_SUB_TO_TITLE)]
            = this->to_attrs(lt, stitch_sc, reporter);
    }

    {
        positioned_property<style_config> stitch_sc;

        stitch_sc.pp_value.sc_color
            = lt.lt_style_status.pp_value.sc_background_color;
        stitch_sc.pp_value.sc_background_color
            = lt.lt_style_status_subtitle.pp_value.sc_background_color;
        this->vc_role_attrs[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_SUB_TO_NORMAL)]
            = this->to_attrs(lt, stitch_sc, reporter);
    }
    {
        positioned_property<style_config> stitch_sc;

        stitch_sc.pp_value.sc_color
            = lt.lt_style_status_subtitle.pp_value.sc_background_color;
        stitch_sc.pp_value.sc_background_color
            = lt.lt_style_status.pp_value.sc_background_color;
        this->vc_role_attrs[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_NORMAL_TO_SUB)]
            = this->to_attrs(lt, stitch_sc, reporter);
    }

    {
        positioned_property<style_config> stitch_sc;

        stitch_sc.pp_value.sc_color
            = lt.lt_style_status.pp_value.sc_background_color;
        stitch_sc.pp_value.sc_background_color
            = lt.lt_style_status_title.pp_value.sc_background_color;
        this->vc_role_attrs[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_TITLE_TO_NORMAL)]
            = this->to_attrs(lt, stitch_sc, reporter);
    }
    {
        positioned_property<style_config> stitch_sc;

        stitch_sc.pp_value.sc_color
            = lt.lt_style_status_title.pp_value.sc_background_color;
        stitch_sc.pp_value.sc_background_color
            = lt.lt_style_status.pp_value.sc_background_color;
        this->vc_role_attrs[lnav::enums::to_underlying(
            role_t::VCR_STATUS_STITCH_NORMAL_TO_TITLE)]
            = this->to_attrs(lt, stitch_sc, reporter);
    }

    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_INACTIVE_STATUS)]
        = this->to_attrs(lt, lt.lt_style_inactive_status, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(
        role_t::VCR_INACTIVE_ALERT_STATUS)]
        = this->to_attrs(lt, lt.lt_style_inactive_alert_status, reporter);

    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_POPUP)]
        = this->to_attrs(lt, lt.lt_style_popup, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_FOCUSED)]
        = this->to_attrs(lt, lt.lt_style_focused, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(
        role_t::VCR_DISABLED_FOCUSED)]
        = this->to_attrs(lt, lt.lt_style_disabled_focused, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_SCROLLBAR)]
        = this->to_attrs(lt, lt.lt_style_scrollbar, reporter);
    {
        positioned_property<style_config> bar_sc;

        bar_sc.pp_value.sc_color = lt.lt_style_error.pp_value.sc_color;
        bar_sc.pp_value.sc_background_color
            = lt.lt_style_scrollbar.pp_value.sc_background_color;
        this->vc_role_attrs[lnav::enums::to_underlying(
            role_t::VCR_SCROLLBAR_ERROR)]
            = this->to_attrs(lt, bar_sc, reporter);
    }
    {
        positioned_property<style_config> bar_sc;

        bar_sc.pp_value.sc_color = lt.lt_style_warning.pp_value.sc_color;
        bar_sc.pp_value.sc_background_color
            = lt.lt_style_scrollbar.pp_value.sc_background_color;
        this->vc_role_attrs[lnav::enums::to_underlying(
            role_t::VCR_SCROLLBAR_WARNING)]
            = this->to_attrs(lt, bar_sc, reporter);
    }

    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_INLINE_CODE)]
        = this->to_attrs(lt, lt.lt_style_inline_code, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_QUOTED_CODE)]
        = this->to_attrs(lt, lt.lt_style_quoted_code, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_CODE_BORDER)]
        = this->to_attrs(lt, lt.lt_style_code_border, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_KEYWORD)]
        = this->to_attrs(lt, lt.lt_style_keyword, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_STRING)]
        = this->to_attrs(lt, lt.lt_style_string, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_COMMENT)]
        = this->to_attrs(lt, lt.lt_style_comment, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_DOC_DIRECTIVE)]
        = this->to_attrs(lt, lt.lt_style_doc_directive, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_VARIABLE)]
        = this->to_attrs(lt, lt.lt_style_variable, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_SYMBOL)]
        = this->to_attrs(lt, lt.lt_style_symbol, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_NULL)]
        = this->to_attrs(lt, lt.lt_style_null, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_ASCII_CTRL)]
        = this->to_attrs(lt, lt.lt_style_ascii_ctrl, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_NON_ASCII)]
        = this->to_attrs(lt, lt.lt_style_non_ascii, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_NUMBER)]
        = this->to_attrs(lt, lt.lt_style_number, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_FUNCTION)]
        = this->to_attrs(lt, lt.lt_style_function, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_TYPE)]
        = this->to_attrs(lt, lt.lt_style_type, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_SEP_REF_ACC)]
        = this->to_attrs(lt, lt.lt_style_sep_ref_acc, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_SUGGESTION)]
        = this->to_attrs(lt, lt.lt_style_suggestion, reporter);

    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_RE_SPECIAL)]
        = this->to_attrs(lt, lt.lt_style_re_special, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_RE_REPEAT)]
        = this->to_attrs(lt, lt.lt_style_re_repeat, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_FILE)]
        = this->to_attrs(lt, lt.lt_style_file, reporter);

    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_DIFF_DELETE)]
        = this->to_attrs(lt, lt.lt_style_diff_delete, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_DIFF_ADD)]
        = this->to_attrs(lt, lt.lt_style_diff_add, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_DIFF_SECTION)]
        = this->to_attrs(lt, lt.lt_style_diff_section, reporter);

    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_LOW_THRESHOLD)]
        = this->to_attrs(lt, lt.lt_style_low_threshold, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_MED_THRESHOLD)]
        = this->to_attrs(lt, lt.lt_style_med_threshold, reporter);
    this->vc_role_attrs[lnav::enums::to_underlying(role_t::VCR_HIGH_THRESHOLD)]
        = this->to_attrs(lt, lt.lt_style_high_threshold, reporter);

    for (auto level = static_cast<log_level_t>(LEVEL_UNKNOWN + 1);
         level < LEVEL__MAX;
         level = static_cast<log_level_t>(level + 1))
    {
        auto level_iter = lt.lt_level_styles.find(level);

        if (level_iter == lt.lt_level_styles.end()) {
            this->vc_level_attrs[level]
                = role_attrs{text_attrs{}, text_attrs{}};
        } else {
            this->vc_level_attrs[level]
                = this->to_attrs(lt, level_iter->second, reporter);
        }
    }

    if (initialized && this->vc_color_pair_end == 0) {
        this->vc_color_pair_end = 1;
    }
    this->vc_dyn_pairs.clear();

    for (int32_t role_index = 0;
         role_index < lnav::enums::to_underlying(role_t::VCR__MAX);
         role_index++)
    {
        const auto& ra = this->vc_role_attrs[role_index];
        if (ra.ra_class_name.empty()) {
            continue;
        }

        this->vc_class_to_role[ra.ra_class_name.to_string()]
            = VC_ROLE.value(role_t(role_index));
    }
    for (int level_index = 0; level_index < LEVEL__MAX; level_index++) {
        const auto& ra = this->vc_level_attrs[level_index];
        if (ra.ra_class_name.empty()) {
            continue;
        }

        this->vc_class_to_role[ra.ra_class_name.to_string()]
            = SA_LEVEL.value(level_index);
    }
}

int
view_colors::ensure_color_pair(short fg, short bg)
{
    require(fg >= -100);
    require(bg >= -100);

    if (fg >= COLOR_BLACK && fg <= COLOR_WHITE) {
        fg = this->ansi_to_theme_color(fg);
    }
    if (bg >= COLOR_BLACK && bg <= COLOR_WHITE) {
        bg = this->ansi_to_theme_color(bg);
    }

    auto index_pair = std::make_pair(fg, bg);
    auto existing = this->vc_dyn_pairs.get(index_pair);

    if (existing) {
        auto retval = existing.value().dp_color_pair;

        return retval;
    }

    auto def_attrs = this->attrs_for_role(role_t::VCR_TEXT);
    int retval = this->vc_color_pair_end + this->vc_dyn_pairs.size();
    auto attrs
        = attr_for_colors(fg == -1 ? def_attrs.ta_fg_color.value_or(-1) : fg,
                          bg == -1 ? def_attrs.ta_bg_color.value_or(-1) : bg);
    init_pair(retval, attrs.ta_fg_color.value(), attrs.ta_bg_color.value());

    if (initialized) {
        struct dyn_pair dp = {retval};

        this->vc_dyn_pairs.set_max_size(256 - this->vc_color_pair_end);
        this->vc_dyn_pairs.put(index_pair, dp);
    }

    return retval;
}

int
view_colors::ensure_color_pair(nonstd::optional<short> fg,
                               nonstd::optional<short> bg)
{
    return this->ensure_color_pair(fg.value_or(-1), bg.value_or(-1));
}

int
view_colors::ensure_color_pair(const styling::color_unit& rgb_fg,
                               const styling::color_unit& rgb_bg)
{
    auto fg = this->match_color(rgb_fg);
    auto bg = this->match_color(rgb_bg);

    return this->ensure_color_pair(fg, bg);
}

nonstd::optional<short>
view_colors::match_color(const styling::color_unit& color) const
{
    return color.cu_value.match(
        [](styling::semantic) -> nonstd::optional<short> {
            return MATCH_COLOR_SEMANTIC;
        },
        [](const rgb_color& color) -> nonstd::optional<short> {
            if (color.empty()) {
                return nonstd::nullopt;
            }

            return vc_active_palette->match_color(lab_color(color));
        });
}

nonstd::optional<short>
view_colors::color_for_ident(const char* str, size_t len) const
{
    auto index = crc32(1, (const Bytef*) str, len);
    int retval;

    if (COLORS >= 256) {
        if (str[0] == '#' && (len == 4 || len == 7)) {
            auto fg_res
                = styling::color_unit::from_str(string_fragment(str, 0, len));
            if (fg_res.isOk()) {
                return this->match_color(fg_res.unwrap());
            }
        }

        auto offset = index % HI_COLOR_COUNT;
        retval = this->vc_highlight_colors[offset];
    } else {
        retval = -1;
    }

    return retval;
}

text_attrs
view_colors::attrs_for_ident(const char* str, size_t len) const
{
    auto retval = this->attrs_for_role(role_t::VCR_IDENTIFIER);

    if (retval.ta_attrs & (A_LEFT | A_RIGHT)) {
        if (retval.ta_attrs & A_LEFT) {
            retval.ta_fg_color = this->color_for_ident(str, len);
        }
        if (retval.ta_attrs & A_RIGHT) {
            retval.ta_bg_color = this->color_for_ident(str, len);
        }
        retval.ta_attrs &= ~(A_COLOR | A_LEFT | A_RIGHT);
    }

    return retval;
}

Result<screen_curses, std::string>
screen_curses::create()
{
    int errret = 0;
    if (setupterm(nullptr, STDIN_FILENO, &errret) == ERR) {
        switch (errret) {
            case 1:
                return Err(std::string("the terminal is a hardcopy, da fuq?!"));
            case 0:
                return Err(
                    fmt::format(FMT_STRING("the TERM environment variable is "
                                           "set to an unknown value: {}"),
                                getenv("TERM")));
            case -1:
                return Err(
                    std::string("the terminfo database could not be found"));
            default:
                return Err(std::string("setupterm() failed unexpectedly"));
        }
    }

    newterm(nullptr, stdout, stdin);

    return Ok(screen_curses{stdscr});
}
