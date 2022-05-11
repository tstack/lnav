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

#ifndef textfile_sub_source_hh
#define textfile_sub_source_hh

#include <deque>

#include "filter_observer.hh"
#include "logfile.hh"
#include "textview_curses.hh"

class textfile_sub_source
    : public text_sub_source
    , public vis_location_history {
public:
    using file_iterator = std::deque<std::shared_ptr<logfile>>::iterator;

    textfile_sub_source() { this->tss_supports_filtering = true; }

    ~textfile_sub_source() override = default;

    bool empty() const { return this->tss_files.empty(); }

    size_t size() const { return this->tss_files.size(); }

    size_t text_line_count() override;

    size_t text_line_width(textview_curses& curses) override
    {
        return this->tss_files.empty()
            ? 0
            : this->current_file()->get_longest_line_length();
    }

    void text_value_for_line(textview_curses& tc,
                             int line,
                             std::string& value_out,
                             line_flags_t flags) override;

    void text_attrs_for_line(textview_curses& tc,
                             int row,
                             string_attrs_t& value_out) override;

    size_t text_size_for_line(textview_curses& tc,
                              int line,
                              line_flags_t flags) override;

    std::shared_ptr<logfile> current_file() const
    {
        if (this->tss_files.empty()) {
            return nullptr;
        }

        return this->tss_files.front();
    }

    std::string text_source_name(const textview_curses& tv) override
    {
        if (this->tss_files.empty()) {
            return "";
        }

        return this->tss_files.front()->get_filename();
    }

    void to_front(const std::shared_ptr<logfile>& lf);

    void rotate_left();

    void rotate_right();

    void remove(const std::shared_ptr<logfile>& lf);

    void push_back(const std::shared_ptr<logfile>& lf);

    template<class T>
    bool rescan_files(T& callback,
                      nonstd::optional<ui_clock::time_point> deadline
                      = nonstd::nullopt)
    {
        file_iterator iter;
        bool retval = false;

        if (this->tss_view->is_paused()) {
            return retval;
        }

        std::vector<std::shared_ptr<logfile>> closed_files;
        for (iter = this->tss_files.begin(); iter != this->tss_files.end();) {
            std::shared_ptr<logfile> lf = (*iter);

            if (lf->is_closed()) {
                iter = this->tss_files.erase(iter);
                this->detach_observer(lf);
                closed_files.template emplace_back(lf);
                continue;
            }

            try {
                uint32_t old_size = lf->size();
                logfile::rebuild_result_t new_text_data
                    = lf->rebuild_index(deadline);

                if (lf->get_format() != nullptr) {
                    iter = this->tss_files.erase(iter);
                    this->detach_observer(lf);
                    callback.promote_file(lf);
                    continue;
                }

                switch (new_text_data) {
                    case logfile::rebuild_result_t::NEW_LINES:
                    case logfile::rebuild_result_t::NEW_ORDER:
                        retval = true;
                        break;
                    default:
                        break;
                }
                callback.scanned_file(lf);

                uint32_t filter_in_mask, filter_out_mask;

                this->get_filters().get_enabled_mask(filter_in_mask,
                                                     filter_out_mask);
                auto* lfo = (line_filter_observer*) lf->get_logline_observer();
                for (uint32_t lpc = old_size; lpc < lf->size(); lpc++) {
                    if (this->tss_apply_filters
                        && lfo->excluded(filter_in_mask, filter_out_mask, lpc))
                    {
                        continue;
                    }
                    lfo->lfo_filter_state.tfs_index.push_back(lpc);
                }
            } catch (const line_buffer::error& e) {
                iter = this->tss_files.erase(iter);
                lf->close();
                this->detach_observer(lf);
                closed_files.template emplace_back(lf);
                continue;
            }

            ++iter;
        }
        if (!closed_files.empty()) {
            callback.closed_files(closed_files);
        }

        if (retval) {
            this->tss_view->search_new_data();
        }

        return retval;
    }

    void text_filters_changed() override;

    int get_filtered_count() const override;

    int get_filtered_count_for(size_t filter_index) const override;

    text_format_t get_text_format() const override;

    nonstd::optional<location_history*> get_location_history() override
    {
        return this;
    }

    void text_crumbs_for_line(int line,
                              std::vector<breadcrumb::crumb>& crumbs) override;

private:
    void detach_observer(std::shared_ptr<logfile> lf)
    {
        auto* lfo = (line_filter_observer*) lf->get_logline_observer();
        lf->set_logline_observer(nullptr);
        delete lfo;
    }

    std::deque<std::shared_ptr<logfile>> tss_files;
    std::deque<std::shared_ptr<logfile>> tss_hidden_files;
};

#endif
