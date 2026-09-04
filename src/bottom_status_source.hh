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

#ifndef lnav_bottom_status_source_hh
#define lnav_bottom_status_source_hh

#include <string>

#include "grep_proc.hh"
#include "statusview_curses.hh"
#include "textview_curses.hh"

class bottom_status_source
    : public status_data_source
    , public grep_proc_control {
public:
    typedef enum {
        BSF_LINE_NUMBER,
        BSF_PERCENT,
        BSF_HITS,
        BSF_SEARCH_TERM,
        BSF_LOADING,
        BSF_HELP,

        BSF__MAX
    } field_t;

    bottom_status_source();

    status_field& get_field(field_t id) { return this->bss_fields[id]; }

    void set_prompt(const std::string& prompt)
    {
        this->bss_prompt.set_value(prompt);
    }

    void grep_error(const std::string& msg) override
    {
        this->bss_error.set_value(msg);
    }

    void set_error(const attr_line_t& msg)
    {
        this->bss_error.set_value(msg);
    }

    size_t statusview_fields() override;

    status_field& statusview_value_for_field(int field) override;

    void update_line_number(listview_curses* lc);

    void update_search_term(textview_curses& tc);

    void update_percent(listview_curses* lc);

    bool update_marks(listview_curses* lc);

    bool update_hits(textview_curses* tc);

    void update_loading(file_off_t off,
                        file_ssize_t total,
                        const char* term = "Loading");

private:
    status_field bss_prompt{1024, role_t::VCR_STATUS};
    status_field bss_error{1024, role_t::VCR_ALERT_STATUS};
    status_field bss_line_error{1024, role_t::VCR_ALERT_STATUS};
    status_field bss_fields[BSF__MAX];
    int bss_hit_spinner{0};
    int bss_load_percent{0};
    bool bss_paused{false};
    /** Which search the hit count and the term beside it are reporting on. */
    enum class search_report_t {
        /** The interactive search, which is on its own in the view. */
        interactive,
        /** The search that has been focused, named or interactive. */
        focused,
        /** Every enabled search, which is what n/N walks by default. */
        all,
    };

    /**
     * Kept in one place so that the count and the term it is labelled with
     * cannot end up describing different searches.
     */
    static search_report_t search_report_for(textview_curses& tc);

    /**
     * What the search term field was last rendered for.  Neither of these can
     * be relied on to change through the commands that set them -- a search
     * can be deleted from the filter panel, and creating the first named
     * search widens what n/N covers without touching the focus -- so the
     * field is re-rendered whenever either no longer matches the view.
     */
    search_report_t bss_search_report{search_report_t::interactive};
    std::optional<size_t> bss_focused_search_slot;
};

#endif
