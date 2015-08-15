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

#ifndef __textfile_sub_source_hh
#define __textfile_sub_source_hh

#include <list>

#include "logfile.hh"
#include "textview_curses.hh"
#include "filter_observer.hh"

class textfile_sub_source : public text_sub_source {
public:
    typedef std::list<logfile *>::iterator file_iterator;

    textfile_sub_source() { };

    bool empty() const {
        return this->tss_files.empty();
    }

    size_t size() const {
        return this->tss_files.size();
    }

    size_t text_line_count()
    {
        size_t retval = 0;

        if (!this->tss_files.empty()) {
            logfile *lf = this->current_file();
            line_filter_observer *lfo = (line_filter_observer *) lf->get_logline_observer();
            retval = lfo->lfo_filter_state.tfs_index.size();
        }

        return retval;
    };

    size_t text_line_width() {
        return this->tss_files.empty() ? 0 : this->current_file()->get_longest_line_length();
    };

    void text_value_for_line(textview_curses &tc,
                             int line,
                             std::string &value_out,
                             bool raw = false)
    {
        if (!this->tss_files.empty()) {
            logfile *lf = this->current_file();
            line_filter_observer *lfo = (line_filter_observer *) lf->get_logline_observer();
            lf->read_line(lf->begin() + lfo->lfo_filter_state.tfs_index[line], value_out);
        }
        else {
            value_out.clear();
        }
    };

    void text_attrs_for_line(textview_curses &tc,
                             int row,
                             string_attrs_t &value_out)
    {
        if (this->current_file() == NULL) {
            return;
        }

        struct line_range lr;

        lr.lr_start = 0;
        lr.lr_end   = -1;
        value_out.push_back(string_attr(lr, &logline::L_FILE, this->current_file()));
    };

    size_t text_size_for_line(textview_curses &tc, int line, bool raw) {
        size_t retval = 0;

        if (!this->tss_files.empty()) {
            logfile *lf = this->current_file();
            line_filter_observer *lfo = (line_filter_observer *) lf->get_logline_observer();
            retval = lf->line_length(lf->begin() + lfo->lfo_filter_state.tfs_index[line]);
        }

        return retval;
    };

    logfile *current_file(void) const
    {
        if (this->tss_files.empty()) {
            return NULL;
        }

        return this->tss_files.front();
    };

    std::string text_source_name(const textview_curses &tv) {
        if (this->tss_files.empty()) {
            return "";
        }

        return this->tss_files.front()->get_filename();
    };

    void to_front(logfile *lf) {
        this->tss_files.remove(lf);
        this->tss_files.push_front(lf);
    };

    void rotate_left() {
        this->tss_files.push_back(this->tss_files.front());
        this->tss_files.pop_front();
    };

    void rotate_right() {
        this->tss_files.push_front(this->tss_files.back());
        this->tss_files.pop_back();
    };

    void remove(logfile *lf) {
        file_iterator iter = std::find(this->tss_files.begin(),
                this->tss_files.end(), lf);
        if (iter != this->tss_files.end()) {
            detach_observer(lf);
            this->tss_files.erase(iter);
        }
    };

    void push_back(logfile *lf) {
        line_filter_observer *lfo = new line_filter_observer(
                this->get_filters(), lf);
        lf->set_logline_observer(lfo);
        this->tss_files.push_back(lf);
    };

    template<class T> bool rescan_files(T &callback) {
        file_iterator iter;
        bool retval = false;

        for (iter = this->tss_files.begin(); iter != this->tss_files.end();) {
            logfile *lf = (*iter);

            if (!lf->exists() || lf->is_closed()) {
                iter = this->tss_files.erase(iter);
                this->detach_observer(lf);
                callback.closed_file(lf);
                continue;
            }

            try {
                uint32_t old_size = lf->size();
                bool new_text_data = lf->rebuild_index();

                if (lf->get_format() != NULL) {
                    iter = this->tss_files.erase(iter);
                    this->detach_observer(lf);
                    callback.promote_file(lf);
                    continue;
                }

                retval = retval || new_text_data;
                callback.scanned_file(lf);

                uint32_t filter_in_mask, filter_out_mask;

                this->get_filters().get_enabled_mask(filter_in_mask, filter_out_mask);
                line_filter_observer *lfo = (line_filter_observer *) lf->get_logline_observer();
                for (uint32_t lpc = old_size; lpc < lf->size(); lpc++) {
                    if (lfo->excluded(filter_in_mask, filter_out_mask, lpc)) {
                        continue;
                    }
                    lfo->lfo_filter_state.tfs_index.push_back(lpc);
                }
            }
            catch (const line_buffer::error &e) {
                iter = this->tss_files.erase(iter);
                lf->close();
                this->detach_observer(lf);
                callback.closed_file(lf);
                continue;
            }

            ++iter;
        }

        return retval;
    };

    virtual void text_filters_changed() {
        logfile *lf = this->current_file();

        if (lf == NULL) {
            return;
        }

        line_filter_observer *lfo = (line_filter_observer *) lf->get_logline_observer();
        uint32_t filter_in_mask, filter_out_mask;

        lf->reobserve_from(lf->begin() + lfo->get_min_count(lf->size()));

        this->get_filters().get_enabled_mask(filter_in_mask, filter_out_mask);
        lfo->lfo_filter_state.tfs_index.clear();
        for (uint32_t lpc = 0; lpc < lf->size(); lpc++) {
            if (lfo->excluded(filter_in_mask, filter_out_mask, lpc)) {
                continue;
            }
            lfo->lfo_filter_state.tfs_index.push_back(lpc);
        }
    };

    int get_filtered_count() const {
        logfile *lf = this->current_file();
        int retval = 0;

        if (lf != NULL) {
            line_filter_observer *lfo = (line_filter_observer *) lf->get_logline_observer();
            retval = lf->size() - lfo->lfo_filter_state.tfs_index.size();
        }
        return retval;
    }

private:
    void detach_observer(logfile *lf) {
        line_filter_observer *lfo = (line_filter_observer *) lf->get_logline_observer();
        lf->set_logline_observer(NULL);
        delete lfo;
    };

    std::list<logfile *> tss_files;
};

#endif
