/**
 * Copyright (c) 2020, Timothy Stack
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
 * @file view_helpers.hh
 */

#ifndef lnav_view_helpers_hh
#define lnav_view_helpers_hh

#include "bookmarks.hh"
#include "help_text.hh"
#include "listview_curses.hh"
#include "logfile_fwd.hh"
#include "vis_line.hh"
#include "xterm_mouse.hh"

class textview_curses;
class hist_source2;
class logfile_sub_source;

/** The different views available. */
typedef enum {
    LNV_LOG,
    LNV_TEXT,
    LNV_HELP,
    LNV_HISTOGRAM,
    LNV_DB,
    LNV_SCHEMA,
    LNV_PRETTY,
    LNV_SPECTRO,
    LNV_TIMELINE,

    LNV__MAX
} lnav_view_t;

/** The command modes that are available while viewing a file. */
enum class ln_mode_t : int {
    PAGING,
    BREADCRUMBS,
    FILTER,
    FILES,
    FILE_DETAILS,
    SPECTRO_DETAILS,
    SEARCH_SPECTRO_DETAILS,
    COMMAND,
    SEARCH,
    SEARCH_FILTERS,
    SEARCH_FILES,
    CAPTURE,
    SQL,
    EXEC,
    USER,
    BUSY,
};

extern const char* lnav_view_strings[LNV__MAX + 1];
extern const char* lnav_view_titles[LNV__MAX];

std::optional<lnav_view_t> view_from_string(const char* name);

bool ensure_view(textview_curses* expected_tc);
bool ensure_view(lnav_view_t expected);
bool toggle_view(textview_curses* toggle_tc);
bool handle_winch();
void layout_views();
void update_hits(textview_curses* tc);
void clear_preview();
void set_view_mode(ln_mode_t mode);

std::optional<vis_line_t> next_cluster(
    std::optional<vis_line_t> (bookmark_vector<vis_line_t>::*f)(vis_line_t)
        const,
    const bookmark_type_t* bt,
    vis_line_t top);
bool moveto_cluster(std::optional<vis_line_t> (bookmark_vector<vis_line_t>::*f)(
                        vis_line_t) const,
                    const bookmark_type_t* bt,
                    vis_line_t top);
vis_line_t search_forward_from(textview_curses* tc);
textview_curses* get_textview_for_mode(ln_mode_t mode);

class lnav_behavior : public mouse_behavior {
public:
    void mouse_event(int button, bool release, int x, int y) override;

    view_curses* lb_last_view{nullptr};
    struct mouse_event lb_last_event;
    struct mouse_event lb_last_release_event;
};

#endif
