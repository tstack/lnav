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
#include "lnav.events.hh"
#include "md2attr_line.hh"
#include "scn/scn.h"
#include "sql_util.hh"
#include "sqlitepp.hh"

using namespace lnav::roles::literals;

size_t
textfile_sub_source::text_line_count()
{
    size_t retval = 0;

    if (!this->tss_files.empty()) {
        auto lf = this->current_file();
        auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
        if (rend_iter == this->tss_rendered_files.end()) {
            if (lf->get_text_format() == text_format_t::TF_BINARY) {
                auto fsize = lf->get_stat().st_size;
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
            retval = rend_iter->second.rf_text_source->text_line_count();
        }
    }

    return retval;
}

void
textfile_sub_source::text_value_for_line(textview_curses& tc,
                                         int line,
                                         std::string& value_out,
                                         text_sub_source::line_flags_t flags)
{
    if (this->tss_files.empty() || line < 0) {
        value_out.clear();
        return;
    }

    const auto lf = this->current_file();
    auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
    if (rend_iter != this->tss_rendered_files.end()) {
        rend_iter->second.rf_text_source->text_value_for_line(
            tc, line, value_out, flags);
        return;
    }

    if (lf->get_text_format() == text_format_t::TF_BINARY) {
        this->tss_hex_line.clear();

        attr_line_builder alb(this->tss_hex_line);

        auto fsize = lf->get_stat().st_size;
        auto fr = file_range{line * 16};
        fr.fr_size = std::min((file_ssize_t) 16, fsize - fr.fr_offset);

        auto read_res = lf->read_range(fr);
        if (read_res.isErr()) {
            log_error("%s: failed to read range %lld:%lld -- %s",
                      lf->get_filename().c_str(),
                      fr.fr_offset,
                      fr.fr_size,
                      read_res.unwrapErr().c_str());
            return;
        }

        auto sbr = read_res.unwrap();
        auto sf = sbr.to_string_fragment();
        {
            auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_FILE_OFFSET));
            alb.appendf(FMT_STRING("{: >16x} "), fr.fr_offset);
        }
        auto byte_off = size_t{0};
        for (auto ch : sf) {
            if (byte_off == 8) {
                alb.append(" ");
            }
            nonstd::optional<role_t> ro;
            if (ch == '\0') {
                ro = role_t::VCR_NULL;
            } else if (isspace(ch) || iscntrl(ch)) {
                ro = role_t::VCR_ASCII_CTRL;
            } else if (!isprint(ch)) {
                ro = role_t::VCR_NON_ASCII;
            }
            auto ag = ro.has_value() ? alb.with_attr(VC_ROLE.value(ro.value()))
                                     : alb.with_default();
            alb.appendf(FMT_STRING(" {:0>2x}"), ch);
            byte_off += 1;
        }
        for (; byte_off < 16; byte_off++) {
            if (byte_off == 8) {
                alb.append(" ");
            }
            alb.append("   ");
        }
        alb.append("  ");
        byte_off = 0;
        for (auto ch : sf) {
            if (byte_off == 8) {
                alb.append(" ");
            }
            if (ch == '\0') {
                auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_NULL));
                alb.append("\u22c4");
            } else if (isspace(ch)) {
                auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_ASCII_CTRL));
                alb.append("_");
            } else if (iscntrl(ch)) {
                auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_ASCII_CTRL));
                alb.append("\u2022");
            } else if (isprint(ch)) {
                this->tss_hex_line.get_string().push_back(ch);
            } else {
                auto ag = alb.with_attr(VC_ROLE.value(role_t::VCR_NON_ASCII));
                alb.append("\u00d7");
            }
            byte_off += 1;
        }
        auto alt_row_index = line % 4;
        if (alt_row_index == 2 || alt_row_index == 3) {
            this->tss_hex_line.with_attr_for_all(
                VC_ROLE.value(role_t::VCR_ALT_ROW));
        }

        value_out = this->tss_hex_line.get_string();
        return;
    }

    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    if (lfo == nullptr || line >= lfo->lfo_filter_state.tfs_index.size()) {
        value_out.clear();
        return;
    }

    auto ll = lf->begin() + lfo->lfo_filter_state.tfs_index[line];
    auto read_result = lf->read_line(ll);
    this->tss_line_indent_size = 0;
    if (read_result.isOk()) {
        value_out = to_string(read_result.unwrap());
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
}

