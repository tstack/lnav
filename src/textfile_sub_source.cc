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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <chrono>

#include "textfile_sub_source.hh"

#include <date/date.h>

#include "base/ansi_scrubber.hh"
#include "base/attr_line.builder.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/itertools.hh"
#include "base/map_util.hh"
#include "base/math_util.hh"
#include "bound_tags.hh"
#include "config.h"
#include "data_scanner.hh"
#include "lnav.events.hh"
#include "md2attr_line.hh"
#include "msg.text.hh"
#include "pretty_printer.hh"
#include "scn/scan.h"
#include "sql_util.hh"
#include "sqlitepp.hh"
#include "textfile_sub_source.cfg.hh"
#include "yajlpp/yajlpp_def.hh"

using namespace lnav::roles::literals;

static bool
file_needs_reformatting(const std::shared_ptr<logfile> lf)
{
    static const auto& cfg = injector::get<const lnav::textfile::config&>();

    switch (lf->get_text_format()) {
        case text_format_t::TF_BINARY:
        case text_format_t::TF_DIFF:
            return false;
        default:
            if (lf->get_longest_line_length()
                > cfg.c_max_unformatted_line_length)
            {
                return true;
            }
            return false;
    }
}

size_t
textfile_sub_source::text_line_count()
{
    size_t retval = 0;

    if (!this->tss_files.empty()) {
        const auto curr_iter = this->current_file_state();
        if (this->tss_view_mode == view_mode::raw
            || !curr_iter->fvs_text_source)
        {
            const auto& lf = curr_iter->fvs_file;
            if (lf->get_text_format() == text_format_t::TF_BINARY) {
                const auto fsize = lf->get_content_size();
                retval = fsize / 16;
                if (fsize % 16) {
                    retval += 1;
                }
            } else {
                auto* lfo = (line_filter_observer*) lf->get_logline_observer();
                if (lfo != nullptr) {
                    retval = lfo->lfo_filter_state.tfs_index.size();
                }
            }
        } else {
            retval = curr_iter->fvs_text_source->text_line_count();
        }
    }

    return retval;
}

line_info
textfile_sub_source::text_value_for_line(textview_curses& tc,
                                         int line,
                                         std::string& value_out,
                                         text_sub_source::line_flags_t flags)
{
    if (this->tss_files.empty() || line < 0) {
        value_out.clear();
        return {};
    }

    const auto curr_iter = this->current_file_state();
    const auto& lf = curr_iter->fvs_file;
    if (this->tss_view_mode == view_mode::rendered
        && curr_iter->fvs_text_source)
    {
        curr_iter->fvs_text_source->text_value_for_line(
            tc, line, value_out, flags);
        return {};
    }

    if (lf->get_text_format() == text_format_t::TF_BINARY) {
        this->tss_hex_line.clear();
        auto fsize = lf->get_content_size();
        auto fr = file_range{line * 16};
        fr.fr_size = std::min((file_ssize_t) 16, fsize - fr.fr_offset);

        auto read_res = lf->read_range(fr);
        if (read_res.isErr()) {
            log_error("%s: failed to read range %lld:%lld -- %s",
                      lf->get_path_for_key().c_str(),
                      fr.fr_offset,
                      fr.fr_size,
                      read_res.unwrapErr().c_str());
            return {};
        }

        auto sbr = read_res.unwrap();
        auto sf = sbr.to_string_fragment();
        attr_line_builder alb(this->tss_hex_line);
        {
            auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_FILE_OFFSET));
            alb.appendf(FMT_STRING("{: >16x} "), fr.fr_offset);
        }
        alb.append_as_hexdump(sf);
        auto alt_row_index = line % 4;
        if (alt_row_index == 2 || alt_row_index == 3) {
            this->tss_hex_line.with_attr_for_all(
                VC_ROLE.value(role_t::VCR_ALT_ROW));
        }

        value_out = this->tss_hex_line.get_string();
        return {};
    }

    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    if (lfo == nullptr
        || line >= (ssize_t) lfo->lfo_filter_state.tfs_index.size())
    {
        value_out.clear();
        return {};
    }

    const auto ll = lf->begin() + lfo->lfo_filter_state.tfs_index[line];
    auto read_result = lf->read_line(ll);
    this->tss_line_indent_size = 0;
    this->tss_plain_line_attrs.clear();
    if (read_result.isOk()) {
        auto sbr = read_result.unwrap();
        value_out = to_string(sbr);
        if (sbr.get_metadata().m_has_ansi) {
            scrub_ansi_string(value_out, &this->tss_plain_line_attrs);
        }
        for (const auto& ch : value_out) {
            if (ch == ' ') {
                this->tss_line_indent_size += 1;
            } else if (ch == '\t') {
                do {
                    this->tss_line_indent_size += 1;
                } while (this->tss_line_indent_size % 8);
            } else {
                break;
            }
        }
        if (lf->has_line_metadata() && this->tas_display_time_offset) {
            auto relstr = this->get_time_offset_for_line(tc, vis_line_t(line));
            value_out
                = fmt::format(FMT_STRING("{: >12}|{}"), relstr, value_out);
        }
    }

    return {};
}

