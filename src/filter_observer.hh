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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef filter_observer_hh
#define filter_observer_hh

#include <cstdint>
#include <memory>

#include "base/file_range.hh"
#include "logfile.hh"
#include "shared_buffer.hh"
#include "textview_curses.hh"

class line_filter_observer : public logline_observer {
public:
    line_filter_observer(filter_stack& fs, const std::shared_ptr<logfile>& lf)
        : lfo_filter_stack(fs), lfo_filter_state(lf)
    {
    }

    void logline_restart(const logfile& lf, file_size_t rollback_size) override
    {
        for (const auto& filter : this->lfo_filter_stack) {
            filter->revert_to_last(this->lfo_filter_state, rollback_size);
        }
    }

    void logline_new_lines(const logfile& lf,
                           logfile::const_iterator ll_baegin,
                           logfile::const_iterator ll_end,
                           const shared_buffer_ref& sbr) override;

    void logline_eof(const logfile& lf) override;

    bool excluded(uint32_t filter_in_mask,
                  uint32_t filter_out_mask,
                  size_t offset) const
    {
        bool filtered_in = (filter_in_mask == 0)
            || (this->lfo_filter_state.tfs_mask[offset] & filter_in_mask) != 0;
        bool filtered_out
            = (this->lfo_filter_state.tfs_mask[offset] & filter_out_mask) != 0;
        return !filtered_in || filtered_out;
    }

    size_t get_min_count(size_t max) const;

    void clear_deleted_filter_state();

    filter_stack& lfo_filter_stack;
    logfile_filter_state lfo_filter_state;
};

#endif
