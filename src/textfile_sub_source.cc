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

#include "textfile_sub_source.hh"

#include "base/ansi_scrubber.hh"
#include "base/fs_util.hh"
#include "base/injector.hh"
#include "base/itertools.hh"
#include "bound_tags.hh"
#include "config.h"
#include "lnav.events.hh"
#include "md2attr_line.hh"
#include "sqlitepp.hh"

using namespace lnav::roles::literals;

size_t
textfile_sub_source::text_line_count()
{
    size_t retval = 0;

    if (!this->tss_files.empty()) {
        std::shared_ptr<logfile> lf = this->current_file();
        auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
        if (rend_iter == this->tss_rendered_files.end()) {
            auto* lfo = (line_filter_observer*) lf->get_logline_observer();
            retval = lfo->lfo_filter_state.tfs_index.size();
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
    if (!this->tss_files.empty()) {
        std::shared_ptr<logfile> lf = this->current_file();
        auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
        if (rend_iter == this->tss_rendered_files.end()) {
            auto* lfo = dynamic_cast<line_filter_observer*>(
                lf->get_logline_observer());
            if (line < 0 || line >= lfo->lfo_filter_state.tfs_index.size()) {
                value_out.clear();
            } else {
                auto read_result = lf->read_line(
                    lf->begin() + lfo->lfo_filter_state.tfs_index[line]);
                if (read_result.isOk()) {
                    value_out = to_string(read_result.unwrap());
                }
            }
        } else {
            rend_iter->second.rf_text_source->text_value_for_line(
                tc, line, value_out, flags);
        }
    } else {
        value_out.clear();
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

    auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
    if (rend_iter != this->tss_rendered_files.end()) {
        rend_iter->second.rf_text_source->text_attrs_for_line(
            tc, row, value_out);
    }

    struct line_range lr;

    lr.lr_start = 0;
    lr.lr_end = -1;
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
            if (line < 0 || line >= lfo->lfo_filter_state.tfs_index.size()) {
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
    this->tss_view->reload_data();
}

void
textfile_sub_source::rotate_left()
{
    if (this->tss_files.size() > 1) {
        this->tss_files.push_back(this->tss_files.front());
        this->tss_files.pop_front();
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
        attr_line_t().append(lf->get_unique_path()),
        [this]() {
            return this->tss_files | lnav::itertools::map([](const auto& lf) {
                       return breadcrumb::possibility{
                           lf->get_unique_path(),
                           attr_line_t(lf->get_unique_path()),
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

    auto meta_iter = this->tss_doc_metadata.find(lf->get_filename());
    if (meta_iter != this->tss_doc_metadata.end()) {
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
            ll_iter->get_offset(),
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

bool
textfile_sub_source::rescan_files(
    textfile_sub_source::scan_callback& callback,
    nonstd::optional<ui_clock::time_point> deadline)
{
    static auto& lnav_db = injector::get<auto_sqlite3&>();

    file_iterator iter;
    bool retval = false;

    if (this->tss_view == nullptr || this->tss_view->is_paused()) {
        return retval;
    }

    std::vector<std::shared_ptr<logfile>> closed_files;
    for (iter = this->tss_files.begin(); iter != this->tss_files.end();) {
        std::shared_ptr<logfile> lf = (*iter);

        if (lf->is_closed()) {
            iter = this->tss_files.erase(iter);
            this->tss_rendered_files.erase(lf->get_filename());
            this->tss_doc_metadata.erase(lf->get_filename());
            this->detach_observer(lf);
            closed_files.template emplace_back(lf);
            continue;
        }

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

            switch (new_text_data) {
                case logfile::rebuild_result_t::NEW_LINES:
                case logfile::rebuild_result_t::NEW_ORDER:
                    retval = true;
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
                        auto_mem<yajl_handle_t> handle(yajl_free);

                        handle = yajl_alloc(&ypc.ypc_callbacks, nullptr, &ypc);
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

            if (!retval && lf->is_indexing()
                && lf->get_text_format() != text_format_t::TF_BINARY)
            {
                auto ms_iter = this->tss_doc_metadata.find(lf->get_filename());

                if (ms_iter != this->tss_doc_metadata.end()) {
                    if (st.st_mtime != ms_iter->second.ms_mtime
                        || st.st_size != ms_iter->second.ms_file_size)
                    {
                        this->tss_doc_metadata.erase(ms_iter);
                        ms_iter = this->tss_doc_metadata.end();
                    }
                }

                if (ms_iter == this->tss_doc_metadata.end()) {
                    auto read_res = lf->read_file();

                    if (read_res.isOk()) {
                        auto content = attr_line_t(read_res.unwrap());

                        log_info("generating metdata for: %s",
                                 lf->get_filename().c_str());
                        scrub_ansi_string(content.get_string(),
                                          &content.get_attrs());
                        this->tss_doc_metadata[lf->get_filename()]
                            = metadata_state{
                                st.st_mtime,
                                static_cast<file_ssize_t>(st.st_size),
                                lnav::document::discover_structure(
                                    content, line_range{0, -1}),
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

    if (retval) {
        this->tss_view->search_new_data();
    }

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
            this->tss_view->set_top(vis_line_t(new_top_opt.value()));
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
    if (!lf) {
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

    lnav::document::hier_node::depth_first(
        meta.m_sections_root.get(),
        [lf, &id, &retval](const lnav::document::hier_node* node) {
            for (const auto& child_pair : node->hn_named_children) {
                auto child_anchor
                    = text_anchors::to_anchor_string(child_pair.first);

                if (child_anchor == id) {
                    auto ll_opt
                        = lf->line_for_offset(child_pair.second->hn_start);
                    if (ll_opt != lf->end()) {
                        retval = vis_line_t(
                            std::distance(lf->cbegin(), ll_opt.value()));
                    }
                }
            }
        });

    return retval;
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

    lnav::document::hier_node::depth_first(
        meta.m_sections_root.get(),
        [&retval](const lnav::document::hier_node* node) {
            if (retval.size() > 100) {
                return;
            }

            for (const auto& child_pair : node->hn_named_children) {
                retval.emplace(
                    text_anchors::to_anchor_string(child_pair.first));
            }
        });

    return retval;
}

nonstd::optional<std::string>
textfile_sub_source::anchor_for_row(vis_line_t vl)
{
    nonstd::optional<std::string> retval;

    auto lf = this->current_file();
    if (!lf) {
        return retval;
    }

    auto rend_iter = this->tss_rendered_files.find(lf->get_filename());
    if (rend_iter != this->tss_rendered_files.end()) {
        return rend_iter->second.rf_text_source->anchor_for_row(vl);
    }

    auto iter = this->tss_doc_metadata.find(lf->get_filename());
    if (iter == this->tss_doc_metadata.end()) {
        return retval;
    }

    auto* lfo = dynamic_cast<line_filter_observer*>(lf->get_logline_observer());
    if (vl >= lfo->lfo_filter_state.tfs_index.size()) {
        return retval;
    }
    auto ll_iter = lf->begin() + lfo->lfo_filter_state.tfs_index[vl];
    auto ll_next_iter = ll_iter + 1;
    auto end_offset = (ll_next_iter == lf->end())
        ? lf->get_index_size() - 1
        : ll_next_iter->get_offset() - 1;
    iter->second.ms_metadata.m_sections_tree.visit_overlapping(
        ll_iter->get_offset(),
        end_offset,
        [&retval](const lnav::document::section_interval_t& iv) {
            retval = iv.value.match(
                [](const std::string& str) {
                    return nonstd::make_optional(
                        text_anchors::to_anchor_string(str));
                },
                [](size_t) { return nonstd::nullopt; });
        });

    return retval;
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
