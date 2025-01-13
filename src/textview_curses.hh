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
 *
 * @file textview_curses.hh
 */

#ifndef textview_curses_hh
#define textview_curses_hh

#include <utility>
#include <vector>

#include "base/func_util.hh"
#include "base/lnav_log.hh"
#include "bookmarks.hh"
#include "breadcrumb.hh"
#include "grep_proc.hh"
#include "highlighter.hh"
#include "listview_curses.hh"
#include "lnav_config_fwd.hh"
#include "log_accel.hh"
#include "logfile_fwd.hh"
#include "ring_span.hh"
#include "text_format.hh"
#include "textview_curses_fwd.hh"
#include "vis_line.hh"

class textview_curses;

using vis_bookmarks = bookmarks<vis_line_t>::type;

class logfile_filter_state {
public:
    logfile_filter_state(std::shared_ptr<logfile> lf = nullptr);

    void clear();

    void clear_filter_state(size_t index);

    void clear_deleted_filter_state(uint32_t used_mask);

    void resize(size_t newsize);

    void reserve(size_t expected);

    std::optional<size_t> content_line_to_vis_line(uint32_t line);

    const static int MAX_FILTERS = 32;

    std::shared_ptr<logfile> tfs_logfile;
    size_t tfs_filter_count[MAX_FILTERS];
    int tfs_filter_hits[MAX_FILTERS];
    bool tfs_message_matched[MAX_FILTERS];
    size_t tfs_lines_for_message[MAX_FILTERS];
    bool tfs_last_message_matched[MAX_FILTERS];
    size_t tfs_last_lines_for_message[MAX_FILTERS];
    std::vector<uint32_t> tfs_mask;
    std::vector<uint32_t> tfs_index;
};

enum class filter_lang_t : int {
    NONE,
    REGEX,
    SQL,
};

class text_filter {
public:
    typedef enum {
        INCLUDE,
        EXCLUDE,
    } type_t;

    text_filter(type_t type, filter_lang_t lang, std::string id, size_t index)
        : lf_type(type), lf_lang(lang), lf_id(std::move(id)), lf_index(index)
    {
    }
    virtual ~text_filter() = default;

    type_t get_type() const { return this->lf_type; }
    filter_lang_t get_lang() const { return this->lf_lang; }
    void set_type(type_t t) { this->lf_type = t; }
    std::string get_id() const { return this->lf_id; }
    void set_id(std::string id) { this->lf_id = std::move(id); }
    size_t get_index() const { return this->lf_index; }

    bool is_enabled() const { return this->lf_enabled; }
    void enable() { this->lf_enabled = true; }
    void disable() { this->lf_enabled = false; }
    void set_enabled(bool value) { this->lf_enabled = value; }

    void revert_to_last(logfile_filter_state& lfs, size_t rollback_size);

    void add_line(logfile_filter_state& lfs,
                  logfile_const_iterator ll,
                  const shared_buffer_ref& line);

    void end_of_message(logfile_filter_state& lfs);

    struct line_source {
        const logfile& ls_file;
        logfile_const_iterator ls_line;
    };

    virtual bool matches(std::optional<line_source> ls,
                         const shared_buffer_ref& line)
        = 0;

    virtual std::string to_command() const = 0;

    bool operator==(const std::string& rhs) const { return this->lf_id == rhs; }

    bool lf_deleted{false};

protected:
    bool lf_enabled{true};
    type_t lf_type;
    filter_lang_t lf_lang;
    std::string lf_id;
    size_t lf_index;
};

class empty_filter : public text_filter {
public:
    empty_filter(type_t type, size_t index)
        : text_filter(type, filter_lang_t::REGEX, "", index)
    {
    }

    bool matches(std::optional<line_source> ls,
                 const shared_buffer_ref& line) override;

    std::string to_command() const override;
};

class pcre_filter : public text_filter {
public:
    pcre_filter(type_t type,
                const std::string& id,
                size_t index,
                std::shared_ptr<lnav::pcre2pp::code> code)
        : text_filter(type, filter_lang_t::REGEX, id, index),
          pf_pcre(std::move(code))
    {
    }

    ~pcre_filter() override = default;

    bool matches(std::optional<line_source> ls,
                 const shared_buffer_ref& line) override
    {
        return this->pf_pcre->find_in(line.to_string_fragment())
            .ignore_error()
            .has_value();
    }

    std::string to_command() const override
    {
        return (this->lf_type == text_filter::INCLUDE ? "filter-in "
                                                      : "filter-out ")
            + this->lf_id;
    }

protected:
    std::shared_ptr<lnav::pcre2pp::code> pf_pcre;
};

