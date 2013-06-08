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
 */

#include "config.h"

#include <vector>
#include <algorithm>

#include "pcrepp.hh"
#include "lnav_util.hh"
#include "data_parser.hh"
#include "textview_curses.hh"

using namespace std;

bookmark_type_t textview_curses::BM_USER;
bookmark_type_t textview_curses::BM_SEARCH;

class ansi_scrubber {
    /* XXX move to view_curses.cc ; actually, move to it's own file and call
     * from there.
     */
public:
    static ansi_scrubber &singleton()
    {
        static ansi_scrubber s_as;

        return s_as;
    }

    void scrub_value(string &str, string_attrs_t &sa)
    {
        view_colors &vc = view_colors::singleton();
        vector<pair<string, string_attr_t> > attr_queue;
        pcre_context_static<60> context;
        vector<line_range> range_queue;
        pcre_input pi(str);

        while (this->as_regex.match(context, pi)) {
            pcre_context::capture_t *caps = context.all();
            struct line_range lr;
            bool has_attrs = false;
            int  attrs     = 0;
            int  bg = 0;
            int  fg = 0;
            int  lpc;

            switch (pi.get_substr_start(&caps[2])[0]) {
            case 'm':
                for (lpc = caps[1].c_begin;
                     lpc != (int)string::npos && lpc < caps[1].c_end; ) {
                    int ansi_code = 0;

                    if (sscanf(&(str[lpc]), "%d", &ansi_code) == 1) {
                        if (90 <= ansi_code && ansi_code <= 97) {
                            ansi_code -= 60;
                            attrs |= A_STANDOUT;
                        }
                        if (30 <= ansi_code && ansi_code <= 37) {
                            fg = ansi_code - 30;
                        }
                        if (40 <= ansi_code && ansi_code <= 47) {
                            bg = ansi_code - 40;
                        }
                        switch (ansi_code) {
                        case 1:
                            attrs |= A_BOLD;
                            break;

                        case 2:
                            attrs |= A_DIM;
                            break;

                        case 4:
                            attrs |= A_UNDERLINE;
                            break;

                        case 7:
                            attrs |= A_REVERSE;
                            break;
                        }
                    }
                    lpc = str.find(";", lpc);
                    if (lpc != (int)string::npos) {
                        lpc += 1;
                    }
                }
                if (fg != 0 || bg != 0) {
                    attrs |= vc.ansi_color_pair(fg, bg);
                }
                has_attrs = true;
                break;

            case 'C':
                {
                    int spaces = 0;

                    if (sscanf(&(str[caps[1].c_begin]), "%d", &spaces) == 1) {
                        str.insert(caps[0].c_end, spaces, ' ');
                    }
                }
                break;
            }
            str.erase(str.begin() + caps[0].c_begin,
                      str.begin() + caps[0].c_end);

            if (has_attrs) {
                if (!range_queue.empty()) {
                    range_queue.back().lr_end = caps[0].c_begin;
                }
                lr.lr_start = caps[0].c_begin;
                lr.lr_end   = -1;
                range_queue.push_back(lr);
                attr_queue.push_back(make_string_attr("style", attrs));
            }

            pi.reset(str);
        }

        for (size_t lpc = 0; lpc < range_queue.size(); lpc++) {
            sa[range_queue[lpc]].insert(attr_queue[lpc]);
        }
    };

private:
    ansi_scrubber()
        : as_regex("\x1b\\[([\\d=;]*)([a-zA-Z])") {
    };

    pcrepp as_regex;
};

textview_curses::textview_curses()
    : tc_searching(false),
      tc_follow_search(false)
{
    this->set_data_source(this);
}

textview_curses::~textview_curses()
{ }

void textview_curses::reload_data(void)
{
    if (this->tc_sub_source != NULL) {
        this->tc_sub_source->text_update_marks(this->tc_bookmarks);
    }
    this->listview_curses::reload_data();
}

void textview_curses::grep_begin(grep_proc &gp)
{
    this->tc_searching = true;
    if (0) {
        this->tc_bookmarks[&BM_SEARCH].clear();
    }

    this->tc_search_action.invoke(this);

    listview_curses::reload_data();
}

void textview_curses::grep_end(grep_proc &gp)
{
    this->tc_searching = false;
    this->tc_search_action.invoke(this);
}

