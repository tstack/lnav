/**
 * Copyright (c) 2018, Timothy Stack
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

#include "view_helpers.hh"

#include "base/itertools.hh"
#include "bound_tags.hh"
#include "config.h"
#include "document.sections.hh"
#include "environ_vtab.hh"
#include "filter_sub_source.hh"
#include "help-md.h"
#include "intervaltree/IntervalTree.h"
#include "lnav.hh"
#include "lnav.indexing.hh"
#include "md2attr_line.hh"
#include "md4cpp.hh"
#include "pretty_printer.hh"
#include "shlex.hh"
#include "sql_help.hh"
#include "sql_util.hh"
#include "static_file_vtab.hh"
#include "timeline_source.hh"
#include "view_helpers.crumbs.hh"
#include "view_helpers.examples.hh"
#include "view_helpers.hist.hh"
#include "vtab_module.hh"

using namespace std::chrono_literals;
using namespace lnav::roles::literals;

const char* lnav_view_strings[LNV__MAX + 1] = {
    "log",
    "text",
    "help",
    "histogram",
    "db",
    "schema",
    "pretty",
    "spectro",
    "timeline",

    nullptr,
};

const char* lnav_view_titles[LNV__MAX] = {
    "LOG",
    "TEXT",
    "HELP",
    "HIST",
    "DB",
    "SCHEMA",
    "PRETTY",
    "SPECTRO",
    "TIMELINE",
};

std::optional<lnav_view_t>
view_from_string(const char* name)
{
    if (name == nullptr) {
        return std::nullopt;
    }

    auto* view_name_iter
        = std::find_if(std::begin(lnav_view_strings),
                       std::end(lnav_view_strings),
                       [&](const char* v) {
                           return v != nullptr && strcasecmp(v, name) == 0;
                       });

    if (view_name_iter == std::end(lnav_view_strings)) {
        return std::nullopt;
    }

    return lnav_view_t(view_name_iter - lnav_view_strings);
}

static void
open_schema_view()
{
    textview_curses* schema_tc = &lnav_data.ld_views[LNV_SCHEMA];
    std::string schema;

    dump_sqlite_schema(lnav_data.ld_db, schema);

    schema += "\n\n-- Virtual Table Definitions --\n\n";
    schema += ENVIRON_CREATE_STMT;
    schema += STATIC_FILE_CREATE_STMT;
    schema += vtab_module_schemas;
    for (const auto& vtab_iter : *lnav_data.ld_vtab_manager) {
        schema += "\n" + vtab_iter.second->get_table_statement();
    }

    delete schema_tc->get_sub_source();

    auto* pts = new plain_text_source(schema);
    pts->set_text_format(text_format_t::TF_SQL);

    schema_tc->set_sub_source(pts);
    schema_tc->redo_search();
}

static void
open_timeline_view()
{
    auto* timeline_tc = &lnav_data.ld_views[LNV_TIMELINE];
    auto* timeline_src
        = dynamic_cast<timeline_source*>(timeline_tc->get_sub_source());

    timeline_src->rebuild_indexes();
    timeline_tc->reload_data();
    timeline_tc->redo_search();
}

class pretty_sub_source : public plain_text_source {
public:
    void set_indents(std::set<size_t>&& indents)
    {
        this->tds_doc_sections.m_indents = std::move(indents);
    }

    void set_sections_root(std::unique_ptr<lnav::document::hier_node>&& hn)
    {
        this->tds_doc_sections.m_sections_root = std::move(hn);
    }

    void text_crumbs_for_line(int line,
                              std::vector<breadcrumb::crumb>& crumbs) override
    {
        text_sub_source::text_crumbs_for_line(line, crumbs);

        if (line < 0 || static_cast<size_t>(line) > this->tds_lines.size()) {
            return;
        }

        const auto& tl = this->tds_lines[line];
        const auto initial_size = crumbs.size();
        lnav::document::hier_node* root_node{nullptr};

        this->pss_hier_tree->template visit_overlapping(
            tl.tl_offset,
            [&root_node](const auto& hier_iv) { root_node = hier_iv.value; });
        this->pss_interval_tree->visit_overlapping(
            tl.tl_offset,
            tl.tl_offset + tl.tl_value.length(),
            [&crumbs, root_node, this, initial_size](const auto& iv) {
                auto path = crumbs | lnav::itertools::skip(initial_size)
                    | lnav::itertools::map(&breadcrumb::crumb::c_key)
                    | lnav::itertools::append(iv.value);
                auto poss_provider = [root_node, path]() {
                    std::vector<breadcrumb::possibility> retval;
                    auto curr_node = lnav::document::hier_node::lookup_path(
                        root_node, path);
                    if (curr_node) {
                        auto* parent_node = curr_node.value()->hn_parent;

                        if (parent_node != nullptr) {
                            for (const auto& sibling :
                                 parent_node->hn_named_children)
                            {
                                retval.template emplace_back(sibling.first);
                            }
                        }
                    }
                    return retval;
                };
                auto path_performer =
                    [this, root_node, path](
                        const breadcrumb::crumb::key_t& value) {
                        auto curr_node = lnav::document::hier_node::lookup_path(
                            root_node, path);
                        if (!curr_node) {
                            return;
                        }
                        auto* parent_node = curr_node.value()->hn_parent;

                        if (parent_node == nullptr) {
                            return;
                        }
                        value.template match(
                            [this, parent_node](const std::string& str) {
                                auto sib_iter
                                    = parent_node->hn_named_children.find(str);
                                if (sib_iter
                                    != parent_node->hn_named_children.end()) {
                                    this->line_for_offset(
                                        sib_iter->second->hn_start)
                                        | [](const auto new_top) {
                                              lnav_data.ld_views[LNV_PRETTY]
                                                  .set_selection(new_top);
                                          };
                                }
                            },
                            [this, parent_node](size_t index) {
                                if (index >= parent_node->hn_children.size()) {
                                    return;
                                }
                                auto sib
                                    = parent_node->hn_children[index].get();
                                this->line_for_offset(sib->hn_start) |
                                    [](const auto new_top) {
                                        lnav_data.ld_views[LNV_PRETTY]
                                            .set_selection(new_top);
                                    };
                            });
                    };
                crumbs.template emplace_back(iv.value,
                                             std::move(poss_provider),
                                             std::move(path_performer));
                auto curr_node
                    = lnav::document::hier_node::lookup_path(root_node, path);
                if (curr_node
                    && curr_node.value()->hn_parent->hn_children.size()
                        != curr_node.value()
                               ->hn_parent->hn_named_children.size())
                {
                    auto node = lnav::document::hier_node::lookup_path(
                        root_node, path);

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
        auto node = lnav::document::hier_node::lookup_path(root_node, path);

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
                            this->line_for_offset(child_iter->second->hn_start)
                                | [](const auto new_top) {
                                      lnav_data.ld_views[LNV_PRETTY]
                                          .set_selection(new_top);
                                  };
                        }
                    },
                    [this, curr_node](size_t index) {
                        auto* child = curr_node->hn_children[index].get();
                        this->line_for_offset(child->hn_start) |
                            [](const auto new_top) {
                                lnav_data.ld_views[LNV_PRETTY].set_selection(
                                    new_top);
                            };
                    });
            };
            crumbs.emplace_back("", "\u22ef", poss_provider, path_performer);
            crumbs.back().c_expected_input
                = node.value()->hn_named_children.empty()
                ? breadcrumb::crumb::expected_input_t::index
                : breadcrumb::crumb::expected_input_t::index_or_exact;
        }
    }

    using hier_tree_t
        = interval_tree::IntervalTree<file_off_t, lnav::document::hier_node*>;
    using hier_interval_t
        = interval_tree::Interval<file_off_t, lnav::document::hier_node*>;

    std::shared_ptr<lnav::document::sections_tree_t> pss_interval_tree;
    std::shared_ptr<hier_tree_t> pss_hier_tree;
    std::unique_ptr<lnav::document::hier_node> pss_root_node;
};

static void
open_pretty_view()
{
    static const char* NOTHING_MSG = "Nothing to pretty-print";

    auto* top_tc = *lnav_data.ld_view_stack.top();
    auto* pretty_tc = &lnav_data.ld_views[LNV_PRETTY];
    auto* log_tc = &lnav_data.ld_views[LNV_LOG];
    auto* text_tc = &lnav_data.ld_views[LNV_TEXT];

    if (top_tc == log_tc && log_tc->get_inner_height() == 0
        && text_tc->get_inner_height() > 0)
    {
        lnav_data.ld_view_stack.push_back(text_tc);
        top_tc = text_tc;
    }

    if (top_tc != log_tc && top_tc != text_tc) {
        return;
    }

    attr_line_t full_text;

    delete pretty_tc->get_sub_source();
    pretty_tc->set_sub_source(nullptr);
    if (top_tc->get_inner_height() == 0) {
        pretty_tc->set_sub_source(new plain_text_source(NOTHING_MSG));
        return;
    }

    std::vector<lnav::document::section_interval_t> all_intervals;
    std::vector<std::unique_ptr<lnav::document::hier_node>> hier_nodes;
    std::vector<pretty_sub_source::hier_interval_t> hier_tree_vec;
    std::set<size_t> pretty_indents;
    if (top_tc == log_tc) {
        auto& lss = lnav_data.ld_log_source;
        bool first_line = true;
        auto start_off = size_t{0};

        for (auto vl = log_tc->get_top(); vl <= log_tc->get_bottom(); ++vl) {
            content_line_t cl = lss.at(vl);
            auto lf = lss.find(cl);
            auto ll = lf->begin() + cl;
            shared_buffer_ref sbr;

            if (!first_line && !ll->is_message()) {
                continue;
            }
            auto ll_start = lf->message_start(ll);
            attr_line_t al;

            vl -= vis_line_t(std::distance(ll_start, ll));
            lss.text_value_for_line(
                *log_tc,
                vl,
                al.get_string(),
                text_sub_source::RF_FULL | text_sub_source::RF_REWRITE);
            lss.text_attrs_for_line(*log_tc, vl, al.get_attrs());
            {
                const auto orig_lr
                    = find_string_attr_range(al.get_attrs(), &SA_ORIGINAL_LINE);
                require(orig_lr.is_valid());
            }
            scrub_ansi_string(al.get_string(), &al.get_attrs());
            if (log_tc->get_hide_fields()) {
                al.apply_hide();
            }

            const auto orig_lr
                = find_string_attr_range(al.get_attrs(), &SA_ORIGINAL_LINE);
            require(orig_lr.is_valid());
            const auto body_lr
                = find_string_attr_range(al.get_attrs(), &SA_BODY);
            auto orig_al = al.subline(orig_lr.lr_start, orig_lr.length());
            auto prefix_al = al.subline(0, orig_lr.lr_start);
            attr_line_t pretty_al;
            std::vector<attr_line_t> pretty_lines;
            data_scanner ds(orig_al.get_string(),
                            body_lr.is_valid()
                                ? body_lr.lr_start - orig_lr.lr_start
                                : orig_lr.lr_start);
            pretty_printer pp(&ds, orig_al.get_attrs());

            if (body_lr.is_valid()) {
                // TODO: dump more details of the line in the output.
                pp.append_to(pretty_al);
            } else {
                pretty_al = orig_al;
            }

            pretty_al.split_lines(pretty_lines);
            auto prefix_len = prefix_al.length();

            auto curr_intervals = pp.take_intervals();
            auto line_hier_root = pp.take_hier_root();
            auto curr_indents = pp.take_indents()
                | lnav::itertools::map([&prefix_len](const auto& elem) {
                                    return elem + prefix_len;
                                });
            auto line_off = 0;
            for (auto& pretty_line : pretty_lines) {
                if (pretty_line.empty() && &pretty_line == &pretty_lines.back())
                {
                    break;
                }
                pretty_line.insert(0, prefix_al);
                for (auto& interval : curr_intervals) {
                    if (line_off <= interval.start) {
                        interval.start += prefix_len;
                        interval.stop += prefix_len;
                    } else if (line_off < interval.stop) {
                        interval.stop += prefix_len;
                    }
                }
                lnav::document::hier_node::depth_first(
                    line_hier_root.get(),
                    [line_off, prefix_len = prefix_len](auto* hn) {
                        if (line_off <= hn->hn_start) {
                            hn->hn_start += prefix_len;
                        }
                    });
                line_off += pretty_line.get_string().length();
                full_text.append(pretty_line);
                full_text.append("\n");
            }

            first_line = false;
            for (auto& interval : curr_intervals) {
                interval.start += start_off;
                interval.stop += start_off;
            }
            lnav::document::hier_node::depth_first(
                line_hier_root.get(),
                [start_off](auto* hn) { hn->hn_start += start_off; });
            hier_nodes.emplace_back(std::move(line_hier_root));
            hier_tree_vec.emplace_back(
                start_off, start_off + line_off, hier_nodes.back().get());
            all_intervals.insert(
                all_intervals.end(),
                std::make_move_iterator(curr_intervals.begin()),
                std::make_move_iterator(curr_intervals.end()));
            pretty_indents.insert(curr_indents.begin(), curr_indents.end());

            start_off += line_off;
        }
    } else if (top_tc == text_tc) {
        if (text_tc->listview_rows(*text_tc)) {
            std::vector<attr_line_t> rows;
            rows.resize(text_tc->get_bottom() - text_tc->get_top() + 1);
            text_tc->listview_value_for_rows(
                *text_tc, text_tc->get_top(), rows);
            attr_line_t orig_al;

            for (auto& row : rows) {
                remove_string_attr(row.get_attrs(), &VC_BLOCK_ELEM);
                for (auto& attr : row.get_attrs()) {
                    if (attr.sa_type == &VC_ROLE) {
                        auto role = attr.sa_value.get<role_t>();

                        if (role == text_tc->tc_cursor_role
                            || role == text_tc->tc_disabled_cursor_role)
                        {
                            attr.sa_range.lr_end = attr.sa_range.lr_start;
                        }
                    }
                }
                orig_al.append(row);
            }

            data_scanner ds(orig_al.get_string());
            pretty_printer pp(&ds, orig_al.get_attrs());

            pp.append_to(full_text);

            all_intervals = pp.take_intervals();
            hier_nodes.emplace_back(pp.take_hier_root());
            hier_tree_vec.emplace_back(
                0, full_text.length(), hier_nodes.back().get());
            pretty_indents = pp.take_indents();
        }
    }
    auto* pts = new pretty_sub_source();
    pts->pss_interval_tree = std::make_shared<lnav::document::sections_tree_t>(
        std::move(all_intervals));
    auto root_node = std::make_unique<lnav::document::hier_node>();
    root_node->hn_children = std::move(hier_nodes);
    pts->pss_hier_tree = std::make_shared<pretty_sub_source::hier_tree_t>(
        std::move(hier_tree_vec));
    pts->pss_root_node = std::move(root_node);
    pts->set_indents(std::move(pretty_indents));

    pts->replace_with_mutable(full_text,
                              top_tc->get_sub_source()->get_text_format());
    pretty_tc->set_sub_source(pts);
    if (lnav_data.ld_last_pretty_print_top != log_tc->get_top()) {
        pretty_tc->set_top(0_vl);
    }
    lnav_data.ld_last_pretty_print_top = log_tc->get_top();
    pretty_tc->redo_search();
}

static void
build_all_help_text()
{
    if (!lnav_data.ld_help_source.empty()) {
        return;
    }

    shlex lexer(help_md.to_string_fragment());
    std::string sub_help_text;

    lexer.with_ignore_quotes(true).eval(
        sub_help_text,
        scoped_resolver{&lnav_data.ld_exec_context.ec_global_vars});

    md2attr_line mdal;
    auto parse_res = md4cpp::parse(sub_help_text, mdal);
    attr_line_t all_help_text = parse_res.unwrap();

    std::map<std::string, const help_text*> sql_funcs;
    std::map<std::string, const help_text*> sql_keywords;

    for (const auto& iter : sqlite_function_help) {
        switch (iter.second->ht_context) {
            case help_context_t::HC_SQL_FUNCTION:
            case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION:
                sql_funcs[iter.second->ht_name] = iter.second;
                break;
            case help_context_t::HC_SQL_KEYWORD:
                sql_keywords[iter.second->ht_name] = iter.second;
                break;
            default:
                break;
        }
    }

    all_help_text.append("\n").append("Command Reference"_h2);

    for (const auto& cmd : lnav_commands) {
        if (cmd.second->c_help.ht_summary == nullptr) {
            continue;
        }
        all_help_text.append(2, '\n');
        format_help_text_for_term(cmd.second->c_help, 70, all_help_text);
        if (!cmd.second->c_help.ht_example.empty()) {
            all_help_text.append("\n");
            format_example_text_for_term(
                cmd.second->c_help, eval_example, 90, all_help_text);
        }
    }

    all_help_text.append("\n").append("SQL Reference"_h2);

    for (const auto& iter : sql_funcs) {
        all_help_text.append(2, '\n');
        format_help_text_for_term(*iter.second, 70, all_help_text);
        if (!iter.second->ht_example.empty()) {
            all_help_text.append(1, '\n');
            format_example_text_for_term(
                *iter.second, eval_example, 90, all_help_text);
        }
    }

    for (const auto& iter : sql_keywords) {
        all_help_text.append(2, '\n');
        format_help_text_for_term(*iter.second, 70, all_help_text);
        if (!iter.second->ht_example.empty()) {
            all_help_text.append(1, '\n');
            format_example_text_for_term(
                *iter.second, eval_example, 79, all_help_text);
        }
    }

    lnav_data.ld_help_source.replace_with(all_help_text);
    lnav_data.ld_views[LNV_HELP].redo_search();
}

bool
handle_winch()
{
    static auto* filter_source = injector::get<filter_sub_source*>();

    if (!lnav_data.ld_winched) {
        return false;
    }

    struct winsize size;

    lnav_data.ld_winched = false;

    if (ioctl(fileno(stdout), TIOCGWINSZ, &size) == 0) {
        resizeterm(size.ws_row, size.ws_col);
    }
    if (lnav_data.ld_rl_view != nullptr) {
        lnav_data.ld_rl_view->do_update();
        lnav_data.ld_rl_view->window_change();
    }
    filter_source->fss_editor->window_change();
    for (auto& sc : lnav_data.ld_status) {
        sc.window_change();
    }
    lnav_data.ld_view_stack.set_needs_update();
    lnav_data.ld_doc_view.set_needs_update();
    lnav_data.ld_example_view.set_needs_update();
    lnav_data.ld_match_view.set_needs_update();
    lnav_data.ld_filter_view.set_needs_update();
    lnav_data.ld_files_view.set_needs_update();
    lnav_data.ld_file_details_view.set_needs_update();
    lnav_data.ld_spectro_details_view.set_needs_update();
    lnav_data.ld_timeline_details_view.set_needs_update();
    lnav_data.ld_user_message_view.set_needs_update();

    return true;
}

void
layout_views()
{
    static constexpr auto FILES_FOCUSED_WIDTH = 40;
    static constexpr auto FILES_BLURRED_WIDTH = 20;

    static auto* breadcrumb_view = injector::get<breadcrumb_curses*>();
    int width, height;
    getmaxyx(lnav_data.ld_window, height, width);

    int doc_height;
    bool doc_side_by_side = width > (90 + 60);
    bool preview_open0
        = !lnav_data.ld_preview_status_source[0].get_description().empty();
    bool preview_open1
        = !lnav_data.ld_preview_status_source[1].get_description().empty();
    bool filters_supported = false;
    auto is_spectro = false;
    auto is_timeline = false;

    lnav_data.ld_view_stack.top() | [&](auto tc) {
        is_spectro = (tc == &lnav_data.ld_views[LNV_SPECTRO]);
        is_timeline = (tc == &lnav_data.ld_views[LNV_TIMELINE]);

        auto* tss = tc->get_sub_source();

        if (tss == nullptr) {
            return;
        }

        if (tss->tss_supports_filtering) {
            filters_supported = true;
        }
    };

    if (doc_side_by_side) {
        doc_height = std::max(lnav_data.ld_doc_source.text_line_count(),
                              lnav_data.ld_example_source.text_line_count());
    } else {
        doc_height = lnav_data.ld_doc_source.text_line_count()
            + lnav_data.ld_example_source.text_line_count();
    }

    int preview_height0 = lnav_data.ld_preview_hidden
        ? 0
        : lnav_data.ld_preview_view[0].get_inner_height();
    if (!lnav_data.ld_preview_hidden
        && lnav_data.ld_preview_view[0].get_overlay_source() != nullptr)
    {
        preview_height0 = 6;  // XXX extra height for db overlay
    }
    int preview_height1 = lnav_data.ld_preview_hidden
        ? 0
        : lnav_data.ld_preview_view[1].get_inner_height();
    if (!lnav_data.ld_preview_hidden
        && lnav_data.ld_preview_view[1].get_overlay_source() != nullptr)
    {
        preview_height1 = 6;  // XXX extra height for db overlay
    }

    int match_rows = lnav_data.ld_match_source.text_line_count();
    int match_height = std::min(match_rows, (height - 4) / 2);
    lnav_data.ld_match_view.set_height(vis_line_t(match_height));

    int um_rows = lnav_data.ld_user_message_source.text_line_count();
    if (um_rows > 0
        && std::chrono::steady_clock::now()
            > lnav_data.ld_user_message_expiration)
    {
        lnav_data.ld_user_message_source.clear();
        um_rows = 0;
    }
    auto um_height = std::min(um_rows, (height - 4) / 2);
    lnav_data.ld_user_message_view.set_height(vis_line_t(um_height));

    auto config_panel_open = (lnav_data.ld_mode == ln_mode_t::FILTER
                              || lnav_data.ld_mode == ln_mode_t::FILES
                              || lnav_data.ld_mode == ln_mode_t::FILE_DETAILS
                              || lnav_data.ld_mode == ln_mode_t::SEARCH_FILTERS
                              || lnav_data.ld_mode == ln_mode_t::SEARCH_FILES);
    auto filters_open = (lnav_data.ld_mode == ln_mode_t::FILTER
                         || lnav_data.ld_mode == ln_mode_t::SEARCH_FILTERS);
    auto files_open = (lnav_data.ld_mode == ln_mode_t::FILES
                       || lnav_data.ld_mode == ln_mode_t::FILE_DETAILS
                       || lnav_data.ld_mode == ln_mode_t::SEARCH_FILES);
    auto files_width = lnav_data.ld_mode == ln_mode_t::FILES
        ? FILES_FOCUSED_WIDTH
        : FILES_BLURRED_WIDTH;
    int filter_height;

    switch (lnav_data.ld_mode) {
        case ln_mode_t::FILES:
        case ln_mode_t::FILTER:
            filter_height = 5;
            break;
        case ln_mode_t::FILE_DETAILS:
            filter_height = 15;
            break;
        default:
            filter_height = 0;
            break;
    }

    bool breadcrumb_open = (lnav_data.ld_mode == ln_mode_t::BREADCRUMBS);

    auto bottom_min = std::min(2 + 3, height);
    auto bottom = clamped<int>::from(height, bottom_min, height);

    lnav_data.ld_rl_view->set_y(height - 1);
    bottom -= lnav_data.ld_rl_view->get_height();
    lnav_data.ld_rl_view->set_width(width);

    breadcrumb_view->set_width(width);

    bool vis;
    vis = bottom.try_consume(lnav_data.ld_match_view.get_height());
    lnav_data.ld_match_view.set_y(bottom);
    lnav_data.ld_match_view.set_visible(vis);

    vis = bottom.try_consume(um_height);
    lnav_data.ld_user_message_view.set_y(bottom);
    lnav_data.ld_user_message_view.set_visible(vis);

    bottom -= 1;
    lnav_data.ld_status[LNS_BOTTOM].set_y(bottom);
    lnav_data.ld_status[LNS_BOTTOM].set_width(width);
    lnav_data.ld_status[LNS_BOTTOM].set_enabled(!config_panel_open
                                                && !breadcrumb_open);

    vis = preview_open1 && bottom.try_consume(preview_height1 + 1);
    lnav_data.ld_preview_view[1].set_height(vis_line_t(preview_height1));
    lnav_data.ld_preview_view[1].set_y(bottom + 1);
    lnav_data.ld_preview_view[1].set_visible(vis);

    lnav_data.ld_status[LNS_PREVIEW1].set_y(bottom);
    lnav_data.ld_status[LNS_PREVIEW1].set_width(width);
    lnav_data.ld_status[LNS_PREVIEW1].set_visible(vis);

    vis = preview_open0 && bottom.try_consume(preview_height0 + 1);
    lnav_data.ld_preview_view[0].set_height(vis_line_t(preview_height0));
    lnav_data.ld_preview_view[0].set_y(bottom + 1);
    lnav_data.ld_preview_view[0].set_visible(vis);

    lnav_data.ld_status[LNS_PREVIEW0].set_y(bottom);
    lnav_data.ld_status[LNS_PREVIEW0].set_width(width);
    lnav_data.ld_status[LNS_PREVIEW0].set_visible(vis);

    if (doc_side_by_side && doc_height > 0) {
        vis = bottom.try_consume(doc_height + 1);
        lnav_data.ld_example_view.set_height(vis_line_t(doc_height));
        lnav_data.ld_example_view.set_x(90);
        lnav_data.ld_example_view.set_y(bottom + 1);
    } else if (doc_height > 0 && bottom.available_to_consume(doc_height + 1)) {
        lnav_data.ld_example_view.set_height(
            vis_line_t(lnav_data.ld_example_source.text_line_count()));
        vis = bottom.try_consume(lnav_data.ld_example_view.get_height());
        lnav_data.ld_example_view.set_x(0);
        lnav_data.ld_example_view.set_y(bottom);
    } else {
        vis = false;
        lnav_data.ld_example_view.set_height(0_vl);
    }
    lnav_data.ld_example_view.set_visible(vis);

    if (doc_side_by_side) {
        lnav_data.ld_doc_view.set_height(vis_line_t(doc_height));
        lnav_data.ld_doc_view.set_y(bottom + 1);
    } else if (doc_height > 0) {
        lnav_data.ld_doc_view.set_height(
            vis_line_t(lnav_data.ld_doc_source.text_line_count()));
        vis = bottom.try_consume(lnav_data.ld_doc_view.get_height() + 1);
        lnav_data.ld_doc_view.set_y(bottom + 1);
    } else {
        vis = false;
    }
    lnav_data.ld_doc_view.set_visible(vis);

    auto has_doc = lnav_data.ld_example_view.get_height() > 0_vl
        || lnav_data.ld_doc_view.get_height() > 0_vl;
    lnav_data.ld_status[LNS_DOC].set_y(bottom);
    lnav_data.ld_status[LNS_DOC].set_width(width);
    lnav_data.ld_status[LNS_DOC].set_visible(has_doc && vis);

    if (is_timeline) {
        vis = bottom.try_consume(lnav_data.ld_timeline_details_view.get_height()
                                 + 1);
    } else {
        vis = false;
    }
    lnav_data.ld_timeline_details_view.set_y(bottom + 1);
    lnav_data.ld_timeline_details_view.set_width(width);
    lnav_data.ld_timeline_details_view.set_visible(vis);

    lnav_data.ld_status[LNS_TIMELINE].set_y(bottom);
    lnav_data.ld_status[LNS_TIMELINE].set_width(width);
    lnav_data.ld_status[LNS_TIMELINE].set_visible(vis);

    vis = bottom.try_consume(filter_height + (config_panel_open ? 1 : 0)
                             + (filters_supported ? 1 : 0));
    lnav_data.ld_filter_view.set_height(vis_line_t(filter_height));
    lnav_data.ld_filter_view.set_y(bottom + 2);
    lnav_data.ld_filter_view.set_width(width);
    lnav_data.ld_filter_view.set_visible(filters_open && vis);

    lnav_data.ld_files_view.set_height(vis_line_t(filter_height));
    lnav_data.ld_files_view.set_y(bottom + 2);
    lnav_data.ld_files_view.set_width(files_width);
    lnav_data.ld_files_view.set_visible(files_open && vis);

    lnav_data.ld_file_details_view.set_height(vis_line_t(filter_height));
    lnav_data.ld_file_details_view.set_y(bottom + 2);
    lnav_data.ld_file_details_view.set_x(files_width);
    lnav_data.ld_file_details_view.set_width(
        std::clamp(width - files_width, 0, width));
    lnav_data.ld_file_details_view.set_visible(files_open && vis);

    lnav_data.ld_status[LNS_FILTER_HELP].set_visible(config_panel_open && vis);
    lnav_data.ld_status[LNS_FILTER_HELP].set_y(bottom + 1);
    lnav_data.ld_status[LNS_FILTER_HELP].set_width(width);

    lnav_data.ld_status[LNS_FILTER].set_visible(vis);
    lnav_data.ld_status[LNS_FILTER].set_enabled(config_panel_open);
    lnav_data.ld_status[LNS_FILTER].set_y(bottom);
    lnav_data.ld_status[LNS_FILTER].set_width(width);

    vis = is_spectro && bottom.try_consume(5 + 1);
    lnav_data.ld_spectro_details_view.set_y(bottom + 1);
    lnav_data.ld_spectro_details_view.set_height(5_vl);
    lnav_data.ld_spectro_details_view.set_width(width);
    lnav_data.ld_spectro_details_view.set_visible(vis);

    lnav_data.ld_status[LNS_SPECTRO].set_y(bottom);
    lnav_data.ld_status[LNS_SPECTRO].set_width(width);
    lnav_data.ld_status[LNS_SPECTRO].set_visible(vis);
    lnav_data.ld_status[LNS_SPECTRO].set_enabled(lnav_data.ld_mode
                                                 == ln_mode_t::SPECTRO_DETAILS);

    auto bottom_used = bottom - height;
    for (auto& tc : lnav_data.ld_views) {
        tc.set_height(vis_line_t(bottom_used));
    }
}

void
update_hits(textview_curses* tc)
{
    if (isendwin()) {
        return;
    }

    auto top_tc = lnav_data.ld_view_stack.top();

    if (top_tc && tc == *top_tc) {
        lnav_data.ld_bottom_source.update_hits(tc);

        if (lnav_data.ld_mode == ln_mode_t::SEARCH) {
            constexpr auto MAX_MATCH_COUNT = 10_vl;
            const auto PREVIEW_SIZE = MAX_MATCH_COUNT + 1_vl;

            int preview_count = 0;
            auto& bm = tc->get_bookmarks();
            const auto& bv = bm[&textview_curses::BM_SEARCH];
            auto vl = tc->get_top();
            unsigned long width;
            vis_line_t height;
            attr_line_t all_matches;
            char linebuf[64];
            int last_line = tc->get_inner_height();

            snprintf(linebuf, sizeof(linebuf), "%d", last_line);
            auto max_line_width = static_cast<int>(strlen(linebuf));

            tc->get_dimensions(height, width);
            vl += height;
            if (vl > PREVIEW_SIZE) {
                vl -= PREVIEW_SIZE;
            }

            auto prev_vl = bv.prev(tc->get_top());

            if (prev_vl) {
                if (prev_vl.value() < 0_vl
                    || prev_vl.value() >= tc->get_inner_height())
                {
                    log_error("stale search bookmark for %s: %d",
                              tc->get_title().c_str(),
                              prev_vl.value());
                } else {
                    attr_line_t al;

                    tc->textview_value_for_row(prev_vl.value(), al);
                    snprintf(linebuf,
                             sizeof(linebuf),
                             "L%*d: ",
                             max_line_width,
                             (int) prev_vl.value());
                    all_matches.append(linebuf).append(al);
                    preview_count += 1;
                }
            }

            std::optional<vis_line_t> next_vl;
            while ((next_vl = bv.next(vl)) && preview_count < MAX_MATCH_COUNT) {
                if (next_vl.value() < 0_vl
                    || next_vl.value() >= tc->get_inner_height())
                {
                    log_error("stale search bookmark for %s: %d",
                              tc->get_title().c_str(),
                              next_vl.value());
                    break;
                }

                attr_line_t al;

                vl = next_vl.value();
                tc->textview_value_for_row(vl, al);
                if (preview_count > 0) {
                    all_matches.append("\n");
                }
                snprintf(linebuf,
                         sizeof(linebuf),
                         "L%*d: ",
                         max_line_width,
                         (int) vl);
                all_matches.append(linebuf).append(al);
                preview_count += 1;
            }

            if (preview_count > 0) {
                lnav_data.ld_preview_status_source[0]
                    .get_description()
                    .set_value("Matching lines for search");
                lnav_data.ld_preview_view[0].set_sub_source(
                    &lnav_data.ld_preview_source[0]);
                lnav_data.ld_preview_source[0]
                    .replace_with(all_matches)
                    .set_text_format(text_format_t::TF_UNKNOWN);
                lnav_data.ld_preview_view[0].set_needs_update();
            }
        }
    }
}

static std::unordered_map<std::string, attr_line_t> EXAMPLE_RESULTS;

static void
execute_example(const help_text& ht)
{
    static const std::set<std::string> IGNORED_NAMES = {"ATTACH"};

    if (IGNORED_NAMES.count(ht.ht_name)) {
        return;
    }

    auto& dls = lnav_data.ld_db_row_source;
    auto& dos = lnav_data.ld_db_overlay;
    auto& db_tc = lnav_data.ld_views[LNV_DB];

    for (const auto& ex : ht.ht_example) {
        std::string alt_msg;
        attr_line_t result;

        if (!ex.he_cmd) {
            continue;
        }

        if (EXAMPLE_RESULTS.count(ex.he_cmd)) {
            continue;
        }

        switch (ht.ht_context) {
            case help_context_t::HC_SQL_KEYWORD:
            case help_context_t::HC_SQL_INFIX:
            case help_context_t::HC_SQL_FUNCTION:
            case help_context_t::HC_SQL_TABLE_VALUED_FUNCTION:
            case help_context_t::HC_PRQL_TRANSFORM:
            case help_context_t::HC_PRQL_FUNCTION: {
                exec_context ec;

                ec.ec_label_source_stack.push_back(&dls);

                auto exec_res = execute_sql(ec, ex.he_cmd, alt_msg);

                if (exec_res.isErr()) {
                    auto um = exec_res.unwrapErr();
                    result.append(um.to_attr_line());
                } else if (dls.dls_rows.size() == 1
                           && dls.dls_rows[0].size() == 1)
                {
                    result.append(dls.dls_rows[0][0]);
                } else {
                    attr_line_t al;
                    dos.list_static_overlay(db_tc, 0, 1, al);
                    result.append(al);
                    for (int lpc = 0; lpc < (int) dls.text_line_count(); lpc++)
                    {
                        al.clear();
                        dls.text_value_for_line(
                            db_tc, lpc, al.get_string(), false);
                        dls.text_attrs_for_line(db_tc, lpc, al.get_attrs());
                        std::replace(al.get_string().begin(),
                                     al.get_string().end(),
                                     '\n',
                                     ' ');
                        result.append("\n").append(al);
                    }
                }

                EXAMPLE_RESULTS[ex.he_cmd] = result;

                log_trace("example: %s", ex.he_cmd);
                log_trace("example result: %s", result.get_string().c_str());
                break;
            }
            default:
                log_warning("Not executing example: %s", ex.he_cmd);
                break;
        }
    }
}

void
execute_examples()
{
    static const auto* sql_cmd_map
        = injector::get<readline_context::command_map_t*, sql_cmd_map_tag>();

    auto& dls = lnav_data.ld_db_row_source;

    auto old_width = dls.dls_max_column_width;
    dls.dls_max_column_width = 15;
    for (auto help_pair : sqlite_function_help) {
        execute_example(*help_pair.second);
    }
    for (auto help_pair : lnav::sql::prql_functions) {
        if (help_pair.second->ht_context != help_context_t::HC_PRQL_FUNCTION) {
            continue;
        }
        execute_example(*help_pair.second);
    }
    for (auto cmd_pair : *sql_cmd_map) {
        if (cmd_pair.second->c_help.ht_context
                != help_context_t::HC_PRQL_TRANSFORM
            && cmd_pair.second->c_help.ht_context
                != help_context_t::HC_PRQL_FUNCTION)
        {
            continue;
        }
        execute_example(cmd_pair.second->c_help);
    }
    dls.dls_max_column_width = old_width;

    dls.clear();
}

attr_line_t
eval_example(const help_text& ht, const help_example& ex)
{
    auto iter = EXAMPLE_RESULTS.find(ex.he_cmd);

    if (iter != EXAMPLE_RESULTS.end()) {
        return iter->second;
    }

    return "";
}

bool
toggle_view(textview_curses* toggle_tc)
{
    textview_curses* tc = lnav_data.ld_view_stack.top().value_or(nullptr);
    bool retval = false;

    require(toggle_tc != nullptr);
    require(toggle_tc >= &lnav_data.ld_views[0]);
    require(toggle_tc < &lnav_data.ld_views[LNV__MAX]);

    lnav_data.ld_preview_view[0].set_sub_source(
        &lnav_data.ld_preview_source[0]);
    lnav_data.ld_preview_source[0].clear();
    lnav_data.ld_preview_status_source[0].get_description().clear();
    lnav_data.ld_preview_view[1].set_sub_source(nullptr);
    lnav_data.ld_preview_status_source[1].get_description().clear();

    if (tc == toggle_tc) {
        if (lnav_data.ld_view_stack.size() == 1) {
            return false;
        }
        lnav_data.ld_last_view = tc;
        lnav_data.ld_view_stack.pop_back();
        lnav_data.ld_view_stack.top() | [](auto* tc) {
            // XXX
            if (tc == &lnav_data.ld_views[LNV_TIMELINE]) {
                auto tss = tc->get_sub_source();
                tss->text_filters_changed();
                tc->reload_data();
            }
        };
    } else {
        if (toggle_tc == &lnav_data.ld_views[LNV_LOG]
            || toggle_tc == &lnav_data.ld_views[LNV_TEXT])
        {
            rescan_files(true);
            rebuild_indexes_repeatedly();
        } else if (toggle_tc == &lnav_data.ld_views[LNV_SCHEMA]) {
            open_schema_view();
        } else if (toggle_tc == &lnav_data.ld_views[LNV_PRETTY]) {
            open_pretty_view();
        } else if (toggle_tc == &lnav_data.ld_views[LNV_TIMELINE]) {
            open_timeline_view();
        } else if (toggle_tc == &lnav_data.ld_views[LNV_HISTOGRAM]) {
            // Rebuild to reflect changes in marks.
            rebuild_hist();
        } else if (toggle_tc == &lnav_data.ld_views[LNV_HELP]) {
            build_all_help_text();
            if (lnav_data.ld_rl_view != nullptr) {
                lnav_data.ld_rl_view->set_alt_value(
                    HELP_MSG_1(q, "to return to the previous view"));
            }
        }
        lnav_data.ld_last_view = nullptr;
        lnav_data.ld_view_stack.push_back(toggle_tc);
        retval = true;
    }

    return retval;
}

/**
 * Ensure that the view is on the top of the view stack.
 *
 * @param expected_tc The text view that should be on top.
 * @return True if the view was already on the top of the stack.
 */
