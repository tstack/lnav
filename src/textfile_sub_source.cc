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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "textfile_sub_source.hh"

size_t textfile_sub_source::text_line_count()
{
    size_t retval = 0;

    if (!this->tss_files.empty()) {
        std::shared_ptr<logfile> lf = this->current_file();
        auto *lfo = (line_filter_observer *) lf->get_logline_observer();
        retval = lfo->lfo_filter_state.tfs_index.size();
    }

    return retval;
}

void textfile_sub_source::text_value_for_line(textview_curses &tc, int line,
                                              std::string &value_out,
                                              text_sub_source::line_flags_t flags)
{
    if (!this->tss_files.empty()) {
        std::shared_ptr<logfile> lf = this->current_file();
        auto *lfo = (line_filter_observer *) lf->get_logline_observer();
        auto read_result = lf->read_line(lf->begin() + lfo->lfo_filter_state.tfs_index[line]);
        if (read_result.isOk()) {
            value_out = to_string(read_result.unwrap());
        }
    }
    else {
        value_out.clear();
    }
}

void textfile_sub_source::text_attrs_for_line(textview_curses &tc, int row,
                                              string_attrs_t &value_out)
{
    if (this->current_file() == nullptr) {
        return;
    }

    struct line_range lr;

    lr.lr_start = 0;
    lr.lr_end   = -1;
    value_out.emplace_back(lr, &logline::L_FILE, this->current_file().get());
}

size_t textfile_sub_source::text_size_for_line(textview_curses &tc, int line,
                                               text_sub_source::line_flags_t flags)
{
    size_t retval = 0;

    if (!this->tss_files.empty()) {
        std::shared_ptr<logfile> lf = this->current_file();
        auto *lfo = (line_filter_observer *) lf->get_logline_observer();
        retval = lf->line_length(lf->begin() + lfo->lfo_filter_state.tfs_index[line]);
    }

    return retval;
}

void textfile_sub_source::to_front(const std::shared_ptr<logfile>& lf)
{
    auto iter = std::find(this->tss_files.begin(),
                          this->tss_files.end(),
                          lf);
    if (iter != this->tss_files.end()) {
        this->tss_files.erase(iter);
    } else {
        iter = std::find(this->tss_hidden_files.begin(),
                         this->tss_hidden_files.end(),
                         lf);

        if (iter != this->tss_hidden_files.end()) {
            this->tss_hidden_files.erase(iter);
        }
    }
    this->tss_files.push_front(lf);
    this->tss_view->reload_data();
}

void textfile_sub_source::rotate_left()
{
    if (this->tss_files.size() > 1) {
        this->tss_files.push_back(this->tss_files.front());
        this->tss_files.pop_front();
        this->tss_view->reload_data();
        this->tss_view->redo_search();
    }
}

void textfile_sub_source::rotate_right()
{
    if (this->tss_files.size() > 1) {
        this->tss_files.push_front(this->tss_files.back());
        this->tss_files.pop_back();
        this->tss_view->reload_data();
        this->tss_view->redo_search();
    }
}

void textfile_sub_source::remove(const std::shared_ptr<logfile>& lf)
{
    auto iter = std::find(this->tss_files.begin(),
                          this->tss_files.end(), lf);
    if (iter != this->tss_files.end()) {
        this->tss_files.erase(iter);
        detach_observer(lf);
    } else {
        iter = std::find(this->tss_hidden_files.begin(),
                         this->tss_hidden_files.end(),
                         lf);
        if (iter != this->tss_hidden_files.end()) {
            this->tss_hidden_files.erase(iter);
            detach_observer(lf);
        }
    }
}

void textfile_sub_source::push_back(const std::shared_ptr<logfile>& lf)
{
    auto *lfo = new line_filter_observer(this->get_filters(), lf);
    lf->set_logline_observer(lfo);
    this->tss_files.push_back(lf);
}

void textfile_sub_source::text_filters_changed()
{
    for (auto iter = this->tss_files.begin();
         iter != this->tss_files.end();) {
        ++iter;
    }
    for (auto iter = this->tss_hidden_files.begin();
         iter != this->tss_hidden_files.end();) {
        ++iter;
    }

    std::shared_ptr<logfile> lf = this->current_file();

    if (lf == nullptr) {
        return;
    }

    auto *lfo = (line_filter_observer *) lf->get_logline_observer();
    uint32_t filter_in_mask, filter_out_mask;

    lfo->clear_deleted_filter_state();
    lf->reobserve_from(lf->begin() + lfo->get_min_count(lf->size()));

    this->get_filters().get_enabled_mask(filter_in_mask, filter_out_mask);
    lfo->lfo_filter_state.tfs_index.clear();
    for (uint32_t lpc = 0; lpc < lf->size(); lpc++) {
        if (this->tss_apply_filters &&
            lfo->excluded(filter_in_mask, filter_out_mask, lpc)) {
            continue;
        }
        lfo->lfo_filter_state.tfs_index.push_back(lpc);
    }

    this->tss_view->redo_search();
}

int textfile_sub_source::get_filtered_count() const
{
    std::shared_ptr<logfile> lf = this->current_file();
    int retval = 0;

    if (lf != nullptr) {
        auto *lfo = (line_filter_observer *) lf->get_logline_observer();
        retval = lf->size() - lfo->lfo_filter_state.tfs_index.size();
    }
    return retval;
}

int textfile_sub_source::get_filtered_count_for(size_t filter_index) const
{
    std::shared_ptr<logfile> lf = this->current_file();

    if (lf == nullptr) {
        return 0;
    }

    auto *lfo = (line_filter_observer *) lf->get_logline_observer();
    return lfo->lfo_filter_state.tfs_filter_hits[filter_index];
}

text_format_t textfile_sub_source::get_text_format() const
{
    if (this->tss_files.empty()) {
        return text_format_t::TF_UNKNOWN;
    }

    return this->tss_files.front()->get_text_format();
}
