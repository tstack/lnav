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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "filter_observer.hh"

void line_filter_observer::logline_new_line(const logfile &lf,
                                            logfile::const_iterator ll,
                                            shared_buffer_ref &sbr)
{
    size_t offset = std::distance(lf.begin(), ll);

    require(&lf == this->lfo_filter_state.tfs_logfile.get());

    this->lfo_filter_state.resize(lf.size());
    if (this->lfo_filter_stack.empty()) {
        return;
    }

    if (lf.get_format() != nullptr) {
        lf.get_format()->get_subline(*ll, sbr);
    }
    for (auto &filter : this->lfo_filter_stack) {
        if (filter->lf_deleted) {
            continue;
        }
        if (offset >= this->lfo_filter_state.tfs_filter_count[filter->get_index()]) {
            filter->add_line(this->lfo_filter_state, ll, sbr);
        }
    }
}

void line_filter_observer::logline_eof(const logfile &lf)
{
    for (auto &iter : this->lfo_filter_stack) {
        iter->end_of_message(this->lfo_filter_state);
    }
}