bool
ensure_view(textview_curses* expected_tc)
{
    auto* tc = lnav_data.ld_view_stack.top().value_or(nullptr);
    bool retval = true;

    if (tc != expected_tc) {
        toggle_view(expected_tc);
        retval = false;
    }
    return retval;
}

bool
ensure_view(lnav_view_t expected)
{
    require(expected >= 0);
    require(expected < LNV__MAX);

    return ensure_view(&lnav_data.ld_views[expected]);
}

std::optional<vis_line_t>
next_cluster(std::optional<vis_line_t> (bookmark_vector<vis_line_t>::*f)(
                 vis_line_t) const,
             const bookmark_type_t* bt,
             const vis_line_t top)
{
    auto* tc = get_textview_for_mode(lnav_data.ld_mode);
    auto& bm = tc->get_bookmarks();
    auto& bv = bm[bt];
    bool top_is_marked = binary_search(bv.begin(), bv.end(), top);
    vis_line_t last_top(top), tc_height;
    std::optional<vis_line_t> new_top = top;
    unsigned long tc_width;
    int hit_count = 0;

    tc->get_dimensions(tc_height, tc_width);

    while ((new_top = (bv.*f)(new_top.value()))) {
        int diff = new_top.value() - last_top;

        hit_count += 1;
        if (tc->is_selectable() || !top_is_marked || diff > 1) {
            return new_top;
        }
        if (hit_count > 1 && std::abs(new_top.value() - top) >= tc_height) {
            return vis_line_t(new_top.value() - diff);
        }
        if (diff < -1) {
            last_top = new_top.value();
            while ((new_top = (bv.*f)(new_top.value()))) {
                if ((std::abs(last_top - new_top.value()) > 1)
                    || (hit_count > 1
                        && (std::abs(top - new_top.value()) >= tc_height)))
                {
                    break;
                }
                last_top = new_top.value();
            }
            return last_top;
        }
        last_top = new_top.value();
    }

    if (last_top != top) {
        return last_top;
    }

    return std::nullopt;
}