void
textfile_sub_source::text_attrs_for_line(textview_curses& tc,
                                         int row,
                                         string_attrs_t& value_out)
{
    auto lf = this->current_file();
    if (lf == nullptr) {
        return;
    }

    struct line_range lr;

    lr.lr_start = 0;
    lr.lr_end = -1;
    auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
    if (rend_iter != this->tss_rendered_files.end()) {
        rend_iter->second.rf_text_source->text_attrs_for_line(
            tc, row, value_out);
    } else if (lf->get_text_format() == text_format_t::TF_BINARY) {
        value_out = this->tss_hex_line.get_attrs();
    } else {
        auto* lfo
            = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
        if (lfo != nullptr && row >= 0
            && row < lfo->lfo_filter_state.tfs_index.size())
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
                                       VC_GRAPHIC.value(ACS_VLINE));

                role_t bar_role = role_t::VCR_NONE;

                switch (this->get_line_accel_direction(vis_line_t(row))) {
                    case log_accel::A_STEADY:
                        break;
                    case log_accel::A_DECEL:
                        bar_role = role_t::VCR_DIFF_DELETE;
                        break;
                    case log_accel::A_ACCEL:
                        bar_role = role_t::VCR_DIFF_ADD;
                        break;
                }
                if (bar_role != role_t::VCR_NONE) {
                    value_out.emplace_back(line_range(12, 13),
                                           VC_ROLE.value(bar_role));
                }
            }

            auto meta_opt
                = lnav::map::find(this->tss_doc_metadata, lf->get_filename());
            if (meta_opt) {
                auto ll_next_iter = ll + 1;
                auto end_offset = (ll_next_iter == lf->end())
                    ? lf->get_index_size() - 1
                    : ll_next_iter->get_offset() - 1;
                const auto& meta = meta_opt.value().get();
                meta.ms_metadata.m_section_types_tree.visit_overlapping(
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
                for (const auto& indent : meta.ms_metadata.m_indents) {
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

    value_out.emplace_back(lr, logline::L_FILE.value(this->current_file()));
}

size_t
textfile_sub_source::text_size_for_line(textview_curses& tc,
                                        int line,
                                        text_sub_source::line_flags_t flags)
{
    size_t retval = 0;

    if (!this->tss_files.empty()) {
        std::shared_ptr<logfile> lf = this->current_file();
        auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
        if (rend_iter == this->tss_rendered_files.end()) {
            auto* lfo = dynamic_cast<line_filter_observer*>(
                lf->get_logline_observer());
            if (lfo == nullptr || line < 0
                || line >= lfo->lfo_filter_state.tfs_index.size())
            {
            } else {
                retval
                    = lf->message_byte_length(
                            lf->begin() + lfo->lfo_filter_state.tfs_index[line])
                          .mlr_length;
            }
        } else {
            retval = rend_iter->second.rf_text_source->text_size_for_line(
                tc, line, flags);
        }
    }

    return retval;
}

void
textfile_sub_source::to_front(const std::shared_ptr<logfile>& lf)
{
    auto iter = std::find(this->tss_files.begin(), this->tss_files.end(), lf);
    if (iter != this->tss_files.end()) {
        this->tss_files.erase(iter);
    } else {
        iter = std::find(
            this->tss_hidden_files.begin(), this->tss_hidden_files.end(), lf);

        if (iter != this->tss_hidden_files.end()) {
            this->tss_hidden_files.erase(iter);
        }
    }
    this->tss_files.push_front(lf);
    this->set_time_offset(false);
    this->tss_view->reload_data();
}

void
textfile_sub_source::rotate_left()
{
    if (this->tss_files.size() > 1) {
        this->tss_files.push_back(this->tss_files.front());
        this->tss_files.pop_front();
        this->set_time_offset(false);
        this->tss_view->reload_data();
        this->tss_view->redo_search();
    }
}

void
textfile_sub_source::rotate_right()
{
    if (this->tss_files.size() > 1) {
        this->tss_files.push_front(this->tss_files.back());
        this->tss_files.pop_back();
        this->set_time_offset(false);
        this->tss_view->reload_data();
        this->tss_view->redo_search();
    }
}

void
textfile_sub_source::remove(const std::shared_ptr<logfile>& lf)
{
    auto iter = std::find(this->tss_files.begin(), this->tss_files.end(), lf);
    if (iter != this->tss_files.end()) {
        this->tss_files.erase(iter);
        detach_observer(lf);
    } else {
        iter = std::find(
            this->tss_hidden_files.begin(), this->tss_hidden_files.end(), lf);
        if (iter != this->tss_hidden_files.end()) {
            this->tss_hidden_files.erase(iter);
            detach_observer(lf);
        }
    }
    this->set_time_offset(false);
}

void
textfile_sub_source::push_back(const std::shared_ptr<logfile>& lf)
{
    auto* lfo = new line_filter_observer(this->get_filters(), lf);
    lf->set_logline_observer(lfo);
    this->tss_files.push_back(lf);
}

void
textfile_sub_source::text_filters_changed()
{
    for (auto iter = this->tss_files.begin(); iter != this->tss_files.end();) {
        ++iter;
    }
    for (auto iter = this->tss_hidden_files.begin();
         iter != this->tss_hidden_files.end();)
    {
        ++iter;
    }

    std::shared_ptr<logfile> lf = this->current_file();

    if (lf == nullptr) {
        return;
    }

    auto* lfo = (line_filter_observer*) lf->get_logline_observer();
    uint32_t filter_in_mask, filter_out_mask;

    lfo->clear_deleted_filter_state();
    lf->reobserve_from(lf->begin() + lfo->get_min_count(lf->size()));

    this->get_filters().get_enabled_mask(filter_in_mask, filter_out_mask);
    lfo->lfo_filter_state.tfs_index.clear();
    for (uint32_t lpc = 0; lpc < lf->size(); lpc++) {
        if (this->tss_apply_filters
            && lfo->excluded(filter_in_mask, filter_out_mask, lpc))
        {
            continue;
        }
        lfo->lfo_filter_state.tfs_index.push_back(lpc);
    }

    this->tss_view->redo_search();
}

int
textfile_sub_source::get_filtered_count() const
{
    std::shared_ptr<logfile> lf = this->current_file();
    int retval = 0;

    if (lf != nullptr) {
        auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
        if (rend_iter == this->tss_rendered_files.end()) {
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

    return this->tss_files.front()->get_text_format();
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

    auto lf = this->current_file();
    crumbs.emplace_back(
        lf->get_unique_path(),
        to_display(lf),
        [this]() {
            return this->tss_files | lnav::itertools::map([](const auto& lf) {
                       return breadcrumb::possibility{
                           lf->get_unique_path(),
                           to_display(lf),
                       };
                   });
        },
        [this](const auto& key) {
            auto lf_opt = this->tss_files
                | lnav::itertools::find_if([&key](const auto& elem) {
                              return key.template get<std::string>()
                                  == elem->get_unique_path();
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

    auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
    if (rend_iter != this->tss_rendered_files.end()) {
        rend_iter->second.rf_text_source->text_crumbs_for_line(line, crumbs);
    }

    if (lf->has_line_metadata()) {
        auto* lfo
            = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
        if (line < 0 || line >= lfo->lfo_filter_state.tfs_index.size()) {
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
    auto meta_iter = this->tss_doc_metadata.find(lf->get_filename());
    if (meta_iter == this->tss_doc_metadata.end()
        || meta_iter->second.ms_metadata.m_sections_tree.empty())
    {
    } else {
        auto* lfo
            = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
        if (line < 0 || line >= lfo->lfo_filter_state.tfs_index.size()) {
            return;
        }
        auto ll_iter = lf->begin() + lfo->lfo_filter_state.tfs_index[line];
        auto ll_next_iter = ll_iter + 1;
        auto end_offset = (ll_next_iter == lf->end())
            ? lf->get_index_size() - 1
            : ll_next_iter->get_offset() - 1;
        const auto initial_size = crumbs.size();

        meta_iter->second.ms_metadata.m_sections_tree.visit_overlapping(
            lf->get_line_content_offset(ll_iter),
            end_offset,
            [&crumbs,
             initial_size,
             meta = &meta_iter->second.ms_metadata,
             this,
             lf](const auto& iv) {
                auto path = crumbs | lnav::itertools::skip(initial_size)
                    | lnav::itertools::map(&breadcrumb::crumb::c_key)
                    | lnav::itertools::append(iv.value);
                auto curr_node = lnav::document::hier_node::lookup_path(
                    meta->m_sections_root.get(), path);
                crumbs.template emplace_back(
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
                        key.template match(
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
            meta_iter->second.ms_metadata.m_sections_root.get(), path);

        if (node && !node.value()->hn_children.empty()) {
            auto poss_provider = [curr_node = node.value()]() {
                std::vector<breadcrumb::possibility> retval;
                for (const auto& child : curr_node->hn_named_children) {
                    retval.template emplace_back(child.first);
                }
                return retval;
            };
            auto path_performer = [this, curr_node = node.value()](
                                      const breadcrumb::crumb::key_t& value) {
                value.template match(
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
textfile_sub_source::rescan_files(
    textfile_sub_source::scan_callback& callback,
    nonstd::optional<ui_clock::time_point> deadline)
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

        std::shared_ptr<logfile> lf = (*iter);

        if (lf->is_closed()) {
            iter = this->tss_files.erase(iter);
            this->tss_rendered_files.erase(lf->get_filename());
            this->tss_doc_metadata.erase(lf->get_filename());
            this->detach_observer(lf);
            closed_files.template emplace_back(lf);
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
                this->tss_rendered_files.erase(lf->get_filename());
                this->tss_doc_metadata.erase(lf->get_filename());
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

            if (lf->get_text_format() == text_format_t::TF_MARKDOWN) {
                auto rend_iter
                    = this->tss_rendered_files.find(lf->get_filename());
                if (rend_iter != this->tss_rendered_files.end()) {
                    if (rend_iter->second.rf_file_size == st.st_size
                        && rend_iter->second.rf_mtime == st.st_mtime)
                    {
                        ++iter;
                        continue;
                    }
                    log_info("markdown file has been updated, re-rendering: %s",
                             lf->get_filename().c_str());
                    this->tss_rendered_files.erase(rend_iter);
                }

                auto read_res = lf->read_file();
                if (read_res.isOk()) {
                    static const auto FRONT_MATTER_RE
                        = lnav::pcre2pp::code::from_const(
                            R"((?:^---\n(.*)\n---\n|^\+\+\+\n(.*)\n\+\+\+\n))",
                            PCRE2_MULTILINE | PCRE2_DOTALL);
                    static thread_local auto md
                        = FRONT_MATTER_RE.create_match_data();

                    auto content = read_res.unwrap();
                    auto content_sf = string_fragment::from_str(content);
                    std::string frontmatter;
                    text_format_t frontmatter_format{text_format_t::TF_UNKNOWN};

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
                            auto consumed = ypc.ypc_total_consumed;
                            if (consumed < content_sf.length()
                                && content_sf[consumed] == '\n')
                            {
                                frontmatter_format = text_format_t::TF_JSON;
                                frontmatter = string_fragment::from_str_range(
                                                  content, 0, consumed)
                                                  .to_string();
                                content_sf = content_sf.substr(consumed);
                            }
                        }
                    }

                    md2attr_line mdal;

                    mdal.with_source_path(lf->get_actual_path());
                    auto parse_res = md4cpp::parse(content_sf, mdal);

                    auto& rf = this->tss_rendered_files[lf->get_filename()];
                    rf.rf_mtime = st.st_mtime;
                    rf.rf_file_size = st.st_size;
                    rf.rf_text_source = std::make_unique<plain_text_source>();
                    rf.rf_text_source->set_text_format(lf->get_text_format());
                    rf.rf_text_source->register_view(this->tss_view);
                    if (parse_res.isOk()) {
                        auto& lf_meta = lf->get_embedded_metadata();

                        rf.rf_text_source->replace_with(parse_res.unwrap());

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
                            attr_line_t::from_ansi_str(content.c_str()));

                        rf.rf_text_source->replace_with(view_content);
                    }
                } else {
                    log_error("unable to read markdown file: %s -- %s",
                              lf->get_filename().c_str(),
                              read_res.unwrapErr().c_str());
                }
                ++iter;
                continue;
            }

            if (lf->is_indexing()
                && lf->get_text_format() != text_format_t::TF_BINARY)
            {
                auto ms_iter = this->tss_doc_metadata.find(lf->get_filename());

                if (!new_data && ms_iter != this->tss_doc_metadata.end()) {
                    if (st.st_mtime != ms_iter->second.ms_mtime
                        || st.st_size != ms_iter->second.ms_file_size)
                    {
                        log_debug(
                            "text file has changed, invalidating metadata.  "
                            "old: {mtime: %d size: %zu}, new: {mtime: %d "
                            "size: %zu}",
                            ms_iter->second.ms_mtime,
                            ms_iter->second.ms_file_size,
                            st.st_mtime,
                            st.st_size);
                        this->tss_doc_metadata.erase(ms_iter);
                        ms_iter = this->tss_doc_metadata.end();
                    }
                }

                if (ms_iter == this->tss_doc_metadata.end()) {
                    auto read_res = lf->read_file();

                    if (read_res.isOk()) {
                        auto content = attr_line_t(read_res.unwrap());

                        log_info("generating metadata for: %s (size=%zu)",
                                 lf->get_filename().c_str(),
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

                        this->tss_doc_metadata[lf->get_filename()]
                            = metadata_state{
                                st.st_mtime,
                                static_cast<file_ssize_t>(lf->get_index_size()),
                                lnav::document::discover_structure(
                                    content,
                                    line_range{0, -1},
                                    lf->get_text_format()),
                            };
                    } else {
                        log_error(
                            "%s: unable to read file for meta discover -- %s",
                            lf->get_filename().c_str(),
                            read_res.unwrapErr().c_str());
                        this->tss_doc_metadata[lf->get_filename()]
                            = metadata_state{
                                st.st_mtime,
                                static_cast<file_ssize_t>(lf->get_index_size()),
                                {},
                            };
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
        } catch (const line_buffer::error& e) {
            iter = this->tss_files.erase(iter);
            this->tss_rendered_files.erase(lf->get_filename());
            this->tss_doc_metadata.erase(lf->get_filename());
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
        }
    };
}

void
textfile_sub_source::quiesce()
{
    for (auto& lf : this->tss_files) {
        lf->quiesce();
    }
}

nonstd::optional<vis_line_t>
textfile_sub_source::row_for_anchor(const std::string& id)
{
    auto lf = this->current_file();
    if (!lf || id.empty()) {
        return nonstd::nullopt;
    }

    auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
    if (rend_iter != this->tss_rendered_files.end()) {
        return rend_iter->second.rf_text_source->row_for_anchor(id);
    }

    auto iter = this->tss_doc_metadata.find(lf->get_filename());
    if (iter == this->tss_doc_metadata.end()) {
        return nonstd::nullopt;
    }

    const auto& meta = iter->second.ms_metadata;
    nonstd::optional<vis_line_t> retval;

    auto is_ptr = startswith(id, "#/");
    if (is_ptr) {
        auto hier_sf = string_fragment::from_str(id).consume_n(2).value();
        std::vector<lnav::document::section_key_t> path;

        while (!hier_sf.empty()) {
            auto comp_pair = hier_sf.split_when(string_fragment::tag1{'/'});
            auto scan_res
                = scn::scan_value<int64_t>(comp_pair.first.to_string_view());
            if (scan_res && scan_res.empty()) {
                path.emplace_back(scan_res.value());
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
                const auto& child_anchor
                    = text_anchors::to_anchor_string(child_pair.first);

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
        for (const auto& child : hn->hn_named_children) {
            comps.emplace_back(child.first);
            anchor_generator(retval, comps, max_depth, child.second);
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

    auto lf = this->current_file();
    if (!lf) {
        return retval;
    }

    auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
    if (rend_iter != this->tss_rendered_files.end()) {
        return rend_iter->second.rf_text_source->get_anchors();
    }

    auto iter = this->tss_doc_metadata.find(lf->get_filename());
    if (iter == this->tss_doc_metadata.end()) {
        return retval;
    }

    const auto& meta = iter->second.ms_metadata;

    if (meta.m_sections_root == nullptr) {
        return retval;
    }

    std::vector<std::string> comps;
    size_t max_depth = 0;
    anchor_generator(retval, comps, max_depth, meta.m_sections_root.get());

    return retval;
}

static nonstd::optional<vis_line_t>
to_vis_line(const std::shared_ptr<logfile>& lf, file_off_t off)
{
    auto ll_opt = lf->line_for_offset(off);
    if (ll_opt != lf->end()) {
        return vis_line_t(std::distance(lf->cbegin(), ll_opt.value()));
    }

    return nonstd::nullopt;
}

nonstd::optional<vis_line_t>
textfile_sub_source::adjacent_anchor(vis_line_t vl, text_anchors::direction dir)
{
    auto lf = this->current_file();
    if (!lf) {
        return nonstd::nullopt;
    }

    log_debug("adjacent_anchor: %s:L%d:%s",
              lf->get_filename().c_str(),
              vl,
              dir == text_anchors::direction::prev ? "prev" : "next");
    auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
    if (rend_iter != this->tss_rendered_files.end()) {
        return rend_iter->second.rf_text_source->adjacent_anchor(vl, dir);
    }

    auto iter = this->tss_doc_metadata.find(lf->get_filename());
    if (iter == this->tss_doc_metadata.end()) {
        log_debug("  no metadata available");
        return nonstd::nullopt;
    }

    auto& md = iter->second.ms_metadata;
    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    if (vl >= lfo->lfo_filter_state.tfs_index.size()
        || md.m_sections_root == nullptr)
    {
        return nonstd::nullopt;
    }
    auto ll_iter = lf->begin() + lfo->lfo_filter_state.tfs_index[vl];
    auto line_offsets = lf->get_file_range(ll_iter, false);
    log_debug(
        "  range %d:%d", line_offsets.fr_offset, line_offsets.next_offset());
    auto path_for_line
        = md.path_for_range(line_offsets.fr_offset, line_offsets.next_offset());

    if (path_for_line.empty()) {
        log_debug("  no path found");
        auto neighbors_res = md.m_sections_root->line_neighbors(vl);
        if (!neighbors_res) {
            return nonstd::nullopt;
        }

        switch (dir) {
            case text_anchors::direction::prev: {
                if (neighbors_res->cnr_previous) {
                    return to_vis_line(
                        lf, neighbors_res->cnr_previous.value()->hn_start);
                }
                break;
            }
            case text_anchors::direction::next: {
                if (neighbors_res->cnr_next) {
                    return to_vis_line(
                        lf, neighbors_res->cnr_next.value()->hn_start);
                }
                break;
            }
        }
        return nonstd::nullopt;
    }

    auto last_key = path_for_line.back();
    path_for_line.pop_back();

    auto parent_opt = lnav::document::hier_node::lookup_path(
        md.m_sections_root.get(), path_for_line);
    if (!parent_opt) {
        return nonstd::nullopt;
    }
    auto parent = parent_opt.value();

    auto child_hn = parent->lookup_child(last_key);
    if (!child_hn) {
        // XXX "should not happen"
        return nonstd::nullopt;
    }

    auto neighbors_res = parent->child_neighbors(
        child_hn.value(), line_offsets.next_offset() + 1);
    if (!neighbors_res) {
        log_debug("  no neighbors found");
        return nonstd::nullopt;
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
        case text_anchors::direction::prev: {
            if (neighbors_res->cnr_previous) {
                return to_vis_line(
                    lf, neighbors_res->cnr_previous.value()->hn_start);
            }
            break;
        }
        case text_anchors::direction::next: {
            if (neighbors_res->cnr_next) {
                return to_vis_line(lf,
                                   neighbors_res->cnr_next.value()->hn_start);
            }
            break;
        }
    }

    return nonstd::nullopt;
}

nonstd::optional<std::string>
textfile_sub_source::anchor_for_row(vis_line_t vl)
{
    auto lf = this->current_file();
    if (!lf) {
        return nonstd::nullopt;
    }

    auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
    if (rend_iter != this->tss_rendered_files.end()) {
        return rend_iter->second.rf_text_source->anchor_for_row(vl);
    }

    auto iter = this->tss_doc_metadata.find(lf->get_filename());
    if (iter == this->tss_doc_metadata.end()) {
        return nonstd::nullopt;
    }

    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    if (vl >= lfo->lfo_filter_state.tfs_index.size()) {
        return nonstd::nullopt;
    }
    auto& md = iter->second.ms_metadata;
    auto ll_iter = lf->begin() + lfo->lfo_filter_state.tfs_index[vl];
    auto line_offsets = lf->get_file_range(ll_iter, false);
    auto path_for_line
        = md.path_for_range(line_offsets.fr_offset, line_offsets.next_offset());

    if (path_for_line.empty()) {
        return nonstd::nullopt;
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
                      return elem->get_filename() == filename;
                  });
    if (!lf_opt) {
        lf_opt = this->tss_hidden_files
            | lnav::itertools::find_if([&filename](const auto& elem) {
                     return elem->get_filename() == filename;
                 });
    }

    if (!lf_opt) {
        return false;
    }

    this->to_front(*(lf_opt.value()));

    return true;
}

logline*
textfile_sub_source::text_accel_get_line(vis_line_t vl)
{
    auto lf = this->current_file();
    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    return (lf->begin() + lfo->lfo_filter_state.tfs_index[vl]).base();
}

textfile_header_overlay::textfile_header_overlay(textfile_sub_source* src)
    : tho_src(src)
{
}

bool
textfile_header_overlay::list_static_overlay(const listview_curses& lv,
                                             int y,
                                             int bottom,
                                             attr_line_t& value_out)
{
    if (y != 0) {
        return false;
    }

    auto lf = this->tho_src->current_file();
    if (lf == nullptr) {
        return false;
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
    value_out.with_attr_for_all(VC_STYLE.value(text_attrs{A_UNDERLINE}));
    return true;
}