void
textfile_sub_source::text_attrs_for_line(textview_curses& tc,
                                         int row,
                                         string_attrs_t& value_out)
{
    const auto curr_iter = this->current_file_state();
    if (curr_iter == this->tss_files.end()) {
        return;
    }
    const auto& lf = curr_iter->fvs_file;

    auto lr = line_range{0, -1};
    if (this->tss_view_mode == view_mode::rendered
        && curr_iter->fvs_text_source)
    {
        curr_iter->fvs_text_source->text_attrs_for_line(tc, row, value_out);
    } else if (lf->get_text_format() == text_format_t::TF_BINARY) {
        value_out = this->tss_hex_line.get_attrs();
    } else {
        value_out = this->tss_plain_line_attrs;
        auto* lfo
            = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
        if (lfo != nullptr && row >= 0
            && row < (ssize_t) lfo->lfo_filter_state.tfs_index.size())
        {
            auto ll = lf->begin() + lfo->lfo_filter_state.tfs_index[row];

            value_out.emplace_back(lr, SA_LEVEL.value(ll->get_msg_level()));
            if (lf->has_line_metadata() && this->tas_display_time_offset) {
                auto time_offset_end = 13;
                lr.lr_start = 0;
                lr.lr_end = time_offset_end;

                shift_string_attrs(value_out, 0, time_offset_end);

                value_out.emplace_back(lr,
                                       VC_ROLE.value(role_t::VCR_OFFSET_TIME));
                value_out.emplace_back(line_range(12, 13),
                                       VC_GRAPHIC.value(NCACS_VLINE));

                auto bar_role = role_t::VCR_NONE;

                switch (this->get_line_accel_direction(vis_line_t(row))) {
                    case log_accel::direction_t::A_STEADY:
                        break;
                    case log_accel::direction_t::A_DECEL:
                        bar_role = role_t::VCR_DIFF_DELETE;
                        break;
                    case log_accel::direction_t::A_ACCEL:
                        bar_role = role_t::VCR_DIFF_ADD;
                        break;
                }
                if (bar_role != role_t::VCR_NONE) {
                    value_out.emplace_back(line_range(12, 13),
                                           VC_ROLE.value(bar_role));
                }
            }

            if (curr_iter->fvs_metadata.m_sections_root) {
                auto ll_next_iter = ll + 1;
                auto end_offset = (ll_next_iter == lf->end())
                    ? lf->get_index_size() - 1
                    : ll_next_iter->get_offset() - 1;
                const auto& meta = curr_iter->fvs_metadata;
                meta.m_section_types_tree.visit_overlapping(
                    lf->get_line_content_offset(ll),
                    end_offset,
                    [&value_out, &ll, &lf, end_offset](const auto& iv) {
                        auto ll_offset = lf->get_line_content_offset(ll);
                        auto lr = line_range{0, -1};
                        if (iv.start > ll_offset) {
                            lr.lr_start = iv.start - ll_offset;
                        }
                        if (iv.stop < end_offset) {
                            lr.lr_end = iv.stop - ll_offset;
                        } else {
                            lr.lr_end = end_offset - ll_offset;
                        }
                        auto role = role_t::VCR_NONE;
                        switch (iv.value) {
                            case lnav::document::section_types_t::comment:
                                role = role_t::VCR_COMMENT;
                                break;
                            case lnav::document::section_types_t::
                                multiline_string:
                                role = role_t::VCR_STRING;
                                break;
                        }
                        value_out.emplace_back(lr, VC_ROLE.value(role));
                    });
                for (const auto& indent : meta.m_indents) {
                    if (indent < this->tss_line_indent_size) {
                        auto guide_lr = line_range{
                            (int) indent,
                            (int) (indent + 1),
                            line_range::unit::codepoint,
                        };
                        if (this->tas_display_time_offset) {
                            guide_lr.shift(0, 13);
                        }
                        value_out.emplace_back(
                            guide_lr,
                            VC_BLOCK_ELEM.value(block_elem_t{
                                L'\u258f', role_t::VCR_INDENT_GUIDE}));
                    }
                }
            }
        }
    }

    value_out.emplace_back(lr, L_FILE.value(this->current_file()));
}

size_t
textfile_sub_source::text_size_for_line(textview_curses& tc,
                                        int line,
                                        text_sub_source::line_flags_t flags)
{
    size_t retval = 0;

    if (!this->tss_files.empty()) {
        const auto curr_iter = this->current_file_state();
        const auto& lf = curr_iter->fvs_file;
        if (this->tss_view_mode == view_mode::raw
            || !curr_iter->fvs_text_source)
        {
            auto* lfo = dynamic_cast<line_filter_observer*>(
                lf->get_logline_observer());
            if (lfo == nullptr || line < 0
                || line >= (ssize_t) lfo->lfo_filter_state.tfs_index.size())
            {
            } else {
                auto read_res = lf->read_line(
                    lf->begin() + lfo->lfo_filter_state.tfs_index[line]);
                if (read_res.isOk()) {
                    auto sbr = read_res.unwrap();
                    auto str = to_string(sbr);
                    scrub_ansi_string(str, nullptr);
                    retval = string_fragment::from_str(str).column_width();
                }
            }
        } else {
            retval = curr_iter->fvs_text_source->text_size_for_line(
                tc, line, flags);
        }
    }

    return retval;
}

void
textfile_sub_source::to_front(const std::shared_ptr<logfile>& lf)
{
    const auto iter
        = std::find(this->tss_files.begin(), this->tss_files.end(), lf);
    if (iter == this->tss_files.end()) {
        return;
    }
    this->tss_files.front().save_from(*this->tss_view);
    auto fvs = std::move(*iter);
    this->tss_files.erase(iter);
    this->tss_files.emplace_front(std::move(fvs));
    this->set_time_offset(false);
    fvs.load_into(*this->tss_view);
    this->tss_view->reload_data();
}

void
textfile_sub_source::rotate_left()
{
    if (this->tss_files.size() > 1) {
        this->tss_files.emplace_back(std::move(this->tss_files.front()));
        this->tss_files.pop_front();
        this->tss_files.back().save_from(*this->tss_view);
        this->tss_files.front().load_into(*this->tss_view);
        this->set_time_offset(false);
        this->tss_view->reload_data();
        this->tss_view->redo_search();
        this->tss_view->set_needs_update();
    }
}

void
textfile_sub_source::rotate_right()
{
    if (this->tss_files.size() > 1) {
        this->tss_files.front().save_from(*this->tss_view);
        auto fvs = std::move(this->tss_files.back());
        this->tss_files.emplace_front(std::move(fvs));
        this->tss_files.pop_back();
        this->tss_files.front().load_into(*this->tss_view);
        this->set_time_offset(false);
        this->tss_view->reload_data();
        this->tss_view->redo_search();
        this->tss_view->set_needs_update();
    }
}

void
textfile_sub_source::remove(const std::shared_ptr<logfile>& lf)
{
    auto iter = std::find(this->tss_files.begin(), this->tss_files.end(), lf);
    if (iter != this->tss_files.end()) {
        this->tss_files.erase(iter);
        this->detach_observer(lf);
    }
    this->set_time_offset(false);
    if (!this->tss_files.empty()) {
        this->tss_files.front().load_into(*this->tss_view);
    }
    this->tss_view->reload_data();
}

void
textfile_sub_source::push_back(const std::shared_ptr<logfile>& lf)
{
    auto* lfo = new line_filter_observer(this->get_filters(), lf);
    lf->set_logline_observer(lfo);
    this->tss_files.emplace_back(lf);
}