bool
moveto_cluster(std::optional<vis_line_t> (bookmark_vector<vis_line_t>::*f)(
                   vis_line_t) const,
               const bookmark_type_t* bt,
               vis_line_t top)
{
    textview_curses* tc = get_textview_for_mode(lnav_data.ld_mode);
    auto new_top = next_cluster(f, bt, top);

    if (!new_top) {
        new_top = next_cluster(f, bt, tc->get_selection());
    }
    if (new_top != -1) {
        tc->get_sub_source()->get_location_history() |
            [new_top](auto lh) { lh->loc_history_append(new_top.value()); };

        if (tc->is_selectable()) {
            tc->set_selection(new_top.value());
        } else {
            tc->set_top(new_top.value());
        }
        return true;
    }

    alerter::singleton().chime("unable to find next bookmark");

    return false;
}

vis_line_t
search_forward_from(textview_curses* tc)
{
    vis_line_t height, retval = tc->get_selection();

    if (!tc->is_selectable()) {
        auto& krh = lnav_data.ld_key_repeat_history;
        unsigned long width;

        tc->get_dimensions(height, width);

        if (krh.krh_count > 1 && retval > (krh.krh_start_line + (1.5 * height)))
        {
            retval += vis_line_t(0.90 * height);
        }
    }

    return retval;
}