class filter_stack {
public:
    using iterator = std::vector<std::shared_ptr<text_filter>>::iterator;
    using const_iterator
        = std::vector<std::shared_ptr<text_filter>>::const_iterator;
    using value_type = std::shared_ptr<text_filter>;

    explicit filter_stack(size_t reserved = 0) : fs_reserved(reserved) {}

    iterator begin() { return this->fs_filters.begin(); }

    iterator end() { return this->fs_filters.end(); }

    const_iterator begin() const { return this->fs_filters.begin(); }

    const_iterator end() const { return this->fs_filters.end(); }

    size_t size() const { return this->fs_filters.size(); }

    bool empty() const { return this->fs_filters.empty(); };

    bool full() const
    {
        return (this->fs_reserved + this->fs_filters.size())
            == logfile_filter_state::MAX_FILTERS;
    }

    std::optional<size_t> next_index();

    void add_filter(const std::shared_ptr<text_filter>& filter);

    void clear_filters()
    {
        while (!this->fs_filters.empty()) {
            this->fs_filters.pop_back();
        }
    }

    void set_filter_enabled(const std::shared_ptr<text_filter>& filter,
                            bool enabled)
    {
        if (enabled) {
            filter->enable();
        } else {
            filter->disable();
        }
    }

    std::shared_ptr<text_filter> get_filter(const std::string& id);

    bool delete_filter(const std::string& id);

    void get_mask(uint32_t& filter_mask);

    void get_enabled_mask(uint32_t& filter_in_mask, uint32_t& filter_out_mask);

private:
    const size_t fs_reserved;
    std::vector<std::shared_ptr<text_filter>> fs_filters;
};

class text_time_translator {
public:
    struct row_info {
        row_info() = default;

        row_info(struct timeval tv, int64_t id) : ri_time(tv), ri_id(id) {}

        struct timeval ri_time{0, 0};
        int64_t ri_id{-1};
    };

    virtual ~text_time_translator() = default;

    virtual std::optional<vis_line_t> row_for_time(struct timeval time_bucket)
        = 0;

    virtual std::optional<vis_line_t> row_for(const row_info& ri)
    {
        return this->row_for_time(ri.ri_time);
    }

    virtual std::optional<row_info> time_for_row(vis_line_t row) = 0;

    void data_reloaded(textview_curses* tc);

    void ttt_scroll_invoked(textview_curses* tc);

protected:
    std::optional<row_info> ttt_top_row_info;
};

class text_accel_source {
public:
    virtual ~text_accel_source() = default;

    virtual log_accel::direction_t get_line_accel_direction(vis_line_t vl);

    void toggle_time_offset()
    {
        this->tas_display_time_offset = !this->tas_display_time_offset;
        this->text_accel_display_changed();
    }

    void set_time_offset(bool enabled)
    {
        if (this->tas_display_time_offset != enabled) {
            this->tas_display_time_offset = enabled;
            this->text_accel_display_changed();
        }
    }

    bool is_time_offset_enabled() const
    {
        return this->tas_display_time_offset;
    }

    virtual bool is_time_offset_supported() const { return true; }

    virtual logline* text_accel_get_line(vis_line_t vl) = 0;

    std::string get_time_offset_for_line(textview_curses& tc, vis_line_t vl);

protected:
    virtual void text_accel_display_changed() {}

    bool tas_display_time_offset{false};
};

class text_anchors {
public:
    virtual ~text_anchors() = default;

    static std::string to_anchor_string(const std::string& raw);

    virtual std::optional<vis_line_t> row_for_anchor(const std::string& id) = 0;

    enum class direction {
        prev,
        next,
    };

    virtual std::optional<vis_line_t> adjacent_anchor(vis_line_t vl,
                                                      direction dir)
    {
        return std::nullopt;
    }

    virtual std::optional<std::string> anchor_for_row(vis_line_t vl) = 0;

    virtual std::unordered_set<std::string> get_anchors() = 0;
};

class location_history {
public:
    virtual ~location_history() = default;

    virtual void loc_history_append(vis_line_t top) = 0;

    virtual std::optional<vis_line_t> loc_history_back(vis_line_t current_top)
        = 0;

    virtual std::optional<vis_line_t> loc_history_forward(
        vis_line_t current_top)
        = 0;

    const static int MAX_SIZE = 100;

protected:
    size_t lh_history_position{0};
};

/**
 * Source for the text to be shown in a textview_curses view.
 */
class text_sub_source {
public:
    virtual ~text_sub_source() = default;

    enum {
        RB_RAW,
        RB_FULL,
        RB_REWRITE,
    };

