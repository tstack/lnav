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
 * @file hist_source.hh
 */

#ifndef hist_source_hh
#define hist_source_hh

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/lnav_log.hh"
#include "mapbox/variant.hpp"
#include "strong_int.hh"
#include "textview_curses.hh"

/** Type for indexes into a group of buckets. */
STRONG_INT_TYPE(int, bucket_group);

/** Type used to differentiate values added to the same row in the histogram */
STRONG_INT_TYPE(int, bucket_type);

struct stacked_bar_chart_base {
    virtual ~stacked_bar_chart_base() = default;

    struct show_none {};
    struct show_all {};
    struct show_one {
        size_t so_index;

        explicit show_one(int so_index) : so_index(so_index) {}
    };

    using show_state = mapbox::util::variant<show_none, show_all, show_one>;

    enum class direction {
        forward,
        backward,
    };
};

template<typename T>
class stacked_bar_chart : public stacked_bar_chart_base {
public:
    stacked_bar_chart& with_stacking_enabled(bool enabled)
    {
        this->sbc_do_stacking = enabled;
        return *this;
    }

    stacked_bar_chart& with_attrs_for_ident(const T& ident, text_attrs attrs)
    {
        auto& ci = this->find_ident(ident);
        ci.ci_attrs = attrs;
        return *this;
    }

    stacked_bar_chart& with_margins(unsigned long left, unsigned long right)
    {
        this->sbc_left = left;
        this->sbc_right = right;
        return *this;
    }

    stacked_bar_chart& with_show_state(show_state ss)
    {
        this->sbc_show_state = ss;
        return *this;
    }

    bool attrs_in_use(const text_attrs& attrs) const
    {
        for (const auto& ident : this->sbc_idents) {
            if (ident.ci_attrs == attrs) {
                return true;
            }
        }
        return false;
    }

    show_state show_next_ident(direction dir)
    {
        bool single_ident = this->sbc_idents.size() == 1;

        if (this->sbc_idents.empty()) {
            return this->sbc_show_state;
        }

        this->sbc_show_state = this->sbc_show_state.match(
            [&](show_none) -> show_state {
                switch (dir) {
                    case direction::forward:
                        if (single_ident) {
                            return show_all();
                        }
                        return show_one(0);
                    case direction::backward:
                        return show_all();
                }

                return show_all();
            },
            [&](show_one& one) -> show_state {
                switch (dir) {
                    case direction::forward:
                        if (one.so_index + 1 == this->sbc_idents.size()) {
                            return show_all();
                        }
                        return show_one(one.so_index + 1);
                    case direction::backward:
                        if (one.so_index == 0) {
                            return show_none();
                        }
                        return show_one(one.so_index - 1);
                }

                return show_all();
            },
            [&](show_all) -> show_state {
                switch (dir) {
                    case direction::forward:
                        return show_none();
                    case direction::backward:
                        if (single_ident) {
                            return show_none();
                        }
                        return show_one(this->sbc_idents.size() - 1);
                }

                return show_all();
            });

        return this->sbc_show_state;
    }

    void get_ident_to_show(T& ident_out) const
    {
        this->sbc_show_state.match(
            [](const show_none) {},
            [](const show_all) {},
            [&](const show_one& one) {
                ident_out = this->sbc_idents[one.so_index].ci_ident;
            });
    }

    void chart_attrs_for_value(const listview_curses& lc,
                               int& left,
                               const T& ident,
                               double value,
                               string_attrs_t& value_out) const
    {
        auto ident_iter = this->sbc_ident_lookup.find(ident);

        require(ident_iter != this->sbc_ident_lookup.end());

        size_t ident_index = ident_iter->second;
        unsigned long width, avail_width;
        bucket_stats_t overall_stats;
        struct line_range lr;
        vis_line_t height;

        lr.lr_unit = line_range::unit::codepoint;

        size_t ident_to_show = this->sbc_show_state.match(
            [](const show_none) { return -1; },
            [ident_index](const show_all) { return ident_index; },
            [](const show_one& one) { return one.so_index; });

        if (ident_to_show != ident_index) {
            return;
        }

        lc.get_dimensions(height, width);

        for (size_t lpc = 0; lpc < this->sbc_idents.size(); lpc++) {
            if (this->sbc_show_state.template is<show_all>()
                || lpc == (size_t) ident_to_show)
            {
                overall_stats.merge(this->sbc_idents[lpc].ci_stats,
                                    this->sbc_do_stacking);
            }
        }

        if (this->sbc_show_state.template is<show_all>()) {
            if (width < this->sbc_idents.size()) {
                avail_width = 0;
            } else {
                avail_width = width - this->sbc_idents.size();
            }
        } else {
            avail_width = width - 1;
        }
        if (avail_width > (this->sbc_left + this->sbc_right)) {
            avail_width -= this->sbc_left + this->sbc_right;
        }

        lr.lr_start = left;

        const auto& ci = this->sbc_idents[ident_index];
        int amount;

        if (value == 0.0 || avail_width < 0) {
            amount = 0;
        } else if ((overall_stats.bs_max_value - 0.01) <= value
                   && value <= (overall_stats.bs_max_value + 0.01))
        {
            amount = avail_width;
        } else {
            double percent
                = (value - overall_stats.bs_min_value) / overall_stats.width();
            amount = (int) rint(percent * avail_width);
            amount = std::max(1, amount);
        }
        require_ge(amount, 0);
        lr.lr_end = left = lr.lr_start + amount;

        if (!ci.ci_attrs.empty() && !lr.empty()) {
            auto rev_attrs = ci.ci_attrs;
            rev_attrs.ta_attrs |= A_REVERSE;
            value_out.emplace_back(lr, VC_STYLE.value(rev_attrs));
        }
    }