void
textfile_sub_source::text_filters_changed()
{
    auto lf = this->current_file();
    if (lf == nullptr || lf->get_text_format() == text_format_t::TF_BINARY) {
        return;
    }

    auto* lfo = (line_filter_observer*) lf->get_logline_observer();
    uint32_t filter_in_mask, filter_out_mask;

    lfo->clear_deleted_filter_state();
    lf->reobserve_from(lf->begin() + lfo->get_min_count(lf->size()));

    this->get_filters().get_enabled_mask(filter_in_mask, filter_out_mask);
    lfo->lfo_filter_state.tfs_index.clear();
    for (uint32_t lpc = 0; lpc < lf->size(); lpc++) {
        if (this->tss_apply_filters) {
            if (lfo->excluded(filter_in_mask, filter_out_mask, lpc)) {
                continue;
            }
            if (lf->has_line_metadata()) {
                auto ll = lf->begin() + lpc;
                if (ll->get_timeval() < this->ttt_min_row_time) {
                    continue;
                }
                if (this->ttt_max_row_time < ll->get_timeval()) {
                    continue;
                }
            }
        }
        lfo->lfo_filter_state.tfs_index.push_back(lpc);
    }

    this->tss_view->redo_search();

    auto iter = std::lower_bound(lfo->lfo_filter_state.tfs_index.begin(),
                                 lfo->lfo_filter_state.tfs_index.end(),
                                 this->tss_content_line);
    auto vl = vis_line_t(
        std::distance(lfo->lfo_filter_state.tfs_index.begin(), iter));
    this->tss_view->set_selection(vl);
}

void
textfile_sub_source::scroll_invoked(textview_curses* tc)
{
    const auto curr_iter = this->current_file_state();
    if (curr_iter == this->tss_files.end()
        || curr_iter->fvs_file->get_text_format() == text_format_t::TF_BINARY)
    {
        return;
    }

    const auto& lf = curr_iter->fvs_file;
    if (this->tss_view_mode == view_mode::rendered
        && curr_iter->fvs_text_source)
    {
        return;
    }

    auto line = tc->get_selection();
    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    if (lfo == nullptr || line < 0_vl
        || line >= (ssize_t) lfo->lfo_filter_state.tfs_index.size())
    {
        return;
    }

    this->tss_content_line = lfo->lfo_filter_state.tfs_index[line];
}

int
textfile_sub_source::get_filtered_count() const
{
    const auto curr_iter = this->current_file_state();
    int retval = 0;

    if (curr_iter != this->tss_files.end()) {
        if (this->tss_view_mode == view_mode::raw
            || !curr_iter->fvs_text_source)
        {
            const auto& lf = curr_iter->fvs_file;
            auto* lfo = (line_filter_observer*) lf->get_logline_observer();
            retval = lf->size() - lfo->lfo_filter_state.tfs_index.size();
        }
    }
    return retval;
}

int
textfile_sub_source::get_filtered_count_for(size_t filter_index) const
{
    std::shared_ptr<logfile> lf = this->current_file();

    if (lf == nullptr) {
        return 0;
    }

    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    return lfo->lfo_filter_state.tfs_filter_hits[filter_index];
}

text_format_t
textfile_sub_source::get_text_format() const
{
    if (this->tss_files.empty()) {
        return text_format_t::TF_UNKNOWN;
    }

    return this->tss_files.front().fvs_file->get_text_format();
}

static attr_line_t
to_display(const std::shared_ptr<logfile>& lf)
{
    attr_line_t retval;

    if (lf->get_open_options().loo_piper) {
        if (!lf->get_open_options().loo_piper->is_finished()) {
            retval.append("\u21bb "_list_glyph);
        }
    }
    retval.append(lf->get_unique_path());

    return retval;
}

