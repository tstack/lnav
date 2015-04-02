/**
 * Copyright (c) 2015, Timothy Stack
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

#ifndef __filter_observer_hh
#define __filter_observer_hh

#include <sys/types.h>

#include "logfile.hh"
#include "textview_curses.hh"

class line_filter_observer : public logline_observer {
public:
    line_filter_observer(filter_stack &fs, logfile *lf)
            : lfo_filter_stack(fs), lfo_filter_state(lf) {

    };

    void logline_restart(const logfile &lf) {
        for (filter_stack::iterator iter = this->lfo_filter_stack.begin();
             iter != this->lfo_filter_stack.end();
             ++iter) {
            (*iter)->revert_to_last(this->lfo_filter_state);
        }
    };

    void logline_new_line(const logfile &lf, logfile::const_iterator ll, shared_buffer_ref &sbr) {
        size_t offset = std::distance(lf.begin(), ll);

        require(&lf == this->lfo_filter_state.tfs_logfile);

        this->lfo_filter_state.resize(lf.size());
        if (!this->lfo_filter_stack.empty()) {
            if (lf.get_format() != NULL) {
                lf.get_format()->get_subline(*ll, sbr);
            }
            for (filter_stack::iterator iter = this->lfo_filter_stack.begin();
                 iter != this->lfo_filter_stack.end();
                 ++iter) {
                if (offset >= this->lfo_filter_state.tfs_filter_count[(*iter)->get_index()]) {
                    (*iter)->add_line(this->lfo_filter_state, ll, sbr);
                }
            }
        }
    };

    void logline_eof(const logfile &lf) {
        for (filter_stack::iterator iter = this->lfo_filter_stack.begin();
             iter != this->lfo_filter_stack.end();
             ++iter) {
            (*iter)->end_of_message(this->lfo_filter_state);
        }
    };

    bool excluded(uint32_t filter_in_mask, uint32_t filter_out_mask,
            size_t offset) const {
        bool filtered_in = (filter_in_mask == 0) || (
                this->lfo_filter_state.tfs_mask[offset] & filter_in_mask) != 0;
        bool filtered_out = (
                this->lfo_filter_state.tfs_mask[offset] & filter_out_mask) != 0;
        return !filtered_in || filtered_out;
    };

    size_t get_min_count(size_t max) const {
        size_t retval = max;

        for (filter_stack::iterator iter = this->lfo_filter_stack.begin();
             iter != this->lfo_filter_stack.end();
             ++iter) {
            retval = std::min(retval, this->lfo_filter_state.tfs_filter_count[(*iter)->get_index()]);
        }

        return retval;
    };

    filter_stack &lfo_filter_stack;
    logfile_filter_state lfo_filter_state;
};

#endif
