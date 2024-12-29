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
 * @file view_curses.hh
 */

#ifndef view_curses_hh
#define view_curses_hh

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <notcurses/notcurses.h>
#include <signal.h>
#include <stdint.h>
#include <sys/time.h>

#include "base/attr_line.hh"
#include "base/enum_util.hh"
#include "base/lnav_log.hh"
#include "base/log_level_enum.hh"
#include "base/result.h"
#include "base/string_attr_type.hh"
#include "config.h"
#include "lnav_config_fwd.hh"
#include "styling.hh"

class view_curses;

/**
 * An RAII class that initializes and deinitializes curses.
 */
class screen_curses : public log_crash_recoverer {
public:
    void log_crash_recover() override
    {
        if (this->sc_notcurses != nullptr) {
            notcurses_stop(this->sc_notcurses);
            this->sc_notcurses = nullptr;
        }
    }

    static Result<screen_curses, std::string> create(
        const notcurses_options& options);

    ~screen_curses() override
    {
        if (this->sc_notcurses != nullptr) {
            notcurses_stop(this->sc_notcurses);
            this->sc_notcurses = nullptr;
        }
    }

    screen_curses(screen_curses&& other) noexcept;

    screen_curses(const screen_curses&) = delete;

    screen_curses& operator=(screen_curses&& other) noexcept
    {
        this->sc_notcurses = std::exchange(other.sc_notcurses, nullptr);
        return *this;
    }

    notcurses* get_notcurses() const { return this->sc_notcurses; }

    ncplane* get_std_plane() const
    {
        return notcurses_stdplane(this->sc_notcurses);
    }

private:
    explicit screen_curses(notcurses* nc) : sc_notcurses(nc) {}

    notcurses* sc_notcurses;
};

template<typename T>
class action_broadcaster : public std::vector<std::function<void(T*)>> {
public:
    void operator()(T* t)
    {
        for (auto& func : *this) {
            func(t);
        }
    }
};

class ui_periodic_timer {
public:
    static const itimerval INTERVAL;

    static ui_periodic_timer& singleton();

    bool time_to_update(sig_atomic_t& counter) const
    {
        if (this->upt_counter != counter) {
            counter = this->upt_counter;
            return true;
        }
        return false;
    }

    void start_fade(sig_atomic_t& counter, size_t decay) const
    {
        counter = this->upt_counter + decay;
    }

    int fade_diff(const sig_atomic_t& counter) const
    {
        if (this->upt_counter >= counter) {
            return 0;
        }
        return counter - this->upt_counter;
    }

private:
    ui_periodic_timer();

    static void sigalrm(int sig);

    volatile sig_atomic_t upt_counter;
};

class alerter {
public:
    static alerter& singleton();

    void enabled(bool enable) { this->a_enabled = enable; }

    bool chime(std::string msg);

    void new_input(const ncinput& ch)
    {
        if (this->a_last_input != ch.id) {
            this->a_do_flash = true;
        }
        this->a_last_input = ch.id;
    }

private:
    bool a_enabled{true};
    bool a_do_flash{true};
    uint32_t a_last_input{0};
};

/**
 * Singleton used to manage the colorspace.
 */
class view_colors {
public:
    static constexpr unsigned long HI_COLOR_COUNT = 6 * 3 * 3;

    /** @return A reference to the singleton. */
    static view_colors& singleton();

    static uint64_t to_channels(const text_attrs& ta);

    view_colors(const view_colors&) = delete;
    view_colors(view_colors&&) = delete;
    view_colors& operator=(const view_colors&) = delete;
    view_colors& operator=(view_colors&&) = delete;

    /**
     * Performs curses-specific initialization.  The other methods can be
     * called before this method, but the returned attributes cannot be used
     * with curses code until this method is called.
     */
    static void init(notcurses* nc);

    void init_roles(const lnav_theme& lt,
                    lnav_config_listener::error_reporter& reporter);