void textview_curses::grep_match(grep_proc &gp,
                                 grep_line_t line,
                                 int start,
                                 int end)
{
    this->tc_bookmarks[&BM_SEARCH].insert_once(vis_line_t(line));
    if (this->tc_sub_source != NULL) {
        this->tc_sub_source->text_mark(&BM_SEARCH, line, true);
    }

    listview_curses::reload_data();
}

void textview_curses::listview_value_for_row(const listview_curses &lv,
                                             vis_line_t row,
                                             attr_line_t &value_out)
{
    bookmark_vector<vis_line_t> &user_marks = this->tc_bookmarks[&BM_USER];
    string_attrs_t &          sa         = value_out.get_attrs();
    string &                  str        = value_out.get_string();
    highlight_map_t::iterator iter;
    string::iterator          str_iter;

    this->tc_sub_source->text_value_for_line(*this, row, str);
    this->tc_sub_source->text_attrs_for_line(*this, row, sa);

    ansi_scrubber::singleton().scrub_value(str, sa);

    for (iter = this->tc_highlights.begin();
         iter != this->tc_highlights.end();
         iter++) {
        int off, hcount = 0;

        for (off = 0; off < (int)str.size(); ) {
            int rc, matches[60];

            rc = pcre_exec(iter->second.h_code,
                           iter->second.h_code_extra,
                           str.c_str(),
                           str.size(),
                           off,
                           0,
                           matches,
                           60);
            if (rc > 0) {
                struct line_range lr;

                if (rc == 2) {
                    lr.lr_start = matches[2];
                    lr.lr_end   = matches[3];
                }
                else {
                    lr.lr_start = matches[0];
                    lr.lr_end   = matches[1];
                }

                if (lr.lr_end > lr.lr_start) {
                    sa[lr].insert(make_string_attr("style", iter->second.
                                                   get_attrs(hcount)));
                    hcount++;

                    off = matches[1];
                }
                else {
                    off += 1;
                }
            }
            else {
                off = str.size();
            }
        }
    }

#if 0
    typedef std::map<std::string, view_colors::role_t> key_map_t;
    static key_map_t key_roles;

    data_scanner ds(str);
    data_parser  dp(&ds);

    dp.parse();

    for (list<data_parser::element>::iterator iter = dp.dp_stack.begin();
         iter != dp.dp_stack.end();
         ++iter) {
        view_colors &vc = view_colors::singleton();

        if (iter->e_token == DNT_PAIR) {
            list<data_parser::element>::iterator pair_iter;
            key_map_t::iterator km_iter;
            data_token_t        value_token;
            struct line_range   lr;
            string key;

            value_token =
                iter->e_sub_elements->back().e_sub_elements->front().e_token;
            if (value_token == DT_STRING) {
                continue;
            }

            lr.lr_start = iter->e_capture.c_begin;
            lr.lr_end   = iter->e_capture.c_end;

            key = ds.get_input().get_substr(
                &iter->e_sub_elements->front().e_capture);
            if ((km_iter = key_roles.find(key)) == key_roles.end()) {
                key_roles[key] = vc.next_highlight();
            }
            /* fprintf(stderr, "key = %s\n", key.c_str()); */
            sa[lr].insert(make_string_attr("style",
                                           vc.attrs_for_role(key_roles[key])));

            pair_iter = iter->e_sub_elements->begin();
            ++pair_iter;

            lr.lr_start = pair_iter->e_capture.c_begin;
            lr.lr_end   = pair_iter->e_capture.c_end;
            sa[lr].insert(make_string_attr("style",
                                           COLOR_PAIR(view_colors::VC_WHITE) |
                                           A_BOLD));
        }
    }
#endif

    if (binary_search(user_marks.begin(), user_marks.end(), row)) {
        struct line_range        lr = { 0, -1 };
        string_attrs_t::iterator iter;

        for (iter = sa.begin(); iter != sa.end(); iter++) {
            attrs_map_t &         am = iter->second;
            attrs_map_t::iterator am_iter;

            for (am_iter = am.begin(); am_iter != am.end(); am_iter++) {
                if (am_iter->first == "style") {
                    am_iter->second.sa_int ^= A_REVERSE;
                }
            }
        }

        sa[lr].insert(make_string_attr("style", A_REVERSE));
    }
}
