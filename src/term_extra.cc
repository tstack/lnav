/**
 * Copyright (c) 2025, Timothy Stack
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

#include <vector>

#include "term_extra.hh"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>

#include "base/attr_line.hh"
#include "fmt/format.h"
#include "fmt/std.h"
#include "log_format_fwd.hh"
#include "logfile.hh"

term_extra::term_extra()
{
    const char* term_name = getenv("TERM");

    this->te_enabled
        = (term_name != nullptr && strstr(term_name, "xterm") != nullptr);

    if (getenv("SSH_CONNECTION") != nullptr) {
        char hostname[MAXHOSTNAMELEN] = "UNKNOWN";
        struct passwd* userent;

        gethostname(hostname, sizeof(hostname));
        this->te_prefix = hostname;
        if ((userent = getpwuid(getuid())) != nullptr) {
            this->te_prefix
                = std::string(userent->pw_name) + "@" + this->te_prefix;
        }
        this->te_prefix += ":";
    }
}

void
term_extra::update_title(listview_curses* lc)
{
    if (!this->te_enabled) {
        return;
    }

    if (lc->get_inner_height() > 0) {
        std::vector<attr_line_t> rows(1);

        lc->get_data_source()->listview_value_for_rows(
            *lc, lc->get_top(), rows);
        auto& sa = rows[0].get_attrs();
        auto line_attr_opt = get_string_attr(sa, L_FILE);
        if (line_attr_opt) {
            auto lf = line_attr_opt.value().get();
            const auto& filename = lf->get_unique_path();

            if (filename != this->te_last_title) {
                fmt::print(
                    FMT_STRING("\033]0;{}{}\007"), this->te_prefix, filename);
                fflush(stdout);

                this->te_last_title = filename;
            }
            return;
        }
    }

    const auto& view_title = lc->get_title();

    if (view_title != this->te_last_title) {
        fmt::print(FMT_STRING("\033]0;{}{}\007"), this->te_prefix, view_title);
        fflush(stdout);

        this->te_last_title = view_title;
    }
}