    enum {
        RF_RAW = (1UL << RB_RAW),
        RF_FULL = (1UL << RB_FULL),
        RF_REWRITE = (1UL << RB_REWRITE),
    };

    typedef long line_flags_t;

    text_sub_source(size_t reserved_filters = 0) : tss_filters(reserved_filters)
    {
    }

    virtual void register_view(textview_curses* tc) { this->tss_view = tc; }

    /**
     * @return The total number of lines available from the source.
     */
    virtual size_t text_line_count() = 0;

    virtual size_t text_line_width(textview_curses& curses) { return INT_MAX; }

    virtual bool text_is_row_selectable(textview_curses& tc, vis_line_t row)
    {
        return true;
    }

    virtual void text_selection_changed(textview_curses& tc) {}

    /**
     * Get the value for a line.
     *
     * @param tc The textview_curses object that is delegating control.
     * @param line The line number to retrieve.
     * @param value_out The string object that should be set to the line
     *   contents.
     * @param raw Indicates that the raw contents of the line should be returned
     *   without any post processing.
     */
    virtual line_info text_value_for_line(textview_curses& tc,
                                          int line,
                                          std::string& value_out,
                                          line_flags_t flags = 0)
        = 0;

    virtual size_t text_size_for_line(textview_curses& tc,
                                      int line,
                                      line_flags_t raw = 0)
        = 0;

    /**
     * Inform the source that the given line has been marked/unmarked.  This
     * callback function can be used to translate between between visible line
     * numbers and content line numbers.  For example, when viewing a log file
     * with filters being applied, we want the bookmarked lines to be stable
     * across changes in the filters.
     *
     * @param bm    The type of bookmark.
     * @param line  The line that has been marked/unmarked.
     * @param added True if the line was bookmarked and false if it was
     *   unmarked.
     */
    virtual void text_mark(const bookmark_type_t* bm,
                           vis_line_t line,
                           bool added)
    {
    }

    /**
     * Clear the bookmarks for a particular type in the text source.
     *
     * @param bm The type of bookmarks to clear.
     */
    virtual void text_clear_marks(const bookmark_type_t* bm) {}

    /**
     * Get the attributes for a line of text.
     *
     * @param tc The textview_curses object that is delegating control.
     * @param line The line number to retrieve.
     * @param value_out A string_attrs_t object that should be updated with the
     *   attributes for the line.
     */
    virtual void text_attrs_for_line(textview_curses& tc,
                                     int line,
                                     string_attrs_t& value_out)
    {
    }

    /**
     * Update the bookmarks used by the text view based on the bookmarks
     * maintained by the text source.
     *
     * @param bm The bookmarks data structure used by the text view.
     */
    virtual void text_update_marks(vis_bookmarks& bm) {}

    virtual std::string text_source_name(const textview_curses& tv)
    {
        return "";
    }

    filter_stack& get_filters() { return this->tss_filters; }

    virtual void text_filters_changed() {}

    virtual int get_filtered_count() const { return 0; }

    virtual int get_filtered_count_for(size_t filter_index) const { return 0; }

    virtual text_format_t get_text_format() const
    {
        return text_format_t::TF_UNKNOWN;
    }

    virtual std::optional<
        std::pair<grep_proc_source<vis_line_t>*, grep_proc_sink<vis_line_t>*>>
    get_grepper()
    {
        return std::nullopt;
    }

    virtual std::optional<location_history*> get_location_history()
    {
        return std::nullopt;
    }

    void toggle_apply_filters();

    virtual void text_crumbs_for_line(int line,
                                      std::vector<breadcrumb::crumb>& crumbs);

    virtual void quiesce() {}

    virtual void scroll_invoked(textview_curses* tc);

    bool tss_supports_filtering{false};
    bool tss_apply_filters{true};

protected:
    textview_curses* tss_view{nullptr};
    filter_stack tss_filters;
};

class vis_location_history : public location_history {
public:
    vis_location_history()
        : vlh_history(std::begin(this->vlh_backing),
                      std::end(this->vlh_backing))
    {
    }

    void loc_history_append(vis_line_t top) override;

    std::optional<vis_line_t> loc_history_back(vis_line_t current_top) override;

    std::optional<vis_line_t> loc_history_forward(
        vis_line_t current_top) override;

    nonstd::ring_span<vis_line_t> vlh_history;

private:
    vis_line_t current_position()
    {
        auto iter = this->vlh_history.rbegin();

        iter += this->lh_history_position;

        return *iter;
    }

    vis_line_t vlh_backing[MAX_SIZE];
};