textview_curses*
get_textview_for_mode(ln_mode_t mode)
{
    switch (mode) {
        case ln_mode_t::SEARCH_FILTERS:
        case ln_mode_t::FILTER:
            return &lnav_data.ld_filter_view;
        case ln_mode_t::SEARCH_FILES:
        case ln_mode_t::FILES:
            return &lnav_data.ld_files_view;
        case ln_mode_t::FILE_DETAILS:
            return &lnav_data.ld_file_details_view;
        case ln_mode_t::SPECTRO_DETAILS:
        case ln_mode_t::SEARCH_SPECTRO_DETAILS:
            return &lnav_data.ld_spectro_details_view;
        default:
            return *lnav_data.ld_view_stack.top();
    }
}

hist_index_delegate::
hist_index_delegate(hist_source2& hs, textview_curses& tc)
    : hid_source(hs), hid_view(tc)
{
}

void
hist_index_delegate::index_start(logfile_sub_source& lss)
{
    this->hid_source.clear();
}

void
hist_index_delegate::index_line(logfile_sub_source& lss,
                                logfile* lf,
                                logfile::iterator ll)
{
    if (ll->is_continued() || ll->get_time() == 0) {
        return;
    }

    hist_source2::hist_type_t ht;

    switch (ll->get_msg_level()) {
        case LEVEL_FATAL:
        case LEVEL_CRITICAL:
        case LEVEL_ERROR:
            ht = hist_source2::HT_ERROR;
            break;
        case LEVEL_WARNING:
            ht = hist_source2::HT_WARNING;
            break;
        default:
            ht = hist_source2::HT_NORMAL;
            break;
    }

    this->hid_source.add_value(ll->get_time(), ht);
    if (ll->is_marked() || ll->is_expr_marked()) {
        this->hid_source.add_value(ll->get_time(), hist_source2::HT_MARK);
    }
}

