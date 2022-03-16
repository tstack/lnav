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

#ifndef sequence_sink_hh
#define sequence_sink_hh

#include <map>

#include "bookmarks.hh"
#include "grep_proc.hh"
#include "listview_curses.hh"
#include "sequence_matcher.hh"

class sequence_sink : public grep_proc_sink<vis_line_t> {
public:
    sequence_sink(sequence_matcher& sm, bookmark_vector<vis_line_t>& bv)
        : ss_matcher(sm), ss_bookmarks(bv){};

    void grep_match(grep_proc<vis_line_t>& gp,
                    vis_line_t line,
                    int start,
                    int end)
    {
        this->ss_line_values.clear();
    };

    void grep_capture(grep_proc<vis_line_t>& gp,
                      vis_line_t line,
                      int start,
                      int end,
                      char* capture)
    {
        if (start == -1) {
            this->ss_line_values.push_back("");
        } else {
            this->ss_line_values.push_back(std::string(capture));
        }
    };

    void grep_match_end(grep_proc<vis_line_t>& gp, vis_line_t line)
    {
        sequence_matcher::id_t line_id;

        this->ss_matcher.identity(this->ss_line_values, line_id);

        std::vector<vis_line_t>& line_state = this->ss_state[line_id];
        if (this->ss_matcher.match(this->ss_line_values, line_state, line)) {
            std::vector<vis_line_t>::iterator iter;

            for (iter = line_state.begin(); iter != line_state.end(); ++iter) {
                this->ss_bookmarks.insert_once(vis_line_t(*iter));
            }
            line_state.clear();
        }
    };

private:
    sequence_matcher& ss_matcher;
    bookmark_vector<vis_line_t>& ss_bookmarks;
    std::vector<std::string> ss_line_values;
    std::map<sequence_matcher::id_t, std::vector<vis_line_t> > ss_state;
};
#endif