void
textfile_sub_source::text_crumbs_for_line(
    int line, std::vector<breadcrumb::crumb>& crumbs)
{
    text_sub_source::text_crumbs_for_line(line, crumbs);

    if (this->empty()) {
        return;
    }

    const auto curr_iter = this->current_file_state();
    const auto& lf = curr_iter->fvs_file;
    crumbs.emplace_back(
        lf->get_unique_path(),
        to_display(lf),
        [this]() {
            return this->tss_files | lnav::itertools::map([](const auto& lf) {
                       return breadcrumb::possibility{
                           lf.fvs_file->get_path_for_key(),
                           to_display(lf.fvs_file),
                       };
                   });
        },
        [this](const auto& key) {
            auto lf_opt = this->tss_files
                | lnav::itertools::map([](const auto& x) { return x.fvs_file; })
                | lnav::itertools::find_if([&key](const auto& elem) {
                              return key.template get<std::string>()
                                  == elem->get_path_for_key();
                          })
                | lnav::itertools::deref();

            if (!lf_opt) {
                return;
            }

            this->to_front(lf_opt.value());
            this->tss_view->reload_data();
        });
    if (lf->size() == 0) {
        return;
    }

    if (this->tss_view_mode == view_mode::rendered
        && curr_iter->fvs_text_source)
    {
        curr_iter->fvs_text_source->text_crumbs_for_line(line, crumbs);
    }

    if (lf->has_line_metadata()) {
        auto* lfo
            = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
        if (line < 0
            || line >= (ssize_t) lfo->lfo_filter_state.tfs_index.size())
        {
            return;
        }
        auto ll_iter = lf->begin() + lfo->lfo_filter_state.tfs_index[line];
        char ts[64];

        sql_strftime(ts, sizeof(ts), ll_iter->get_timeval(), 'T');

        crumbs.emplace_back(
            std::string(ts),
            []() -> std::vector<breadcrumb::possibility> { return {}; },
            [](const auto& key) {});
    }
    if (curr_iter->fvs_metadata.m_sections_tree.empty()) {
    } else {
        auto* lfo
            = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
        if (line < 0
            || line >= (ssize_t) lfo->lfo_filter_state.tfs_index.size())
        {
            return;
        }
        auto ll_iter = lf->begin() + lfo->lfo_filter_state.tfs_index[line];
        auto ll_next_iter = ll_iter + 1;
        auto end_offset = (ll_next_iter == lf->end())
            ? lf->get_index_size() - 1
            : ll_next_iter->get_offset() - 1;
        const auto initial_size = crumbs.size();

        curr_iter->fvs_metadata.m_sections_tree.visit_overlapping(
            lf->get_line_content_offset(ll_iter),
            end_offset,
            [&crumbs, initial_size, meta = &curr_iter->fvs_metadata, this, lf](
                const auto& iv) {
                auto path = crumbs | lnav::itertools::skip(initial_size)
                    | lnav::itertools::map(&breadcrumb::crumb::c_key)
                    | lnav::itertools::append(iv.value);
                auto curr_node = lnav::document::hier_node::lookup_path(
                    meta->m_sections_root.get(), path);
                crumbs.emplace_back(
                    iv.value,
                    [meta, path]() { return meta->possibility_provider(path); },
                    [this, curr_node, path, lf](const auto& key) {
                        if (!curr_node) {
                            return;
                        }
                        auto* parent_node = curr_node.value()->hn_parent;
                        if (parent_node == nullptr) {
                            return;
                        }
                        key.match(
                            [this, parent_node](const std::string& str) {
                                auto sib_iter
                                    = parent_node->hn_named_children.find(str);
                                if (sib_iter
                                    == parent_node->hn_named_children.end()) {
                                    return;
                                }
                                this->set_top_from_off(
                                    sib_iter->second->hn_start);
                            },
                            [this, parent_node](size_t index) {
                                if (index >= parent_node->hn_children.size()) {
                                    return;
                                }
                                auto sib
                                    = parent_node->hn_children[index].get();
                                this->set_top_from_off(sib->hn_start);
                            });
                    });
                if (curr_node
                    && curr_node.value()->hn_parent->hn_children.size()
                        != curr_node.value()
                               ->hn_parent->hn_named_children.size())
                {
                    auto node = lnav::document::hier_node::lookup_path(
                        meta->m_sections_root.get(), path);

                    crumbs.back().c_expected_input
                        = curr_node.value()
                              ->hn_parent->hn_named_children.empty()
                        ? breadcrumb::crumb::expected_input_t::index
                        : breadcrumb::crumb::expected_input_t::index_or_exact;
                    crumbs.back().with_possible_range(
                        node | lnav::itertools::map([](const auto hn) {
                            return hn->hn_parent->hn_children.size();
                        })
                        | lnav::itertools::unwrap_or(size_t{0}));
                }
            });

        auto path = crumbs | lnav::itertools::skip(initial_size)
            | lnav::itertools::map(&breadcrumb::crumb::c_key);
        auto node = lnav::document::hier_node::lookup_path(
            curr_iter->fvs_metadata.m_sections_root.get(), path);

        if (node && !node.value()->hn_children.empty()) {
            auto poss_provider = [curr_node = node.value()]() {
                std::vector<breadcrumb::possibility> retval;
                for (const auto& child : curr_node->hn_named_children) {
                    retval.emplace_back(child.first);
                }
                return retval;
            };
            auto path_performer = [this, curr_node = node.value()](
                                      const breadcrumb::crumb::key_t& value) {
                value.match(
                    [this, curr_node](const std::string& str) {
                        auto child_iter
                            = curr_node->hn_named_children.find(str);
                        if (child_iter != curr_node->hn_named_children.end()) {
                            this->set_top_from_off(
                                child_iter->second->hn_start);
                        }
                    },
                    [this, curr_node](size_t index) {
                        if (index >= curr_node->hn_children.size()) {
                            return;
                        }
                        auto* child = curr_node->hn_children[index].get();
                        this->set_top_from_off(child->hn_start);
                    });
            };
            crumbs.emplace_back("", "\u22ef", poss_provider, path_performer);
            crumbs.back().c_expected_input
                = node.value()->hn_named_children.empty()
                ? breadcrumb::crumb::expected_input_t::index
                : breadcrumb::crumb::expected_input_t::index_or_exact;
        }
    }
}