void
hist_index_delegate::index_complete(logfile_sub_source& lss)
{
    this->hid_view.reload_data();
    lnav_data.ld_views[LNV_SPECTRO].reload_data();
}

static std::vector<breadcrumb::possibility>
view_title_poss()
{
    std::vector<breadcrumb::possibility> retval;

    for (int view_index = 0; view_index < LNV__MAX; view_index++) {
        attr_line_t display_value{lnav_view_titles[view_index]};
        std::optional<size_t> quantity;
        std::string units;

        switch (view_index) {
            case LNV_LOG:
                quantity = lnav_data.ld_log_source.file_count();
                units = "file";
                break;
            case LNV_TEXT:
                quantity = lnav_data.ld_text_source.size();
                units = "file";
                break;
            case LNV_DB:
                quantity = lnav_data.ld_db_row_source.dls_rows.size();
                units = "row";
                break;
        }

        if (quantity) {
            display_value.pad_to(8)
                .append(" (")
                .append(lnav::roles::number(
                    quantity.value() == 0 ? "no"
                                          : fmt::to_string(quantity.value())))
                .appendf(FMT_STRING(" {}{})"),
                         units,
                         quantity.value() == 1 ? "" : "s");
        }
        retval.emplace_back(lnav_view_titles[view_index], display_value);
    }
    return retval;
}

