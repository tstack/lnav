/**
 * Copyright (c) 2022, Timothy Stack
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

#include "plain_text_source.hh"

#include "base/itertools.hh"
#include "config.h"
#include "scn/scn.h"

static std::vector<plain_text_source::text_line>
to_text_line(const std::vector<attr_line_t>& lines)
{
    file_off_t off = 0;

    return lines | lnav::itertools::map([&off](const auto& elem) {
               auto retval = plain_text_source::text_line{
                   off,
                   elem,
               };

               off += elem.length() + 1;
               return retval;
           });
}

plain_text_source::plain_text_source(const std::string& text)
{
    size_t start = 0, end;

    while ((end = text.find('\n', start)) != std::string::npos) {
        size_t len = (end - start);
        this->tds_lines.emplace_back(start, text.substr(start, len));
        start = end + 1;
    }
    if (start < text.length()) {
        this->tds_lines.emplace_back(start, text.substr(start));
    }
    this->tds_longest_line = this->compute_longest_line();
}

plain_text_source::plain_text_source(const std::vector<std::string>& text_lines)
{
    this->replace_with(text_lines);
}

plain_text_source::plain_text_source(const std::vector<attr_line_t>& text_lines)
    : tds_lines(to_text_line(text_lines))
{
    this->tds_longest_line = this->compute_longest_line();
}

plain_text_source&
plain_text_source::replace_with(const attr_line_t& text_lines)
{
    this->tds_lines.clear();
    this->tds_doc_sections = lnav::document::discover_metadata(text_lines);
    file_off_t off = 0;
    auto lines = text_lines.split_lines();
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    for (auto& line : lines) {
        auto line_len = line.length() + 1;
        this->tds_lines.emplace_back(off, std::move(line));
        off += line_len;
    }
    this->tds_longest_line = this->compute_longest_line();
    if (this->tss_view != nullptr) {
        this->tss_view->set_needs_update();
    }
    return *this;
}

plain_text_source&
plain_text_source::replace_with_mutable(attr_line_t& text_lines,
                                        text_format_t tf)
{
    this->tds_lines.clear();
    this->tds_doc_sections
        = lnav::document::discover_structure(text_lines, line_range{0, -1}, tf);
    file_off_t off = 0;
    auto lines = text_lines.split_lines();
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    for (auto& line : lines) {
        auto line_len = line.length() + 1;
        this->tds_lines.emplace_back(off, std::move(line));
        off += line_len;
    }
    this->tds_longest_line = this->compute_longest_line();
    if (this->tss_view != nullptr) {
        this->tss_view->set_needs_update();
    }
    return *this;
}

plain_text_source&
plain_text_source::replace_with(const std::vector<std::string>& text_lines)
{
    file_off_t off = 0;
    for (const auto& str : text_lines) {
        this->tds_lines.emplace_back(off, str);
        off += str.length() + 1;
    }
    this->tds_longest_line = this->compute_longest_line();
    if (this->tss_view != nullptr) {
        this->tss_view->set_needs_update();
    }
    return *this;
}

plain_text_source&
plain_text_source::replace_with(const std::vector<attr_line_t>& text_lines)
{
    file_off_t off = 0;
    for (const auto& al : text_lines) {
        this->tds_lines.emplace_back(off, al);
        off += al.length() + 1;
    }
    this->tds_longest_line = this->compute_longest_line();
    if (this->tss_view != nullptr) {
        this->tss_view->set_needs_update();
    }
    return *this;
}

void
plain_text_source::clear()
{
    this->tds_lines.clear();
    this->tds_longest_line = 0;
    this->tds_text_format = text_format_t::TF_UNKNOWN;
    if (this->tss_view != nullptr) {
        this->tss_view->set_needs_update();
    }
}

plain_text_source&
plain_text_source::truncate_to(size_t max_lines)
{
    while (this->tds_lines.size() > max_lines) {
        this->tds_lines.pop_back();
    }
    if (this->tss_view != nullptr) {
        this->tss_view->set_needs_update();
    }
    return *this;
}

size_t
plain_text_source::text_line_width(textview_curses& curses)
{
    return this->tds_longest_line;
}

void
plain_text_source::text_value_for_line(textview_curses& tc,
                                       int row,
                                       std::string& value_out,
                                       text_sub_source::line_flags_t flags)
{
    value_out = this->tds_lines[row].tl_value.get_string();
    this->tds_line_indent_size = 0;
    for (const auto& ch : value_out) {
        if (ch == ' ') {
            this->tds_line_indent_size += 1;
        } else if (ch == '\t') {
            do {
                this->tds_line_indent_size += 1;
            } while (this->tds_line_indent_size % 8);
        } else {
            break;
        }
    }
}

void
plain_text_source::text_attrs_for_line(textview_curses& tc,
                                       int line,
                                       string_attrs_t& value_out)
{
    value_out = this->tds_lines[line].tl_value.get_attrs();
    if (this->tds_reverse_selection && tc.is_selectable()
        && tc.get_selection() == line)
    {
        value_out.emplace_back(line_range{0, -1},
                               VC_STYLE.value(text_attrs{A_REVERSE}));
    }
    for (const auto& indent : this->tds_doc_sections.m_indents) {
        if (indent < this->tds_line_indent_size) {
            auto guide_lr = line_range{
                (int) indent,
                (int) (indent + 1),
                line_range::unit::codepoint,
            };
            value_out.emplace_back(guide_lr,
                                   VC_BLOCK_ELEM.value(block_elem_t{
                                       L'\u258f', role_t::VCR_INDENT_GUIDE}));
        }
    }
}

size_t
plain_text_source::text_size_for_line(textview_curses& tc,
                                      int row,
                                      text_sub_source::line_flags_t flags)
{
    return this->tds_lines[row].tl_value.length();
}

text_format_t
plain_text_source::get_text_format() const
{
    return this->tds_text_format;
}

size_t
plain_text_source::compute_longest_line()
{
    size_t retval = 0;
    for (auto& iter : this->tds_lines) {
        retval = std::max(retval, (size_t) iter.tl_value.length());
    }
    return retval;
}

nonstd::optional<vis_line_t>
plain_text_source::line_for_offset(file_off_t off) const
{
    struct cmper {
        bool operator()(const file_off_t& lhs, const text_line& rhs)
        {
            return lhs < rhs.tl_offset;
        }

        bool operator()(const text_line& lhs, const file_off_t& rhs)
        {
            return lhs.tl_offset < rhs;
        }
    };

    if (this->tds_lines.empty()) {
        return nonstd::nullopt;
    }

    auto iter = std::lower_bound(
        this->tds_lines.begin(), this->tds_lines.end(), off, cmper{});
    if (iter == this->tds_lines.end()) {
        if (this->tds_lines.back().contains_offset(off)) {
            return nonstd::make_optional(
                vis_line_t(std::distance(this->tds_lines.end() - 1, iter)));
        }
        return nonstd::nullopt;
    }

    if (!iter->contains_offset(off) && iter != this->tds_lines.begin()) {
        --iter;
    }

    return nonstd::make_optional(
        vis_line_t(std::distance(this->tds_lines.begin(), iter)));
}

void
plain_text_source::text_crumbs_for_line(int line,
                                        std::vector<breadcrumb::crumb>& crumbs)
{
    const auto initial_size = crumbs.size();
    const auto& tl = this->tds_lines[line];

    this->tds_doc_sections.m_sections_tree.visit_overlapping(
        tl.tl_offset,
        [&crumbs, initial_size, meta = &this->tds_doc_sections, this](
            const auto& iv) {
            auto path = crumbs | lnav::itertools::skip(initial_size)
                | lnav::itertools::map(&breadcrumb::crumb::c_key)
                | lnav::itertools::append(iv.value);
            crumbs.template emplace_back(
                iv.value,
                [meta, path]() { return meta->possibility_provider(path); },
                [this, meta, path](const auto& key) {
                    auto curr_node = lnav::document::hier_node::lookup_path(
                        meta->m_sections_root.get(), path);
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
                            this->line_for_offset(sib_iter->second->hn_start) |
                                [this](const auto new_top) {
                                    this->tss_view->set_selection(new_top);
                                };
                        },
                        [this, parent_node](size_t index) {
                            if (index >= parent_node->hn_children.size()) {
                                return;
                            }
                            auto sib = parent_node->hn_children[index].get();
                            this->line_for_offset(sib->hn_start) |
                                [this](const auto new_top) {
                                    this->tss_view->set_selection(new_top);
                                };
                        });
                });
        });

    auto path = crumbs | lnav::itertools::skip(initial_size)
        | lnav::itertools::map(&breadcrumb::crumb::c_key);
    auto node = lnav::document::hier_node::lookup_path(
        this->tds_doc_sections.m_sections_root.get(), path);

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
                    auto child_iter = curr_node->hn_named_children.find(str);
                    if (child_iter != curr_node->hn_named_children.end()) {
                        this->line_for_offset(child_iter->second->hn_start) |
                            [this](const auto new_top) {
                                this->tss_view->set_selection(new_top);
                            };
                    }
                },
                [this, curr_node](size_t index) {
                    auto* child = curr_node->hn_children[index].get();
                    this->line_for_offset(child->hn_start) |
                        [this](const auto new_top) {
                            this->tss_view->set_selection(new_top);
                        };
                });
        };
        crumbs.emplace_back(
            "", "\u22ef", std::move(poss_provider), std::move(path_performer));
        crumbs.back().c_expected_input = node.value()->hn_named_children.empty()
            ? breadcrumb::crumb::expected_input_t::index
            : breadcrumb::crumb::expected_input_t::index_or_exact;
    }
}

nonstd::optional<vis_line_t>
plain_text_source::row_for_anchor(const std::string& id)
{
    nonstd::optional<vis_line_t> retval;

    if (this->tds_doc_sections.m_sections_root == nullptr) {
        return retval;
    }

    const auto& meta = this->tds_doc_sections;

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
            retval = this->line_for_offset(lookup_res.value()->hn_start);
        }

        return retval;
    }

    lnav::document::hier_node::depth_first(
        meta.m_sections_root.get(),
        [this, &id, &retval](const lnav::document::hier_node* node) {
            for (const auto& child_pair : node->hn_named_children) {
                const auto& child_anchor
                    = text_anchors::to_anchor_string(child_pair.first);

                if (child_anchor != id) {
                    continue;
                }

                retval = this->line_for_offset(child_pair.second->hn_start);
                break;
            }
        });

    return retval;
}

std::unordered_set<std::string>
plain_text_source::get_anchors()
{
    std::unordered_set<std::string> retval;

    lnav::document::hier_node::depth_first(
        this->tds_doc_sections.m_sections_root.get(),
        [&retval](const lnav::document::hier_node* node) {
            for (const auto& child_pair : node->hn_named_children) {
                retval.emplace(
                    text_anchors::to_anchor_string(child_pair.first));
            }
        });

    return retval;
}

nonstd::optional<std::string>
plain_text_source::anchor_for_row(vis_line_t vl)
{
    nonstd::optional<std::string> retval;

    if (vl > this->tds_lines.size()
        || this->tds_doc_sections.m_sections_root == nullptr)
    {
        return retval;
    }

    const auto& tl = this->tds_lines[vl];
    auto& md = this->tds_doc_sections;
    auto path_for_line = md.path_for_range(
        tl.tl_offset, tl.tl_offset + tl.tl_value.al_string.length());

    if (path_for_line.empty()) {
        return nonstd::nullopt;
    }

    if ((path_for_line.size() == 1
         || this->tds_text_format == text_format_t::TF_MARKDOWN)
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

nonstd::optional<vis_line_t>
plain_text_source::adjacent_anchor(vis_line_t vl, text_anchors::direction dir)
{
    if (vl > this->tds_lines.size()
        || this->tds_doc_sections.m_sections_root == nullptr)
    {
        return nonstd::nullopt;
    }

    const auto& tl = this->tds_lines[vl];
    auto path_for_line = this->tds_doc_sections.path_for_range(
        tl.tl_offset, tl.tl_offset + tl.tl_value.al_string.length());

    auto& md = this->tds_doc_sections;
    if (path_for_line.empty()) {
        auto neighbors_res = md.m_sections_root->line_neighbors(vl);
        if (!neighbors_res) {
            return nonstd::nullopt;
        }

        switch (dir) {
            case text_anchors::direction::prev: {
                if (neighbors_res->cnr_previous) {
                    return this->line_for_offset(
                        neighbors_res->cnr_previous.value()->hn_start);
                }
                break;
            }
            case text_anchors::direction::next: {
                if (neighbors_res->cnr_next) {
                    return this->line_for_offset(
                        neighbors_res->cnr_next.value()->hn_start);
                } else if (!md.m_sections_root->hn_children.empty()) {
                    return this->line_for_offset(
                        md.m_sections_root->hn_children[0]->hn_start);
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
        child_hn.value(), tl.tl_offset + tl.tl_value.al_string.length() + 1);
    if (!neighbors_res) {
        return nonstd::nullopt;
    }

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
                return this->line_for_offset(
                    neighbors_res->cnr_previous.value()->hn_start);
            }
            break;
        }
        case text_anchors::direction::next: {
            if (neighbors_res->cnr_next) {
                return this->line_for_offset(
                    neighbors_res->cnr_next.value()->hn_start);
            }
            break;
        }
    }

    return nonstd::nullopt;
}