textfile_sub_source::rescan_result_t
textfile_sub_source::rescan_files(textfile_sub_source::scan_callback& callback,
                                  std::optional<ui_clock::time_point> deadline)
{
    static auto& lnav_db = injector::get<auto_sqlite3&>();

    file_iterator iter;
    rescan_result_t retval;
    size_t files_scanned = 0;

    if (this->tss_view == nullptr || this->tss_view->is_paused()) {
        return retval;
    }

    std::vector<std::shared_ptr<logfile>> closed_files;
    for (iter = this->tss_files.begin(); iter != this->tss_files.end();) {
        if (deadline && files_scanned > 0 && ui_clock::now() > deadline.value())
        {
            log_info("rescan_files() deadline reached, breaking...");
            retval.rr_scan_completed = false;
            break;
        }

        std::shared_ptr<logfile> lf = iter->fvs_file;

        if (lf->is_closed()) {
            iter = this->tss_files.erase(iter);
            this->detach_observer(lf);
            closed_files.emplace_back(lf);
            retval.rr_rescan_needed = true;
            continue;
        }

        if (!this->tss_completed_last_scan && lf->size() > 0) {
            ++iter;
            continue;
        }
        files_scanned += 1;

        try {
            const auto& st = lf->get_stat();
            uint32_t old_size = lf->size();
            auto new_text_data = lf->rebuild_index(deadline);

            if (lf->get_format() != nullptr) {
                iter = this->tss_files.erase(iter);
                this->detach_observer(lf);
                callback.promote_file(lf);
                continue;
            }

            bool new_data = false;
            switch (new_text_data) {
                case logfile::rebuild_result_t::NEW_LINES:
                case logfile::rebuild_result_t::NEW_ORDER:
                    new_data = true;
                    retval.rr_new_data += 1;
                    break;
                default:
                    break;
            }
            callback.scanned_file(lf);

            if (lf->is_indexing()
                && lf->get_text_format() != text_format_t::TF_BINARY)
            {
                if (!new_data) {
                    // Only invalidate the meta if the file is small, or we
                    // found some meta previously.
                    if ((st.st_mtime != iter->fvs_mtime
                         || lf->get_index_size() != iter->fvs_file_size)
                        && (st.st_size < 10 * 1024 || iter->fvs_file_size == 0
                            || !iter->fvs_metadata.m_sections_tree.empty()))
                    {
                        log_debug(
                            "text file has changed, invalidating metadata.  "
                            "old: {mtime: %d size: %zu}, new: {mtime: %d "
                            "size: %zu}",
                            iter->fvs_mtime,
                            iter->fvs_file_size,
                            st.st_mtime,
                            st.st_size);
                        iter->fvs_metadata = {};
                        iter->fvs_error.clear();
                    }
                }

                if (!iter->fvs_metadata.m_sections_root
                    && iter->fvs_error.empty())
                {
                    auto read_res
                        = lf->read_file(logfile::read_format_t::with_framing);

                    if (read_res.isOk()) {
                        auto read_file_res = read_res.unwrap();

                        if (!read_file_res.rfr_range.fr_metadata.m_valid_utf) {
                            log_error(
                                "%s: file has invalid UTF, skipping meta "
                                "discovery",
                                lf->get_path_for_key().c_str());
                            iter->fvs_mtime = st.st_mtime;
                            iter->fvs_file_size = lf->get_index_size();
                        } else {
                            auto content
                                = attr_line_t(read_file_res.rfr_content);

                            log_info("generating metadata for: %s (size=%zu)",
                                     lf->get_path_for_key().c_str(),
                                     content.length());
                            scrub_ansi_string(content.get_string(),
                                              &content.get_attrs());

                            auto text_meta = extract_text_meta(
                                content.get_string(), lf->get_text_format());
                            if (text_meta) {
                                lf->set_filename(text_meta->tfm_filename);
                                lf->set_include_in_session(true);
                                callback.renamed_file(lf);
                            }

                            iter->fvs_mtime = st.st_mtime;
                            iter->fvs_file_size = lf->get_index_size();
                            iter->fvs_metadata
                                = lnav::document::discover(content)
                                      .with_text_format(lf->get_text_format())
                                      .perform();
                        }
                    } else {
                        auto errmsg = read_res.unwrapErr();
                        log_error(
                            "%s: unable to read file for meta discover -- %s",
                            lf->get_path_for_key().c_str(),
                            errmsg.c_str());
                        iter->fvs_mtime = st.st_mtime;
                        iter->fvs_file_size = lf->get_index_size();
                        iter->fvs_error = errmsg;
                    }
                }
            }

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

            if (lf->get_text_format() == text_format_t::TF_MARKDOWN) {
                if (iter->fvs_text_source) {
                    if (iter->fvs_file_size == st.st_size
                        && iter->fvs_file_indexed_size == lf->get_index_size()
                        && iter->fvs_mtime == st.st_mtime)
                    {
                        ++iter;
                        continue;
                    }
                    log_info("markdown file has been updated, re-rendering: %s",
                             lf->get_path_for_key().c_str());
                    iter->fvs_text_source = nullptr;
                }

                auto read_res = lf->read_file(logfile::read_format_t::plain);
                if (read_res.isOk()) {
                    static const auto FRONT_MATTER_RE
                        = lnav::pcre2pp::code::from_const(
                            R"((?:^---\n(.*)\n---\n|^\+\+\+\n(.*)\n\+\+\+\n))",
                            PCRE2_MULTILINE | PCRE2_DOTALL);
                    thread_local auto md = FRONT_MATTER_RE.create_match_data();

                    auto read_file_res = read_res.unwrap();
                    auto content_sf
                        = string_fragment::from_str(read_file_res.rfr_content);
                    std::string frontmatter;
                    auto frontmatter_format = text_format_t::TF_UNKNOWN;

                    auto cap_res = FRONT_MATTER_RE.capture_from(content_sf)
                                       .into(md)
                                       .matches()
                                       .ignore_error();
                    if (cap_res) {
                        if (md[1]) {
                            frontmatter_format = text_format_t::TF_YAML;
                            frontmatter = md[1]->to_string();
                        } else if (md[2]) {
                            frontmatter_format = text_format_t::TF_TOML;
                            frontmatter = md[2]->to_string();
                        }
                        content_sf = cap_res->f_remaining;
                    } else if (content_sf.startswith("{")) {
                        yajlpp_parse_context ypc(
                            intern_string::lookup(lf->get_filename()));
                        auto handle
                            = yajlpp::alloc_handle(&ypc.ypc_callbacks, &ypc);

                        yajl_config(
                            handle.in(), yajl_allow_trailing_garbage, 1);
                        ypc.with_ignore_unused(true)
                            .with_handle(handle.in())
                            .with_error_reporter(
                                [&lf](const auto& ypc, const auto& um) {
                                    log_error(
                                        "%s: failed to parse JSON front matter "
                                        "-- %s",
                                        lf->get_filename().c_str(),
                                        um.um_reason.al_string.c_str());
                                });
                        if (ypc.parse_doc(content_sf)) {
                            ssize_t consumed = ypc.ypc_total_consumed;
                            if (consumed < content_sf.length()
                                && content_sf[consumed] == '\n')
                            {
                                frontmatter_format = text_format_t::TF_JSON;
                                frontmatter = string_fragment::from_str_range(
                                                  read_file_res.rfr_content,
                                                  0,
                                                  consumed)
                                                  .to_string();
                                content_sf = content_sf.substr(consumed);
                            }
                        }
                    }

                    log_info("%s: rendering markdown content of size %d",
                             lf->get_basename().c_str(),
                             read_file_res.rfr_content.size());
                    md2attr_line mdal;

                    mdal.with_source_path(lf->get_actual_path());
                    if (this->tss_view->tc_interactive) {
                        mdal.add_lnav_script_icons();
                    }
                    auto parse_res = md4cpp::parse(content_sf, mdal);

                    iter->fvs_mtime = st.st_mtime;
                    iter->fvs_file_indexed_size = lf->get_index_size();
                    iter->fvs_file_size = st.st_size;
                    iter->fvs_text_source
                        = std::make_unique<plain_text_source>();
                    iter->fvs_text_source->set_text_format(
                        lf->get_text_format());
                    iter->fvs_text_source->register_view(this->tss_view);
                    if (parse_res.isOk()) {
                        auto& lf_meta = lf->get_embedded_metadata();

                        iter->fvs_text_source->replace_with(parse_res.unwrap());

                        if (!frontmatter.empty()) {
                            lf_meta["net.daringfireball.markdown.frontmatter"]
                                = {frontmatter_format, frontmatter};
                        }

                        lnav::events::publish(
                            lnav_db,
                            lnav::events::file::format_detected{
                                lf->get_filename(),
                                fmt::to_string(lf->get_text_format()),
                            });
                    } else {
                        auto view_content
                            = lnav::console::user_message::error(
                                  "unable to parse markdown file")
                                  .with_reason(parse_res.unwrapErr())
                                  .to_attr_line();
                        view_content.append("\n").append(
                            attr_line_t::from_ansi_str(
                                read_file_res.rfr_content.c_str()));

                        iter->fvs_text_source->replace_with(view_content);
                    }
                } else {
                    log_error("unable to read markdown file: %s -- %s",
                              lf->get_path_for_key().c_str(),
                              read_res.unwrapErr().c_str());
                }
            } else if (file_needs_reformatting(lf) && !new_data) {
                if (iter->fvs_file_size == st.st_size
                    && iter->fvs_file_indexed_size == lf->get_index_size()
                    && iter->fvs_mtime == st.st_mtime)
                {
                    ++iter;
                    continue;
                }
                log_info("pretty file has been updated, re-rendering: %s",
                         lf->get_path_for_key().c_str());
                iter->fvs_text_source = nullptr;
                iter->fvs_error.clear();

                auto read_res = lf->read_file(logfile::read_format_t::plain);
                if (read_res.isOk()) {
                    auto read_file_res = read_res.unwrap();
                    if (read_file_res.rfr_range.fr_metadata.m_valid_utf) {
                        auto orig_al = attr_line_t(read_file_res.rfr_content);
                        scrub_ansi_string(orig_al.al_string, &orig_al.al_attrs);
                        data_scanner ds(orig_al.al_string);
                        pretty_printer pp(&ds, orig_al.al_attrs);
                        attr_line_t pretty_al;

                        pp.append_to(pretty_al);
                        iter->fvs_mtime = st.st_mtime;
                        iter->fvs_file_indexed_size = lf->get_index_size();
                        iter->fvs_file_size = st.st_size;
                        iter->fvs_text_source
                            = std::make_unique<plain_text_source>();
                        iter->fvs_text_source->set_text_format(
                            lf->get_text_format());
                        iter->fvs_text_source->register_view(this->tss_view);
                        iter->fvs_text_source->replace_with(pretty_al);
                    } else {
                        log_error(
                            "unable to read file to pretty-print: %s -- file "
                            "is not valid UTF-8",
                            lf->get_path_for_key().c_str());
                        iter->fvs_mtime = st.st_mtime;
                        iter->fvs_file_indexed_size = lf->get_index_size();
                        iter->fvs_file_size = st.st_size;
                        iter->fvs_error = "file is not valid UTF-8";
                    }
                } else {
                    auto errmsg = read_res.unwrapErr();
                    log_error("unable to read file to pretty-print: %s -- %s",
                              lf->get_path_for_key().c_str(),
                              errmsg.c_str());
                    iter->fvs_mtime = st.st_mtime;
                    iter->fvs_file_indexed_size = lf->get_index_size();
                    iter->fvs_file_size = st.st_size;
                    iter->fvs_error = errmsg;
                }
            }
        } catch (const line_buffer::error& e) {
            iter = this->tss_files.erase(iter);
            lf->close();
            this->detach_observer(lf);
            closed_files.emplace_back(lf);
            continue;
        }

        ++iter;
    }
    if (!closed_files.empty()) {
        callback.closed_files(closed_files);
        if (!this->tss_files.empty()) {
            this->tss_files.front().load_into(*this->tss_view);
        }
        this->tss_view->set_needs_update();
    }

    if (retval.rr_new_data) {
        this->tss_view->search_new_data();
    }
    this->tss_completed_last_scan = retval.rr_scan_completed;

    return retval;
}