static void
view_performer(const breadcrumb::crumb::key_t& view_name)
{
    auto* view_title_iter = std::find_if(
        std::begin(lnav_view_titles),
        std::end(lnav_view_titles),
        [&](const char* v) {
            return strcasecmp(v, view_name.get<std::string>().c_str()) == 0;
        });

    if (view_title_iter != std::end(lnav_view_titles)) {
        ensure_view(lnav_view_t(view_title_iter - lnav_view_titles));
    }
}

std::vector<breadcrumb::crumb>
lnav_crumb_source()
{
    std::vector<breadcrumb::crumb> retval;

    auto top_view_opt = lnav_data.ld_view_stack.top();
    if (!top_view_opt) {
        return retval;
    }

    auto* top_view = top_view_opt.value();
    auto view_index = top_view - lnav_data.ld_views;
    retval.emplace_back(
        lnav_view_titles[view_index],
        attr_line_t().append(lnav::roles::status_title(
            fmt::format(FMT_STRING(" {} "), lnav_view_titles[view_index]))),
        view_title_poss,
        view_performer);

    auto* tss = top_view->get_sub_source();
    if (tss != nullptr) {
        tss->text_crumbs_for_line(top_view->get_selection(), retval);
    }

    return retval;
}

void
clear_preview()
{
    for (size_t lpc = 0; lpc < 2; lpc++) {
        lnav_data.ld_preview_source[lpc].clear();
        lnav_data.ld_preview_status_source[lpc]
            .get_description()
            .set_cylon(false)
            .clear();
        lnav_data.ld_db_preview_source[lpc].clear();
        lnav_data.ld_preview_view[lpc].set_sub_source(nullptr);
        lnav_data.ld_preview_view[lpc].set_overlay_source(nullptr);
    }
}

