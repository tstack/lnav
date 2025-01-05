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

#include <algorithm>
#include <iterator>

#include "filter_observer.hh"

#include "base/lnav_log.hh"
#include "config.h"
#include "log_format.hh"
#include "shared_buffer.hh"

void
line_filter_observer::logline_new_lines(const logfile& lf,
                                        logfile::const_iterator ll_begin,
                                        logfile::const_iterator ll_end,
                                        const shared_buffer_ref& sbr)
{
    const auto offset = std::distance(lf.begin(), ll_begin);

    require(&lf == this->lfo_filter_state.tfs_logfile.get());

    this->lfo_filter_state.resize(lf.size());
    if (this->lfo_filter_stack.empty()) {
        return;
    }

    for (; ll_begin != ll_end; ++ll_begin) {
        auto sbr_copy = sbr.clone();
        if (lf.get_format() != nullptr) {
            lf.get_format()->get_subline(*ll_begin, sbr_copy);
        }
        sbr_copy.erase_ansi();
        for (const auto& filter : this->lfo_filter_stack) {
            if (filter->lf_deleted) {
                continue;
            }
            if (offset
                >= this->lfo_filter_state.tfs_filter_count[filter->get_index()])
            {
                filter->add_line(this->lfo_filter_state, ll_begin, sbr_copy);
            }
        }
    }
}

void
line_filter_observer::logline_eof(const logfile& lf)
{
    this->lfo_filter_state.reserve(lf.size() + lf.estimated_remaining_lines());
    for (const auto& iter : this->lfo_filter_stack) {
        if (iter->lf_deleted) {
            continue;
        }
        iter->end_of_message(this->lfo_filter_state);
    }
}

size_t
line_filter_observer::get_min_count(size_t max) const
{
    size_t retval = max;

    for (const auto& filter : this->lfo_filter_stack) {
        if (filter->lf_deleted) {
            continue;
        }
        retval = std::min(
            retval,
            this->lfo_filter_state.tfs_filter_count[filter->get_index()]);
    }

    return retval;
}

void
line_filter_observer::clear_deleted_filter_state()
{
    uint32_t used_mask = 0;

    for (auto& filter : this->lfo_filter_stack) {
        if (filter->lf_deleted) {
            log_debug("skipping deleted %p %d %d",
                      filter.get(),
                      filter->get_index(),
                      filter->get_lang());
            continue;
        }
        used_mask |= (1UL << filter->get_index());
    }
    this->lfo_filter_state.clear_deleted_filter_state(used_mask);
}