void
textfile_sub_source::set_top_from_off(file_off_t off)
{
    auto lf = this->current_file();

    lf->line_for_offset(off) | [this, lf](auto new_top_iter) {
        auto* lfo = (line_filter_observer*) lf->get_logline_observer();
        auto new_top_opt = lfo->lfo_filter_state.content_line_to_vis_line(
            std::distance(lf->cbegin(), new_top_iter));

        if (new_top_opt) {
            this->tss_view->set_selection(vis_line_t(new_top_opt.value()));
            if (this->tss_view->is_selectable()) {
                this->tss_view->set_top(this->tss_view->get_selection() - 2_vl,
                                        false);
            }
        }
    };
}

void
textfile_sub_source::quiesce()
{
    for (auto& lf : this->tss_files) {
        lf.fvs_file->quiesce();
    }
}

std::optional<vis_line_t>
textfile_sub_source::row_for_anchor(const std::string& id)
{
    const auto curr_iter = this->current_file_state();
    if (curr_iter == this->tss_files.end() || id.empty()) {
        return std::nullopt;
    }

    if (this->tss_view_mode == view_mode::rendered
        && curr_iter->fvs_text_source)
    {
        return curr_iter->fvs_text_source->row_for_anchor(id);
    }

    if (!curr_iter->fvs_metadata.m_sections_root) {
        return std::nullopt;
    }

    const auto& lf = curr_iter->fvs_file;
    const auto& meta = curr_iter->fvs_metadata;
    std::optional<vis_line_t> retval;

    auto is_ptr = startswith(id, "#/");
    if (is_ptr) {
        auto hier_sf = string_fragment::from_str(id).consume_n(2).value();
        std::vector<lnav::document::section_key_t> path;

        while (!hier_sf.empty()) {
            auto comp_pair = hier_sf.split_when(string_fragment::tag1{'/'});
            auto scan_res
                = scn::scan_value<int64_t>(comp_pair.first.to_string_view());
            if (scan_res && scan_res->range().empty()) {
                path.emplace_back(scan_res->value());
            } else {
                path.emplace_back(json_ptr::decode(comp_pair.first));
            }
            hier_sf = comp_pair.second;
        }

        auto lookup_res = lnav::document::hier_node::lookup_path(
            meta.m_sections_root.get(), path);
        if (lookup_res) {
            auto ll_opt = lf->line_for_offset(lookup_res.value()->hn_start);
            if (ll_opt != lf->end()) {
                retval
                    = vis_line_t(std::distance(lf->cbegin(), ll_opt.value()));
            }
        }

        return retval;
    }

    lnav::document::hier_node::depth_first(
        meta.m_sections_root.get(),
        [lf, &id, &retval](const lnav::document::hier_node* node) {
            for (const auto& child_pair : node->hn_named_children) {
                const auto& child_anchor = to_anchor_string(child_pair.first);

                if (child_anchor != id) {
                    continue;
                }

                auto ll_opt = lf->line_for_offset(child_pair.second->hn_start);
                if (ll_opt != lf->end()) {
                    retval = vis_line_t(
                        std::distance(lf->cbegin(), ll_opt.value()));
                }
                break;
            }
        });

    return retval;
}