void
set_view_mode(ln_mode_t mode)
{
    if (mode == lnav_data.ld_mode) {
        return;
    }

    static auto* breadcrumb_view = injector::get<breadcrumb_curses*>();

    switch (lnav_data.ld_mode) {
        case ln_mode_t::BREADCRUMBS: {
            breadcrumb_view->blur();
            lnav_data.ld_view_stack.set_needs_update();
            break;
        }
        case ln_mode_t::FILE_DETAILS: {
            lnav_data.ld_file_details_view.tc_cursor_role
                = role_t::VCR_DISABLED_CURSOR_LINE;
            break;
        }
        default:
            break;
    }
    switch (mode) {
        case ln_mode_t::BREADCRUMBS: {
            breadcrumb_view->focus();
            break;
        }
        case ln_mode_t::FILE_DETAILS: {
            lnav_data.ld_status[LNS_FILTER].set_needs_update();
            lnav_data.ld_file_details_view.tc_cursor_role
                = role_t::VCR_CURSOR_LINE;
            break;
        }
        default:
            break;
    }
    lnav_data.ld_mode = mode;
}

static std::vector<view_curses*>
all_views()
{
    static auto* breadcrumb_view = injector::get<breadcrumb_curses*>();

    std::vector<view_curses*> retval;

    retval.push_back(breadcrumb_view);
    for (auto& sc : lnav_data.ld_status) {
        retval.push_back(&sc);
    }
    retval.push_back(&lnav_data.ld_doc_view);
    retval.push_back(&lnav_data.ld_example_view);
    retval.push_back(&lnav_data.ld_preview_view[0]);
    retval.push_back(&lnav_data.ld_preview_view[1]);
    retval.push_back(&lnav_data.ld_file_details_view);
    retval.push_back(&lnav_data.ld_files_view);
    retval.push_back(&lnav_data.ld_filter_view);
    retval.push_back(&lnav_data.ld_user_message_view);
    retval.push_back(&lnav_data.ld_spectro_details_view);
    retval.push_back(&lnav_data.ld_timeline_details_view);
    retval.push_back(lnav_data.ld_rl_view);

    return retval;
}

