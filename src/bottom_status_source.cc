/**
 * Copyright (c) 2019, Timothy Stack
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

#include "bottom_status_source.hh"

#include "base/snippet_highlighters.hh"
#include "config.h"

using namespace lnav::roles::literals;

bottom_status_source::bottom_status_source()
{
    this->bss_fields[BSF_LINE_NUMBER].set_min_width(10);
    this->bss_fields[BSF_LINE_NUMBER].set_share(1000);
    this->bss_fields[BSF_PERCENT].set_width(6);
    this->bss_fields[BSF_PERCENT].set_left_pad(1);
    this->bss_fields[BSF_HITS].set_min_width(10);
    this->bss_fields[BSF_HITS].set_share(5);
    this->bss_fields[BSF_SEARCH_TERM].set_min_width(10);
    this->bss_fields[BSF_SEARCH_TERM].set_share(1);
    this->bss_fields[BSF_LOADING].set_width(13);
    this->bss_fields[BSF_LOADING].right_justify(true);
    this->bss_fields[BSF_HELP].set_width(14);
    auto help_al = attr_line_t(" ").append("?"_hotkey).append(":View Help ");
    this->bss_fields[BSF_HELP].set_value(help_al);
    this->bss_fields[BSF_HELP].right_justify(true);
    this->bss_prompt.set_left_pad(1);
    this->bss_prompt.set_min_width(35);
    this->bss_prompt.set_share(1);
    this->bss_error.set_left_pad(1);
    this->bss_error.set_min_width(35);
    this->bss_error.set_share(1);
    this->bss_line_error.set_left_pad(1);
    this->bss_line_error.set_min_width(35);
    this->bss_line_error.set_share(1);
}

void
bottom_status_source::update_line_number(listview_curses* lc)
{
    auto& sf = this->bss_fields[BSF_LINE_NUMBER];
    auto sel = lc->get_selection();

    if (lc->get_inner_height() == 0) {
        sf.set_value(" L0"_frag);
    } else if (sel) {
        sf.set_value(" L%'d", (int) sel.value());
    } else {
        sf.set_value(" L-"_frag);
    }

    this->bss_line_error.set_value(
        lc->map_top_row([](const attr_line_t& top_row)
                            -> std::optional<std::string> {
              const auto& sa = top_row.get_attrs();
              auto error_wrapper = get_string_attr(sa, SA_ERROR);
              if (error_wrapper) {
                  return error_wrapper.value().get();
              }
              return std::nullopt;
          }).value_or(""));
}

bottom_status_source::search_report_t
bottom_status_source::search_report_for(textview_curses& tc)
{
    if (tc.get_focused_search_slot()) {
        return search_report_t::focused;
    }
    // With no named search in play, n/N covers the interactive search and
    // nothing else, so there is no distinction to draw.
    if (tc.get_named_searches().empty()) {
        return search_report_t::interactive;
    }

    return search_report_t::all;
}

void
bottom_status_source::update_search_term(textview_curses& tc)
{
    auto& sf = this->bss_fields[BSF_SEARCH_TERM];
    auto search_term = tc.get_current_search();

    this->bss_focused_search_slot = tc.get_focused_search_slot();
    this->bss_search_report = search_report_for(tc);
    sf.clear();
    switch (this->bss_search_report) {
        case search_report_t::all: {
            // n/N is moving through more than one pattern, so there is no
            // single term to put here.
            sf.get_value().append("all searches"_variable);
            search_term.clear();
            break;
        }
        case search_report_t::focused: {
            auto focused_name = tc.get_focused_search_name();

            if (focused_name) {
                const auto* ns = tc.find_named_search(focused_name.value());
                // The name wears the background that its matches wear in the
                // view, so the field and the highlighting read as the same
                // search.
                auto name_attrs = text_attrs{};

                name_attrs.ta_bg_color
                    = view_colors::singleton().color_for_ident(
                        string_fragment::from_str(focused_name.value()));
                sf.get_value()
                    .append(" ")
                    .append(focused_name.value(), VC_STYLE.value(name_attrs))
                    .append(" ");
                search_term = ns == nullptr ? std::string() : ns->ns_pattern;
            }
            break;
        }
        case search_report_t::interactive:
            break;
    }
    if (!search_term.empty()) {
        auto search_term_al = attr_line_t(search_term);

        lnav::snippets::regex_highlighter(
            search_term_al, -1, line_range{0, (int) search_term_al.length()});
        sf.get_value().append_quoted(search_term_al);
    }

    this->bss_paused = tc.is_paused();
    this->update_loading(0, 0);
}

void
bottom_status_source::update_percent(listview_curses* lc)
{
    status_field& sf = this->bss_fields[BSF_PERCENT];
    vis_line_t top = lc->get_top();
    vis_line_t bottom, height;
    unsigned long width;
    double percent;

    lc->get_dimensions(height, width);

    if (lc->get_inner_height() > 0) {
        bottom = std::min(top + height - 1_vl, lc->get_inner_height() - 1_vl);
        percent = (double) (bottom + 1);
        percent /= (double) lc->get_inner_height();
        percent *= 100.0;
    } else {
        percent = 0.0;
    }
    sf.set_value("%3d%% ", (int) percent);
}

bool
bottom_status_source::update_marks(listview_curses* lc)
{
    auto* tc = static_cast<textview_curses*>(lc);
    status_field& sf = this->bss_fields[BSF_HITS];
    auto retval = false;

    // The count has to cover the same hits that n/N moves through, since the
    // term it is labelled with names them.
    auto focused_slot = tc->get_focused_search_slot();

    if (focused_slot != this->bss_focused_search_slot
        || search_report_for(*tc) != this->bss_search_report)
    {
        this->update_search_term(*tc);
    }


    auto report = search_report_for(*tc);
    using bv_t = bookmark_vector<vis_line_t>;
    const auto& bv = [tc, report, focused_slot]() -> const bv_t& {
        switch (report) {
            case search_report_t::focused:
                return tc->search_matches_for_slot(focused_slot.value());
            case search_report_t::all:
                return tc->get_bookmarks()[&textview_curses::BM_SEARCH];
            case search_report_t::interactive:
                break;
        }
        return tc->get_interactive_matches();
    }();

    // A focused search and the "all searches" label always name something, so
    // they get a count even when it is zero.  The interactive search on its
    // own leaves the field empty until there is a pattern to count.
    if (!bv.empty() || report != search_report_t::interactive
        || !tc->get_current_search().empty())
    {
        auto vl = tc->get_selection();
        if (vl) {
            auto lb = bv.bv_tree.find(vl.value());
            if (lb != bv.bv_tree.end()) {
                retval = sf.set_value("  Hit %'d of %'d for ",
                                      (lb - bv.bv_tree.begin()) + 1,
                                      bv.size());
            } else {
                retval = sf.set_value("  %'d hits for ", bv.size());
            }
        }
    } else if (tc->is_searching()) {
        // A search that this field is not counting -- a named search, say --
        // is running, so update_hits() has the cylon going.  Say what is
        // happening instead of animating an empty field.
        retval = sf.set_value("  Searching...  "_frag);
    } else {
        retval = sf.clear();
    }
    return retval;
}

bool
bottom_status_source::update_hits(textview_curses* tc)
{
    auto& sf = this->bss_fields[BSF_HITS];
    bool retval = false;
    role_t new_role;

    if (tc->is_searching()) {
        this->bss_hit_spinner += 1;
        if (this->bss_hit_spinner % 2) {
            new_role = role_t::VCR_ACTIVE_STATUS;
        } else {
            new_role = role_t::VCR_ACTIVE_STATUS2;
        }
        if (!sf.is_cylon()) {
            sf.set_cylon(true);
        }
        retval = true;
    } else {
        new_role = role_t::VCR_STATUS;
        if (sf.is_cylon()) {
            sf.set_cylon(false);
            sf.clear();  // clear cylon style attribute
            retval = true;
        }
    }
    // this->bss_error.clear();
    sf.set_role(new_role);
    retval = this->update_marks(tc) || retval;
    return retval;
}

void
bottom_status_source::update_loading(file_off_t off,
                                     file_ssize_t total,
                                     const char* term)
{
    auto& sf = this->bss_fields[BSF_LOADING];

    require_ge(off, 0);
    require_ge(total, off);

    if (total == 0) {
        sf.set_cylon(false);
        sf.set_role(role_t::VCR_STATUS);
        if (this->bss_paused) {
            sf.set_value("\xE2\x80\x96 Paused"_frag);
        } else {
            sf.clear();
        }
    } else if (off == total) {
        static const char* const DOTS[] = {
            "   ",
            ".  ",
            ".. ",
            "...",
            ".. ",
            ".  ",
        };
        static auto DOTS_LEN = std::distance(std::begin(DOTS), std::end(DOTS));

        this->bss_load_percent += 1;
        sf.set_cylon(true);
        sf.set_role(role_t::VCR_ACTIVE_STATUS2);
        sf.set_value(" Working%s  ", DOTS[this->bss_load_percent % DOTS_LEN]);
    } else {
        int pct = (int) (((double) off / (double) total) * 100.0);

        if (this->bss_load_percent != pct) {
            this->bss_load_percent = pct;

            sf.set_cylon(true);
            sf.set_role(role_t::VCR_ACTIVE_STATUS2);
            sf.set_value(" %s %2d%% ", term, pct);
        }
    }
}

size_t
bottom_status_source::statusview_fields()
{
    size_t retval;

    if (this->bss_prompt.empty() && this->bss_error.empty()
        && this->bss_line_error.empty())
    {
        retval = BSF__MAX - 1;
    } else {
        retval = 1;
    }

    return retval;
}

status_field&
bottom_status_source::statusview_value_for_field(int field)
{
    if (!this->bss_error.empty()) {
        return this->bss_error;
    }
    if (!this->bss_prompt.empty()) {
        return this->bss_prompt;
    }
    if (!this->bss_line_error.empty()) {
        return this->bss_line_error;
    }
    if (field == 4) {
        if (this->bss_fields[BSF_LOADING].empty()) {
            return this->bss_fields[BSF_HELP];
        }
        return this->bss_fields[BSF_LOADING];
    }
    return this->get_field((field_t) field);
}