static void
anchor_generator(std::unordered_set<std::string>& retval,
                 std::vector<std::string>& comps,
                 size_t& max_depth,
                 lnav::document::hier_node* hn)
{
    if (hn->hn_named_children.empty()) {
        if (hn->hn_children.empty()) {
            if (retval.size() >= 250 || comps.empty()) {
            } else if (comps.size() == 1) {
                retval.emplace(text_anchors::to_anchor_string(comps.front()));
            } else {
                retval.emplace(
                    fmt::format(FMT_STRING("#/{}"),
                                fmt::join(comps.begin(), comps.end(), "/")));
            }
            max_depth = std::max(max_depth, comps.size());
        } else {
            int index = 0;
            for (const auto& child : hn->hn_children) {
                comps.emplace_back(fmt::to_string(index));
                anchor_generator(retval, comps, max_depth, child.get());
                comps.pop_back();
            }
        }
    } else {
        for (const auto& [child_name, child_node] : hn->hn_named_children) {
            comps.emplace_back(child_name);
            anchor_generator(retval, comps, max_depth, child_node);
            comps.pop_back();
        }
        if (max_depth > 1) {
            retval.emplace(
                fmt::format(FMT_STRING("#/{}"),
                            fmt::join(comps.begin(), comps.end(), "/")));
        }
    }
}

std::unordered_set<std::string>
textfile_sub_source::get_anchors()
{
    std::unordered_set<std::string> retval;

    const auto curr_iter = this->current_file_state();
    if (curr_iter == this->tss_files.end()) {
        return retval;
    }

    if (this->tss_view_mode == view_mode::rendered
        && curr_iter->fvs_text_source)
    {
        return curr_iter->fvs_text_source->get_anchors();
    }

    const auto& meta = curr_iter->fvs_metadata;
    if (meta.m_sections_root == nullptr) {
        return retval;
    }

    std::vector<std::string> comps;
    size_t max_depth = 0;
    anchor_generator(retval, comps, max_depth, meta.m_sections_root.get());

    return retval;
}

struct tfs_time_cmp {
    bool operator()(int32_t lhs, const timeval& rhs) const
    {
        auto ll = this->ttc_logfile->begin() + this->ttc_index[lhs];
        return ll->get_timeval() < rhs;
    }

    logfile* ttc_logfile;
    std::vector<uint32_t>& ttc_index;
};

std::optional<vis_line_t>
textfile_sub_source::row_for_time(timeval time_bucket)
{
    auto lf = this->current_file();
    if (!lf || !lf->has_line_metadata()) {
        return std::nullopt;
    }

    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    auto& tfs = lfo->lfo_filter_state.tfs_index;
    auto lb = std::lower_bound(
        tfs.begin(), tfs.end(), time_bucket, tfs_time_cmp{lf.get(), tfs});
    if (lb != tfs.end()) {
        return vis_line_t{(int) std::distance(tfs.begin(), lb)};
    }

    return std::nullopt;
}

std::optional<text_time_translator::row_info>
textfile_sub_source::time_for_row(vis_line_t row)
{
    auto lf = this->current_file();
    if (!lf || !lf->has_line_metadata()) {
        return std::nullopt;
    }

    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    if (row < 0_vl || row >= (ssize_t) lfo->lfo_filter_state.tfs_index.size()) {
        return std::nullopt;
    }
    auto row_id = lfo->lfo_filter_state.tfs_index[row];
    auto ll_iter = lf->begin() + row_id;
    return row_info{
        ll_iter->get_timeval(),
        row_id,
    };
}

static std::optional<vis_line_t>
to_vis_line(const std::shared_ptr<logfile>& lf, file_off_t off)
{
    auto ll_opt = lf->line_for_offset(off);
    if (ll_opt != lf->end()) {
        return vis_line_t(std::distance(lf->cbegin(), ll_opt.value()));
    }

    return std::nullopt;
}

std::optional<vis_line_t>
textfile_sub_source::adjacent_anchor(vis_line_t vl, direction dir)
{
    const auto curr_iter = this->current_file_state();
    if (curr_iter == this->tss_files.end()) {
        return std::nullopt;
    }

    const auto& lf = curr_iter->fvs_file;
    log_debug("adjacent_anchor: %s:L%d:%s",
              lf->get_path_for_key().c_str(),
              vl,
              dir == text_anchors::direction::prev ? "prev" : "next");
    if (this->tss_view_mode == view_mode::rendered
        && curr_iter->fvs_text_source)
    {
        return curr_iter->fvs_text_source->adjacent_anchor(vl, dir);
    }

    if (!curr_iter->fvs_metadata.m_sections_root) {
        log_debug("  no metadata available");
        return std::nullopt;
    }

    auto& md = curr_iter->fvs_metadata;
    const auto* lfo
        = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    if (vl >= (ssize_t) lfo->lfo_filter_state.tfs_index.size()
        || md.m_sections_root == nullptr)
    {
        return std::nullopt;
    }
    const auto ll_iter = lf->begin() + lfo->lfo_filter_state.tfs_index[vl];
    const auto line_offsets = lf->get_file_range(ll_iter, false);
    log_debug(
        "  range %d:%d", line_offsets.fr_offset, line_offsets.next_offset());
    auto path_for_line
        = md.path_for_range(line_offsets.fr_offset, line_offsets.next_offset());

    if (path_for_line.empty()) {
        log_debug("  no path found");
        const auto neighbors_res = md.m_sections_root->line_neighbors(vl);
        if (!neighbors_res) {
            return std::nullopt;
        }

        switch (dir) {
            case direction::prev: {
                if (neighbors_res->cnr_previous) {
                    return to_vis_line(
                        lf, neighbors_res->cnr_previous.value()->hn_start);
                }
                break;
            }
            case direction::next: {
                if (neighbors_res->cnr_next) {
                    return to_vis_line(
                        lf, neighbors_res->cnr_next.value()->hn_start);
                }
                if (!md.m_sections_root->hn_children.empty()) {
                    return to_vis_line(
                        lf, md.m_sections_root->hn_children[0]->hn_start);
                }
                break;
            }
        }
        return std::nullopt;
    }

    log_debug("  path for line: %s", fmt::to_string(path_for_line).c_str());
    const auto last_key = std::move(path_for_line.back());
    path_for_line.pop_back();

    const auto parent_opt = lnav::document::hier_node::lookup_path(
        md.m_sections_root.get(), path_for_line);
    if (!parent_opt) {
        log_debug("  no parent for path: %s",
                  fmt::to_string(path_for_line).c_str());
        return std::nullopt;
    }
    const auto parent = parent_opt.value();

    const auto child_hn = parent->lookup_child(last_key);
    if (!child_hn) {
        // XXX "should not happen"
        log_debug("  child not found");
        return std::nullopt;
    }

    auto neighbors_res = parent->child_neighbors(
        child_hn.value(), line_offsets.next_offset() + 1);
    if (!neighbors_res) {
        log_debug("  no neighbors found");
        return std::nullopt;
    }

    log_debug("  neighbors p:%d n:%d",
              neighbors_res->cnr_previous.has_value(),
              neighbors_res->cnr_next.has_value());
    if (neighbors_res->cnr_previous && last_key.is<std::string>()) {
        auto neighbor_sub
            = neighbors_res->cnr_previous.value()->lookup_child(last_key);
        if (neighbor_sub) {
            neighbors_res->cnr_previous = neighbor_sub;
        }
    }

    if (neighbors_res->cnr_next && last_key.is<std::string>()) {
        auto neighbor_sub
            = neighbors_res->cnr_next.value()->lookup_child(last_key);
        if (neighbor_sub) {
            neighbors_res->cnr_next = neighbor_sub;
        }
    }

    switch (dir) {
        case direction::prev: {
            if (neighbors_res->cnr_previous) {
                return to_vis_line(
                    lf, neighbors_res->cnr_previous.value()->hn_start);
            }
            break;
        }
        case direction::next: {
            if (neighbors_res->cnr_next) {
                return to_vis_line(lf,
                                   neighbors_res->cnr_next.value()->hn_start);
            }
            break;
        }
    }

    return std::nullopt;
}

