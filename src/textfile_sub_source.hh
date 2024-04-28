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
#include <unordered_map>

#include "filter_observer.hh"
#include "logfile.hh"
#include "plain_text_source.hh"
#include "text_overlay_menu.hh"
#include "textview_curses.hh"

class textfile_sub_source
    : public text_sub_source
    , public vis_location_history
    , public text_accel_source
    , public text_anchors {
public:
    using file_iterator = std::deque<std::shared_ptr<logfile>>::iterator;

    textfile_sub_source() { this->tss_supports_filtering = true; }

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

    bool to_front(const std::string& filename);

    void set_top_from_off(file_off_t off);

    void rotate_left();

    void rotate_right();

    void remove(const std::shared_ptr<logfile>& lf);

    void push_back(const std::shared_ptr<logfile>& lf);

    class scan_callback {
    public:
        virtual ~scan_callback() = default;

        virtual void closed_files(
            const std::vector<std::shared_ptr<logfile>>& files)
            = 0;
        virtual void promote_file(const std::shared_ptr<logfile>& lf) = 0;
        virtual void scanned_file(const std::shared_ptr<logfile>& lf) = 0;
        virtual void renamed_file(const std::shared_ptr<logfile>& lf) = 0;
    };

    struct rescan_result_t {
        size_t rr_new_data{0};
        bool rr_scan_completed{true};
    };

    rescan_result_t rescan_files(scan_callback& callback,
                                 std::optional<ui_clock::time_point> deadline
                                 = std::nullopt);

    void text_filters_changed() override;

    int get_filtered_count() const override;

    int get_filtered_count_for(size_t filter_index) const override;

    text_format_t get_text_format() const override;

    std::optional<location_history*> get_location_history() override
    {
        return this;
    }

    void text_crumbs_for_line(int line,
                              std::vector<breadcrumb::crumb>& crumbs) override;

    std::optional<vis_line_t> row_for_anchor(const std::string& id) override;

    std::optional<std::string> anchor_for_row(vis_line_t vl) override;

    std::optional<vis_line_t> adjacent_anchor(vis_line_t vl,
                                                 direction dir) override;

    std::unordered_set<std::string> get_anchors() override;

    void quiesce() override;

    bool is_time_offset_supported() const override
    {
        const auto lf = this->current_file();
        if (lf != nullptr && lf->has_line_metadata()) {
            return true;
        }

        return false;
    }

    logline* text_accel_get_line(vis_line_t vl) override;

    void scroll_invoked(textview_curses* tc) override;

private:
    void detach_observer(std::shared_ptr<logfile> lf)
    {
        auto* lfo = (line_filter_observer*) lf->get_logline_observer();
        lf->set_logline_observer(nullptr);
        delete lfo;
    }

    struct rendered_file {
        time_t rf_mtime;
        file_ssize_t rf_file_size;
        std::unique_ptr<plain_text_source> rf_text_source;
    };

    struct metadata_state {
        time_t ms_mtime;
        file_ssize_t ms_file_size;
        lnav::document::metadata ms_metadata;
    };

    std::deque<std::shared_ptr<logfile>> tss_files;
    std::deque<std::shared_ptr<logfile>> tss_hidden_files;
    std::unordered_map<std::string, rendered_file> tss_rendered_files;
    std::unordered_map<std::string, metadata_state> tss_doc_metadata;
    size_t tss_line_indent_size{0};
    bool tss_completed_last_scan{true};
    attr_line_t tss_hex_line;
    int64_t tss_content_line{0};
};

class textfile_header_overlay : public text_overlay_menu {
public:
    explicit textfile_header_overlay(textfile_sub_source* src);

    bool list_static_overlay(const listview_curses& lv,
                             int y,
                             int bottom,
                             attr_line_t& value_out) override;

private:
    textfile_sub_source* tho_src;
};

#endif
