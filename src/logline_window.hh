/**
 * Copyright (c) 2025, Timothy Stack
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

#ifndef lnav_logline_window_hh
#define lnav_logline_window_hh

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "base/attr_line.hh"
#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/result.h"
#include "bookmarks.hh"
#include "log_format.hh"
#include "strong_int.hh"
#include "vis_line.hh"

STRONG_INT_TYPE(uint64_t, content_line);

class logfile_sub_source;

class logline_window {
public:
    logline_window(logfile_sub_source& lss,
                   vis_line_t start_vl,
                   vis_line_t end_vl)
        : lw_source(lss), lw_start_line(start_vl), lw_end_line(end_vl)
    {
    }

    class iterator;

    class logmsg_info {
    public:
        logmsg_info(logfile_sub_source& lss, vis_line_t vl);

        vis_line_t get_vis_line() const { return this->li_line; }

        size_t get_line_count() const;

        uint32_t get_file_line_number() const { return this->li_line_number; }

        logfile* get_file_ptr() const { return this->li_file; }

        logline& get_logline() const { return *this->li_logline; }

        const string_attrs_t& get_attrs() const
        {
            this->load_msg();
            return this->li_string_attrs;
        }

        const logline_value_vector& get_values() const
        {
            this->load_msg();
            return this->li_line_values;
        }

        std::optional<bookmark_metadata*> get_metadata() const;

        Result<auto_buffer, std::string> get_line_hash() const;

        struct metadata_edit_guard {
            ~metadata_edit_guard();

            bookmark_metadata& operator*();

        private:
            friend logmsg_info;

            explicit metadata_edit_guard(logmsg_info& li) : meg_logmsg_info(li)
            {
            }
            logmsg_info& meg_logmsg_info;
        };

        metadata_edit_guard edit_metadata()
        {
            return metadata_edit_guard{*this};
        }

        template<typename T>
        std::optional<string_fragment> get_string_for_attr(
            const string_attr_type<T>& sat) const
        {
            this->load_msg();

            auto attr_opt = get_string_attr(this->get_attrs(), sat);
            if (!attr_opt) {
                return std::nullopt;
            }

            return this->to_string_fragment(
                attr_opt->saw_string_attr->sa_range);
        }

        string_fragment to_string_fragment(const line_range& lr) const;

        // True when this visible row is the lead of a metric-sibling
        // fan-out — i.e. it's a metrics-format logline.  Only leads
        // can have suppressed siblings behind them.
        bool is_metric_line() const;

        // Optional stop conditions on the sibling walk.  Default off
        // to match the renderer/mark/spectrogram callers, which want
        // every suppressed same-timestamp sibling.  `metrics_vtab`
        // enables `skip_filtered_out` so the vtab respects the LOG
        // view's filters.
        struct sibling_policy {
            // Skip rows that the active filters are hiding.
            bool skip_filtered_out{false};
        };

        // Per-sibling record yielded by the sibling range.  Values and
        // attrs are lazily loaded on first access so callers that only
        // need the logfile/line number don't pay for an annotate pass.
        class sibling_info {
        public:
            logfile* get_file_ptr() const { return this->si_lf; }
            uint64_t get_file_line_number() const
            {
                return this->si_line_number;
            }
            content_line_t get_content_line() const { return this->si_cl; }
            logline& get_logline() const;
            const logline_value_vector& get_values() const
            {
                this->load();
                return this->si_values;
            }
            const string_attrs_t& get_attrs() const
            {
                this->load();
                return this->si_attrs;
            }

        private:
            friend class logmsg_info;

            logfile* si_lf{nullptr};
            uint64_t si_line_number{0};
            content_line_t si_cl{0};
            mutable logline_value_vector si_values;
            mutable string_attrs_t si_attrs;
            mutable bool si_loaded{false};

            void load() const;
        };

        // A lazy range over the metric-sibling fan-out behind this
        // logmsg_info: the lead row itself at offset 0, followed by
        // every metric sibling that `rebuild_index` collapsed into
        // this visible line.  Empty when `is_metric_line()` is false.
        class sibling_range {
        public:
            class iterator {
            public:
                struct end_tag {};
                iterator(const sibling_range* range, size_t offset);
                iterator(const sibling_range* range, end_tag)
                    : i_range(range), i_offset(0), i_done(true)
                {
                }

                iterator& operator++();

                bool operator==(const iterator& other) const
                {
                    if (this->i_range != other.i_range) {
                        return false;
                    }
                    if (this->i_done && other.i_done) {
                        return true;
                    }
                    if (this->i_done != other.i_done) {
                        return false;
                    }
                    return this->i_offset == other.i_offset;
                }

                bool operator!=(const iterator& other) const
                {
                    return !(*this == other);
                }

                const sibling_info& operator*() const { return this->i_info; }
                const sibling_info* operator->() const { return &this->i_info; }

            private:
                void settle();

                const sibling_range* i_range;
                size_t i_offset;
                bool i_done{false};
                sibling_info i_info;
            };

            iterator begin() const { return iterator{this, 0}; }
            iterator end() const
            {
                return iterator{this, iterator::end_tag{}};
            }

        private:
            friend class logmsg_info;
            friend class iterator;

            sibling_range(logfile_sub_source& lss,
                          size_t lead_idx,
                          std::chrono::microseconds lead_time,
                          sibling_policy policy);

            // Outcome of `at(offset)`.  `ok` yields a sibling; `skip`
            // means the offset is filter-hidden (`iterator::settle`
            // transparently advances past it); `end` terminates the
            // run.
            enum class outcome {
                ok,
                skip,
                end,
            };
            struct result {
                outcome oc{outcome::end};
                sibling_info info;
            };

            // Point query — the primitive that `iterator::settle`
            // calls to advance.  Private: cursor-style callers park
            // the iterator itself instead of indexing by offset.
            result at(size_t offset) const;

            logfile_sub_source& sr_lss;
            size_t sr_lead_idx;
            std::chrono::microseconds sr_lead_time;
            sibling_policy sr_policy;
            uint32_t sr_filter_in_mask{0};
            uint32_t sr_filter_out_mask{0};
        };

        // Iterate the metric-sibling fan-out — this visible row's
        // lead as the first element, then each suppressed sibling
        // `rebuild_index` collapsed into the same visible line.
        // Returns an empty range when this isn't a metric line.
        sibling_range metric_siblings() const
        {
            return this->metric_siblings(sibling_policy{});
        }

        sibling_range metric_siblings(sibling_policy policy) const;

    private:
        friend iterator;
        friend metadata_edit_guard;
        friend logline_window;

        void next_msg();
        void prev_msg();
        void load_msg() const;
        bool is_valid() const;

        logfile_sub_source& li_source;
        vis_line_t li_line;
        uint32_t li_line_number;
        logfile* li_file{nullptr};
        logfile::iterator li_logline;
        mutable string_attrs_t li_string_attrs;
        mutable logline_value_vector li_line_values;
    };

    class iterator {
    public:
        iterator(logfile_sub_source& lss, vis_line_t vl) : i_info(lss, vl) {}

        iterator& operator++();
        iterator& operator--();

        bool operator!=(const iterator& rhs) const
        {
            return this->i_info.get_vis_line() != rhs.i_info.get_vis_line();
        }

        bool operator==(const iterator& rhs) const
        {
            return this->i_info.get_vis_line() == rhs.i_info.get_vis_line();
        }

        const logmsg_info& operator*() const { return this->i_info; }

        const logmsg_info* operator->() const { return &this->i_info; }

    private:
        logmsg_info i_info;
    };

    iterator begin();

    iterator end();

private:
    logfile_sub_source& lw_source;
    vis_line_t lw_start_line;
    vis_line_t lw_end_line;
};

#endif