    /**
     * @param role The role to retrieve character attributes for.
     * @return The attributes to use for the given role.
     */
    text_attrs attrs_for_role(role_t role, bool selected = false) const
    {
        if (role == role_t::VCR_NONE) {
            return {};
        }

        require(role > role_t::VCR_NONE);
        require(role < role_t::VCR__MAX);

        return selected
            ? this->vc_role_attrs[lnav::enums::to_underlying(role)].ra_reverse
            : this->vc_role_attrs[lnav::enums::to_underlying(role)].ra_normal;
    }

    styling::color_unit color_for_ident(const char* str, size_t len) const;

    styling::color_unit color_for_ident(const string_fragment& sf) const
    {
        return this->color_for_ident(sf.data(), sf.length());
    }

    text_attrs attrs_for_ident(const char* str, size_t len) const;

    text_attrs attrs_for_ident(intern_string_t str) const
    {
        return this->attrs_for_ident(str.get(), str.size());
    }

    text_attrs attrs_for_ident(const std::string& str) const
    {
        return this->attrs_for_ident(str.c_str(), str.length());
    }

    text_attrs attrs_for_level(log_level_t level) const
    {
        return this->vc_level_attrs[level].ra_normal;
    }

    styling::color_unit ansi_to_theme_color(styling::color_unit ansi_fg) const;

    std::unordered_map<std::string, string_attr_pair> vc_class_to_role;

    block_elem_t wchar_for_icon(ui_icon_t ic) const;

    styling::color_unit match_color(styling::color_unit cu) const;

    static bool initialized;
    static term_color_palette* vc_active_palette;

private:
    /** Private constructor that initializes the member fields. */
    view_colors();

    struct role_attrs {
        text_attrs ra_normal;
        text_attrs ra_reverse;
        intern_string_t ra_class_name;
    };

    role_attrs to_attrs(const lnav_theme& lt,
                        const positioned_property<style_config>& sc,
                        lnav_config_listener::error_reporter& reporter);

    role_attrs& get_role_attrs(const role_t role)
    {
        return this->vc_role_attrs[lnav::enums::to_underlying(role)];
    }

    notcurses* vc_notcurses{nullptr};
    role_attrs vc_level_attrs[LEVEL__MAX];

    /** Map of role IDs to attribute values. */
    role_attrs vc_role_attrs[lnav::enums::to_underlying(role_t::VCR__MAX)];
    styling::color_unit vc_ansi_to_theme[8];
    short vc_highlight_colors[HI_COLOR_COUNT];
    block_elem_t vc_icons[lnav::enums::to_underlying(ui_icon_t::hidden) + 1];
};

enum class mouse_button_t {
    BUTTON_LEFT,
    BUTTON_MIDDLE,
    BUTTON_RIGHT,

    BUTTON_SCROLL_UP,
    BUTTON_SCROLL_DOWN,
};

enum class mouse_button_state_t {
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_DRAGGED,
    BUTTON_STATE_RELEASED,
    BUTTON_STATE_DOUBLE_CLICK,
};

struct mouse_event {
    mouse_event(mouse_button_t button = mouse_button_t::BUTTON_LEFT,
                mouse_button_state_t state
                = mouse_button_state_t::BUTTON_STATE_PRESSED,
                uint8_t mods = 0,
                int x = -1,
                int y = -1)
        : me_button(button), me_state(state), me_modifiers(mods), me_x(x),
          me_y(y)
    {
    }

    enum class modifier_t : uint8_t {
        shift = 4,
        meta = 8,
        ctrl = 16,
    };

    bool is_modifier_pressed(modifier_t mod) const
    {
        return this->me_modifiers & lnav::enums::to_underlying(mod);
    }

    bool is_click_in(mouse_button_t button, int x_start, int x_end) const;

    bool is_click_in(mouse_button_t button, line_range lr) const
    {
        return this->is_click_in(button, lr.lr_start, lr.lr_end);
    }

    bool is_press_in(mouse_button_t button, line_range lr) const;

    bool is_drag_in(mouse_button_t button, line_range lr) const;
    bool is_double_click_in(mouse_button_t button, line_range lr) const;

    mouse_button_t me_button;
    mouse_button_state_t me_state;
    uint8_t me_modifiers;
    timeval me_time{};
    int me_x;
    int me_y;
    int me_press_x{-1};
    int me_press_y{-1};
};