class text_delegate {
public:
    virtual ~text_delegate() = default;

    virtual bool text_handle_mouse(
        textview_curses& tc,
        const listview_curses::display_line_content_t&,
        mouse_event& me)
    {
        return false;
    }
};

/**
 * The textview_curses class adds user bookmarks and searching to the standard
 * list view interface.
 */
class textview_curses
    : public listview_curses
    , public list_data_source
    , public grep_proc_source<vis_line_t>
    , public grep_proc_sink<vis_line_t>
    , public lnav_config_listener {
public:
    using action = std::function<void(textview_curses*)>;

    const static bookmark_type_t BM_USER;
    const static bookmark_type_t BM_USER_EXPR;
    const static bookmark_type_t BM_SEARCH;
    const static bookmark_type_t BM_META;
    const static bookmark_type_t BM_PARTITION;

    textview_curses();

    ~textview_curses();

    void reload_config(error_reporter& reporter);

    void set_paused(bool paused)
    {
        this->tc_paused = paused;
        if (this->tc_state_event_handler) {
            this->tc_state_event_handler(*this);
        }
    }

    bool is_paused() const { return this->tc_paused; }

    vis_bookmarks& get_bookmarks() { return this->tc_bookmarks; }

    const vis_bookmarks& get_bookmarks() const { return this->tc_bookmarks; }

    void toggle_user_mark(const bookmark_type_t* bm,
                          vis_line_t start_line,
                          vis_line_t end_line = vis_line_t(-1));

    void set_user_mark(const bookmark_type_t* bm, vis_line_t vl, bool marked);

    textview_curses& set_sub_source(text_sub_source* src);

    text_sub_source* get_sub_source() const { return this->tc_sub_source; }

    textview_curses& set_supports_marks(bool m)
    {
        this->tc_supports_marks = m;
        return *this;
    }

    textview_curses& set_delegate(std::shared_ptr<text_delegate> del)
    {
        this->tc_delegate = del;

        return *this;
    }

    std::shared_ptr<text_delegate> get_delegate() const
    {
        return this->tc_delegate;
    }

    std::optional<std::pair<int, int>> horiz_shift(vis_line_t start,
                                                   vis_line_t end,
                                                   int off_start);

    void set_search_action(action sa)
    {
        this->tc_search_action = std::move(sa);
    }

    void grep_end_batch(grep_proc<vis_line_t>& gp);
    void grep_end(grep_proc<vis_line_t>& gp);

    size_t listview_rows(const listview_curses& lv)
    {
        return this->tc_sub_source == nullptr
            ? 0
            : this->tc_sub_source->text_line_count();
    }

    size_t listview_width(const listview_curses& lv)
    {
        return this->tc_sub_source == nullptr
            ? 0
            : this->tc_sub_source->text_line_width(*this);
    }

    void listview_value_for_rows(const listview_curses& lv,
                                 vis_line_t line,
                                 std::vector<attr_line_t>& rows_out);

    void textview_value_for_row(vis_line_t line, attr_line_t& value_out);

    bool listview_is_row_selectable(const listview_curses& lv, vis_line_t row);

    void listview_selection_changed(const listview_curses& lv);

    size_t listview_size_for_row(const listview_curses& lv, vis_line_t row)
    {
        return this->tc_sub_source->text_size_for_line(*this, row);
    }

    std::string listview_source_name(const listview_curses& lv)
    {
        return this->tc_sub_source == nullptr
            ? ""
            : this->tc_sub_source->text_source_name(*this);
    }

    std::optional<line_info> grep_value_for_line(vis_line_t line,
                                                 std::string& value_out);

    void grep_quiesce()
    {
        if (this->tc_sub_source != nullptr) {
            this->tc_sub_source->quiesce();
        }
    }

    void grep_begin(grep_proc<vis_line_t>& gp,
                    vis_line_t start,
                    vis_line_t stop);
    void grep_match(grep_proc<vis_line_t>& gp, vis_line_t line);

    bool is_searching() const { return this->tc_searching > 0; }

    void set_follow_search_for(int64_t ms_to_deadline,
                               std::function<bool()> func)
    {
        struct timeval now, tv;

        tv.tv_sec = ms_to_deadline / 1000;
        tv.tv_usec = (ms_to_deadline % 1000) * 1000;
        gettimeofday(&now, nullptr);
        timeradd(&now, &tv, &this->tc_follow_deadline);
        this->tc_follow_selection = this->get_selection();
        this->tc_follow_func = func;
    }

    size_t get_match_count() { return this->tc_bookmarks[&BM_SEARCH].size(); }

    void match_reset()
    {
        this->tc_bookmarks[&BM_SEARCH].clear();
        if (this->tc_sub_source != nullptr) {
            this->tc_sub_source->text_clear_marks(&BM_SEARCH);
        }
    }

    highlight_map_t& get_highlights() { return this->tc_highlights; }

    const highlight_map_t& get_highlights() const
    {
        return this->tc_highlights;
    }

    std::set<highlight_source_t>& get_disabled_highlights()
    {
        return this->tc_disabled_highlights;
    }

    bool handle_mouse(mouse_event& me);

    void reload_data();

    bool toggle_hide_fields()
    {
        bool retval = this->tc_hide_fields;

        this->tc_hide_fields = !this->tc_hide_fields;

        return retval;
    }

    bool get_hide_fields() const { return this->tc_hide_fields; }

    void set_hide_fields(bool val)
    {
        if (this->tc_hide_fields != val) {
            this->tc_hide_fields = val;
            this->set_needs_update();
        }
    }

    void execute_search(const std::string& regex_orig);

    void redo_search();

    void search_range(vis_line_t start, vis_line_t stop = -1_vl)
    {
        if (this->tc_search_child) {
            this->tc_search_child->get_grep_proc()->queue_request(start, stop);
        }
        if (this->tc_source_search_child) {
            this->tc_source_search_child->queue_request(start, stop);
        }
    }

    void search_new_data(vis_line_t start = -1_vl)
    {
        this->search_range(start);
        if (this->tc_search_child) {
            this->tc_search_child->get_grep_proc()->start();
        }
        if (this->tc_source_search_child) {
            this->tc_source_search_child->start();
        }
    }

    std::string get_current_search() const { return this->tc_current_search; }

    void save_current_search()
    {
        this->tc_previous_search = this->tc_current_search;
    }

    void revert_search() { this->execute_search(this->tc_previous_search); }

    void invoke_scroll();

    textview_curses& set_reload_config_delegate(
        std::function<void(textview_curses&)> func)
    {
        this->tc_reload_config_delegate = std::move(func);
        if (this->tc_reload_config_delegate) {
            this->tc_reload_config_delegate(*this);
        }
        return *this;
    }

    std::function<void(textview_curses&)> tc_state_event_handler;

    std::optional<role_t> tc_cursor_role;
    std::optional<role_t> tc_disabled_cursor_role;

    struct selected_text_info {
        int sti_x;
        int64_t sti_line;
        line_range sti_range;
        string_attrs_t sti_attrs;
        std::string sti_value;
        std::string sti_href;
    };

    std::optional<selected_text_info> tc_selected_text;
    bool tc_text_selection_active{false};
    display_line_content_t tc_press_line;
    int tc_press_left{0};

protected:
    class grep_highlighter {
    public:
        grep_highlighter(std::shared_ptr<grep_proc<vis_line_t>>& gp,
                         highlight_source_t source,
                         std::string hl_name,
                         highlight_map_t& hl_map)
            : gh_grep_proc(std::move(gp)), gh_hl_source(source),
              gh_hl_name(std::move(hl_name)), gh_hl_map(hl_map)
        {
        }

        ~grep_highlighter()
        {
            this->gh_hl_map.erase(
                this->gh_hl_map.find({this->gh_hl_source, this->gh_hl_name}));
        }

        grep_proc<vis_line_t>* get_grep_proc()
        {
            return this->gh_grep_proc.get();
        }

    private:
        std::shared_ptr<grep_proc<vis_line_t>> gh_grep_proc;
        highlight_source_t gh_hl_source;
        std::string gh_hl_name;
        highlight_map_t& gh_hl_map;
    };

    text_sub_source* tc_sub_source{nullptr};
    std::shared_ptr<text_delegate> tc_delegate;

    vis_bookmarks tc_bookmarks;

    int tc_searching{0};
    struct timeval tc_follow_deadline{0, 0};
    vis_line_t tc_follow_selection{-1_vl};
    std::function<bool()> tc_follow_func;
    action tc_search_action;

    highlight_map_t tc_highlights;
    std::set<highlight_source_t> tc_disabled_highlights;

    std::optional<vis_line_t> tc_selection_start;
    mouse_event tc_press_event;
    bool tc_hide_fields{true};
    bool tc_paused{false};
    bool tc_supports_marks{false};

    std::string tc_current_search;
    std::string tc_previous_search;
    std::shared_ptr<grep_highlighter> tc_search_child;
    std::shared_ptr<grep_proc<vis_line_t>> tc_source_search_child;
    std::function<void(textview_curses&)> tc_reload_config_delegate;
};

#endif