    void clear()
    {
        this->sbc_idents.clear();
        this->sbc_ident_lookup.clear();
        this->sbc_show_state = show_none();
    }

    void add_value(const T& ident, double amount = 1.0)
    {
        struct chart_ident& ci = this->find_ident(ident);
        ci.ci_stats.update(amount);
    }

    struct bucket_stats_t {
        bucket_stats_t()
            : bs_min_value(std::numeric_limits<double>::max()), bs_max_value(0)
        {
        }

        void merge(const bucket_stats_t& rhs, bool do_stacking)
        {
            this->bs_min_value = std::min(this->bs_min_value, rhs.bs_min_value);
            if (do_stacking) {
                this->bs_max_value += rhs.bs_max_value;
            } else {
                this->bs_max_value
                    = std::max(this->bs_max_value, rhs.bs_max_value);
            }
        }

        double width() const
        {
            return std::fabs(this->bs_max_value - this->bs_min_value);
        }

        void update(double value)
        {
            this->bs_max_value = std::max(this->bs_max_value, value);
            this->bs_min_value = std::min(this->bs_min_value, value);
        }

        double bs_min_value;
        double bs_max_value;
    };

    const bucket_stats_t& get_stats_for(const T& ident)
    {
        const chart_ident& ci = this->find_ident(ident);

        return ci.ci_stats;
    }

protected:
    struct chart_ident {
        explicit chart_ident(const T& ident) : ci_ident(ident) {}

        T ci_ident;
        text_attrs ci_attrs;
        bucket_stats_t ci_stats;
    };

    struct chart_ident& find_ident(const T& ident)
    {
        auto iter = this->sbc_ident_lookup.find(ident);
        if (iter == this->sbc_ident_lookup.end()) {
            this->sbc_ident_lookup[ident] = this->sbc_idents.size();
            this->sbc_idents.emplace_back(ident);
            return this->sbc_idents.back();
        }
        return this->sbc_idents[iter->second];
    }

    bool sbc_do_stacking{true};
    unsigned long sbc_left{0}, sbc_right{0};
    std::vector<struct chart_ident> sbc_idents;
    std::unordered_map<T, unsigned int> sbc_ident_lookup;
    show_state sbc_show_state{show_none()};
};

class hist_source2
    : public text_sub_source
    , public text_time_translator {
public:
    typedef enum {
        HT_NORMAL,
        HT_WARNING,
        HT_ERROR,
        HT_MARK,

        HT__MAX
    } hist_type_t;

    hist_source2() { this->clear(); }

    ~hist_source2() override = default;

    void init();

    void set_time_slice(int64_t slice) { this->hs_time_slice = slice; }

    int64_t get_time_slice() const { return this->hs_time_slice; }

    size_t text_line_count() override { return this->hs_line_count; }

    size_t text_line_width(textview_curses& curses) override
    {
        return 48 + 8 * 4;
    }

    void clear();

    void add_value(time_t row, hist_type_t htype, double value = 1.0);

    void end_of_row();

    void text_value_for_line(textview_curses& tc,
                             int row,
                             std::string& value_out,
                             line_flags_t flags) override;

    void text_attrs_for_line(textview_curses& tc,
                             int row,
                             string_attrs_t& value_out) override;

    size_t text_size_for_line(textview_curses& tc,
                              int row,
                              line_flags_t flags) override
    {
        return 0;
    }

    nonstd::optional<row_info> time_for_row(vis_line_t row) override;

    nonstd::optional<vis_line_t> row_for_time(
        struct timeval tv_bucket) override;

private:
    struct hist_value {
        double hv_value;
    };

    struct bucket_t {
        time_t b_time;
        hist_value b_values[HT__MAX];
    };

    static const int64_t BLOCK_SIZE = 100;

    struct bucket_block {
        bucket_block()
        {
            memset(this->bb_buckets, 0, sizeof(this->bb_buckets));
        }

        unsigned int bb_used{0};
        bucket_t bb_buckets[BLOCK_SIZE];
    };

    bucket_t& find_bucket(int64_t index);

    int64_t hs_time_slice{10 * 60};
    int64_t hs_line_count;
    int64_t hs_last_bucket;
    time_t hs_last_row;
    std::map<int64_t, struct bucket_block> hs_blocks;
    stacked_bar_chart<hist_type_t> hs_chart;
};

#endif