std::optional<std::string>
textfile_sub_source::anchor_for_row(vis_line_t vl)
{
    const auto curr_iter = this->current_file_state();
    if (curr_iter == this->tss_files.end()) {
        return std::nullopt;
    }

    if (this->tss_view_mode == view_mode::rendered
        && curr_iter->fvs_text_source)
    {
        return curr_iter->fvs_text_source->anchor_for_row(vl);
    }

    if (!curr_iter->fvs_metadata.m_sections_root) {
        return std::nullopt;
    }

    const auto& lf = curr_iter->fvs_file;
    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    if (vl >= (ssize_t) lfo->lfo_filter_state.tfs_index.size()) {
        return std::nullopt;
    }
    auto& md = curr_iter->fvs_metadata;
    auto ll_iter = lf->begin() + lfo->lfo_filter_state.tfs_index[vl];
    auto line_offsets = lf->get_file_range(ll_iter, false);
    auto path_for_line
        = md.path_for_range(line_offsets.fr_offset, line_offsets.next_offset());

    if (path_for_line.empty()) {
        return std::nullopt;
    }

    if ((path_for_line.size() == 1
         || md.m_text_format == text_format_t::TF_MARKDOWN)
        && path_for_line.back().is<std::string>())
    {
        return text_anchors::to_anchor_string(
            path_for_line.back().get<std::string>());
    }

    auto comps = path_for_line | lnav::itertools::map([](const auto& elem) {
                     return elem.match(
                         [](const std::string& str) {
                             return json_ptr::encode_str(str);
                         },
                         [](size_t index) { return fmt::to_string(index); });
                 });

    return fmt::format(FMT_STRING("#/{}"),
                       fmt::join(comps.begin(), comps.end(), "/"));
}

bool
textfile_sub_source::to_front(const std::string& filename)
{
    auto lf_opt = this->tss_files
        | lnav::itertools::find_if([&filename](const auto& elem) {
                      return elem.fvs_file->get_filename() == filename;
                  });
    if (!lf_opt) {
        return false;
    }

    this->to_front(lf_opt.value()->fvs_file);

    return true;
}

logline*
textfile_sub_source::text_accel_get_line(vis_line_t vl)
{
    auto lf = this->current_file();
    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    return (lf->begin() + lfo->lfo_filter_state.tfs_index[vl]).base();
}

void
textfile_sub_source::set_view_mode(view_mode vm)
{
    this->tss_view_mode = vm;
    this->tss_view->set_needs_update();
}

textfile_sub_source::view_mode
textfile_sub_source::get_effective_view_mode() const
{
    auto retval = view_mode::raw;

    const auto curr_iter = this->current_file_state();
    if (curr_iter != this->tss_files.end()) {
        if (this->tss_view_mode == view_mode::rendered
            && curr_iter->fvs_text_source)
        {
            retval = view_mode::rendered;
        }
    }

    return retval;
}

textfile_header_overlay::textfile_header_overlay(textfile_sub_source* src,
                                                 text_sub_source* log_src)
    : tho_src(src), tho_log_src(log_src)
{
}

bool
textfile_header_overlay::list_static_overlay(const listview_curses& lv,
                                             int y,
                                             int bottom,
                                             attr_line_t& value_out)
{
    const std::vector<attr_line_t>* lines = nullptr;
    if (this->tho_src->text_line_count() == 0) {
        if (this->tho_log_src->text_line_count() == 0) {
            lines = lnav::messages::view::no_files();
        } else {
            lines = lnav::messages::view::only_log_files();
        }
    }

    if (lines != nullptr && y < (ssize_t) lines->size()) {
        value_out = lines->at(y);
        value_out.with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS));
        if (y == (ssize_t) lines->size() - 1) {
            value_out.with_attr_for_all(
                VC_STYLE.value(text_attrs::with_underline()));
        }
        return true;
    }

    if (y != 0) {
        return false;
    }

    const auto lf = this->tho_src->current_file();
    if (lf == nullptr) {
        return false;
    }

    if (lf->get_text_format() != text_format_t::TF_MARKDOWN
        && this->tho_src->get_effective_view_mode()
            == textfile_sub_source::view_mode::rendered)
    {
        auto ta = text_attrs::with_underline();
        value_out.append("\u24d8"_info)
            .append(" The following is a rendered view of the content.  Use ")
            .append(lnav::roles::quoted_code(":set-text-view-mode raw"))
            .append(" to view the raw version of this text")
            .with_attr_for_all(VC_ROLE.value(role_t::VCR_STATUS_INFO))
            .with_attr_for_all(VC_STYLE.value(ta));
        return true;
    }

    if (lf->get_text_format() != text_format_t::TF_BINARY) {
        return false;
    }

    {
        attr_line_builder alb(value_out);
        {
            auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_TABLE_HEADER));
            alb.appendf(FMT_STRING("{:>16} "), "File Offset");
        }
        size_t byte_off = 0;
        for (size_t lpc = 0; lpc < 16; lpc++) {
            auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_FILE_OFFSET));
            if (byte_off == 8) {
                alb.append(" ");
            }
            alb.appendf(FMT_STRING(" {:0>2x}"), lpc);
            byte_off += 1;
        }
        {
            auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_TABLE_HEADER));
            alb.appendf(FMT_STRING("  {:^17}"), "ASCII");
        }
    }
    value_out.with_attr_for_all(VC_STYLE.value(text_attrs::with_underline()));
    return true;
}