void
lnav_behavior::mouse_event(int button, bool release, int x, int y)
{
    static auto* breadcrumb_view = injector::get<breadcrumb_curses*>();
    static const auto VIEWS = all_views();
    static const auto CLICK_INTERVAL
        = std::chrono::milliseconds(mouseinterval(-1) * 2);

    struct mouse_event me;

    switch (button & xterm_mouse::XT_BUTTON__MASK) {
        case xterm_mouse::XT_BUTTON1:
            me.me_button = mouse_button_t::BUTTON_LEFT;
            break;
        case xterm_mouse::XT_BUTTON2:
            me.me_button = mouse_button_t::BUTTON_MIDDLE;
            break;
        case xterm_mouse::XT_BUTTON3:
            me.me_button = mouse_button_t::BUTTON_RIGHT;
            break;
        case xterm_mouse::XT_SCROLL_UP:
            me.me_button = mouse_button_t::BUTTON_SCROLL_UP;
            break;
        case xterm_mouse::XT_SCROLL_DOWN:
            me.me_button = mouse_button_t::BUTTON_SCROLL_DOWN;
            break;
    }

    gettimeofday(&me.me_time, nullptr);
    me.me_modifiers = button & xterm_mouse::XT_MODIFIER_MASK;

    if (release
        && (to_mstime(me.me_time)
            - to_mstime(this->lb_last_release_event.me_time))
            < CLICK_INTERVAL.count())
    {
        me.me_state = mouse_button_state_t::BUTTON_STATE_DOUBLE_CLICK;
    } else if (button & xterm_mouse::XT_DRAG_FLAG) {
        me.me_state = mouse_button_state_t::BUTTON_STATE_DRAGGED;
    } else if (release) {
        me.me_state = mouse_button_state_t::BUTTON_STATE_RELEASED;
    } else {
        me.me_state = mouse_button_state_t::BUTTON_STATE_PRESSED;
    }

    auto width = getmaxx(lnav_data.ld_window);

    me.me_press_x = this->lb_last_event.me_press_x;
    me.me_press_y = this->lb_last_event.me_press_y;
    me.me_x = x - 1;
    if (me.me_x >= width) {
        me.me_x = width - 1;
    }
    me.me_y = y - 1;

    switch (me.me_state) {
        case mouse_button_state_t::BUTTON_STATE_PRESSED:
        case mouse_button_state_t::BUTTON_STATE_DOUBLE_CLICK: {
            if (lnav_data.ld_mode == ln_mode_t::BREADCRUMBS) {
                if (breadcrumb_view->contains(me.me_x, me.me_y)) {
                    this->lb_last_view = breadcrumb_view;
                    break;
                } else {
                    set_view_mode(ln_mode_t::PAGING);
                    lnav_data.ld_view_stack.set_needs_update();
                }
            }

            auto* tc = *(lnav_data.ld_view_stack.top());
            if (tc->contains(me.me_x, me.me_y)) {
                me.me_press_y = me.me_y - tc->get_y();
                me.me_press_x = me.me_x - tc->get_x();
                this->lb_last_view = tc;

                switch (lnav_data.ld_mode) {
                    case ln_mode_t::PAGING:
                        break;
                    case ln_mode_t::FILES:
                    case ln_mode_t::FILE_DETAILS:
                    case ln_mode_t::FILTER:
                        // Clicking on the main view when the config panels are
                        // open should return us to paging.
                        set_view_mode(ln_mode_t::PAGING);
                        break;
                    default:
                        break;
                }
            } else {
                for (auto* vc : VIEWS) {
                    if (vc->contains(me.me_x, me.me_y)) {
                        this->lb_last_view = vc;
                        me.me_press_y = me.me_y - vc->get_y();
                        me.me_press_x = me.me_x - vc->get_x();
                        break;
                    }
                }
            }
            break;
        }
        case mouse_button_state_t::BUTTON_STATE_DRAGGED: {
            break;
        }
        case mouse_button_state_t::BUTTON_STATE_RELEASED: {
            this->lb_last_release_event = me;
            break;
        }
    }

    if (this->lb_last_view != nullptr) {
        me.me_y -= this->lb_last_view->get_y();
        me.me_x -= this->lb_last_view->get_x();
        this->lb_last_view->handle_mouse(me);
    }
    this->lb_last_event = me;
    if (me.me_state == mouse_button_state_t::BUTTON_STATE_RELEASED
        || me.me_state == mouse_button_state_t::BUTTON_STATE_DOUBLE_CLICK
        || me.me_button == mouse_button_t::BUTTON_SCROLL_UP
        || me.me_button == mouse_button_t::BUTTON_SCROLL_DOWN)
    {
        this->lb_last_view = nullptr;
    }
}