/**
 * Interface for "view" classes that will update a curses(3) display.
 */
class view_curses {
public:
    virtual ~view_curses() = default;

    /**
     * Update the curses display.
     */
    virtual bool do_update()
    {
        bool retval = false;

        this->vc_needs_update = false;

        if (!this->vc_visible) {
            return retval;
        }

        for (auto* child : this->vc_children) {
            retval = child->do_update() || retval;
        }
        return retval;
    }

    virtual bool handle_mouse(mouse_event& me);

    virtual bool contains(int x, int y) const;

    void set_needs_update()
    {
        this->vc_needs_update = true;
        for (auto* child : this->vc_children) {
            child->set_needs_update();
        }
    }

    bool get_needs_update() const { return this->vc_needs_update; }

    view_curses& add_child_view(view_curses* child)
    {
        this->vc_children.push_back(child);

        return *this;
    }

    void set_default_role(role_t role) { this->vc_default_role = role; }

    void set_visible(bool value) { this->vc_visible = value; }

    bool is_visible() const { return this->vc_visible; }

    /**
     * Set the Y position of this view on the display.  A value greater than
     * zero is considered to be an absolute size.  A value less than zero makes
     * the position relative to the bottom of the enclosing window.
     *
     * @param y The Y position of the cursor on the curses display.
     */
    void set_y(int y)
    {
        if (y != this->vc_y) {
            this->vc_y = y;
            this->set_needs_update();
        }
    }

    int get_y() const { return this->vc_y; }

    void set_x(unsigned int x)
    {
        if (x != this->vc_x) {
            this->vc_x = x;
            this->set_needs_update();
        }
    }

    unsigned int get_x() const { return this->vc_x; }

    void set_width(long width) { this->vc_width = width; }

    long get_width() const { return this->vc_width; }

    static void awaiting_user_input();

    struct mvwattrline_result {
        size_t mr_chars_out{0};
        size_t mr_bytes_remaining{0};
        string_fragment mr_selected_text;
    };

    static mvwattrline_result mvwattrline(ncplane* window,
                                          int y,
                                          int x,
                                          attr_line_t& al,
                                          const struct line_range& lr,
                                          role_t base_role = role_t::VCR_TEXT);

    bool vc_enabled{true};

protected:
    bool vc_visible{true};
    /** Flag to indicate if a display update is needed. */
    bool vc_needs_update{true};
    unsigned int vc_x{0};
    int vc_y{0};
    long vc_width{0};
    std::vector<view_curses*> vc_children;
    role_t vc_default_role{role_t::VCR_TEXT};
    view_curses* vc_last_drag_child{nullptr};
};

template<class T>
class view_stack : public view_curses {
public:
    using iterator = typename std::vector<T*>::iterator;

    std::optional<T*> top()
    {
        if (this->vs_views.empty()) {
            return std::nullopt;
        }
        return this->vs_views.back();
    }

    bool do_update() override
    {
        if (!this->vc_visible) {
            return false;
        }

        bool retval = false;
        this->top() | [this, &retval](T* vc) {
            if (this->vc_needs_update) {
                vc->set_needs_update();
            }
            retval = vc->do_update();
        };

        retval = view_curses::do_update() || retval;

        this->vc_needs_update = false;
        return retval;
    }

    void push_back(T* view)
    {
        this->vs_views.push_back(view);
        if (this->vs_change_handler) {
            this->vs_change_handler(view);
        }
        this->set_needs_update();
    }

    void pop_back()
    {
        this->vs_views.pop_back();
        if (!this->vs_views.empty() && this->vs_change_handler) {
            this->vs_change_handler(this->vs_views.back());
        }
        this->set_needs_update();
    }

    iterator find(T* view) const
    {
        return std::find(this->begin(), this->end(), view);
    }

    iterator begin() { return this->vs_views.begin(); }

    iterator end() { return this->vs_views.end(); }

    size_t size() const { return this->vs_views.size(); }

    bool empty() const { return this->vs_views.empty(); }

    std::function<void(T*)> vs_change_handler;

private:
    std::vector<T*> vs_views;
};

#endif
