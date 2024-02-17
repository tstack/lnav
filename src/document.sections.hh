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

#ifndef lnav_attr_line_breadcrumbs_hh
#define lnav_attr_line_breadcrumbs_hh

#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/attr_line.hh"
#include "base/file_range.hh"
#include "breadcrumb.hh"
#include "intervaltree/IntervalTree.h"
#include "mapbox/variant.hpp"
#include "optional.hpp"
#include "text_format.hh"

namespace lnav {
namespace document {

using section_key_t = mapbox::util::variant<std::string, size_t>;
using section_interval_t = interval_tree::Interval<file_off_t, section_key_t>;
using sections_tree_t = interval_tree::IntervalTree<file_off_t, section_key_t>;

enum class section_types_t {
    comment,
    multiline_string,
};

using section_type_interval_t
    = interval_tree::Interval<file_off_t, section_types_t>;
using section_types_tree_t
    = interval_tree::IntervalTree<file_off_t, section_types_t>;

struct hier_node {
    hier_node* hn_parent{nullptr};
    file_off_t hn_start{0};
    size_t hn_line_number{0};
    std::multimap<std::string, hier_node*> hn_named_children;
    std::vector<std::unique_ptr<hier_node>> hn_children;

    nonstd::optional<hier_node*> lookup_child(section_key_t key) const;

    nonstd::optional<size_t> child_index(const hier_node* hn) const;

    struct child_neighbors_result {
        nonstd::optional<const hier_node*> cnr_previous;
        nonstd::optional<const hier_node*> cnr_next;
    };

    nonstd::optional<child_neighbors_result> child_neighbors(
        const hier_node* hn, file_off_t offset) const;

    nonstd::optional<child_neighbors_result> line_neighbors(size_t ln) const;

    nonstd::optional<size_t> find_line_number(const std::string& str) const
    {
        auto iter = this->hn_named_children.find(str);
        if (iter != this->hn_named_children.end()) {
            return nonstd::make_optional(iter->second->hn_line_number);
        }

        return nonstd::nullopt;
    }

    nonstd::optional<size_t> find_line_number(size_t index) const
    {
        if (index < this->hn_children.size()) {
            return nonstd::make_optional(
                this->hn_children[index]->hn_line_number);
        }
        return nonstd::nullopt;
    }

    bool is_named_only() const
    {
        return this->hn_children.size() == this->hn_named_children.size();
    }

    static nonstd::optional<const hier_node*> lookup_path(
        const hier_node* root, const std::vector<section_key_t>& path);

    template<typename F>
    static void depth_first(hier_node* root, F func)
    {
        if (root == nullptr) {
            return;
        }

        for (auto& child : root->hn_children) {
            depth_first(child.get(), func);
        }
        func(root);
    }
};

struct metadata {
    sections_tree_t m_sections_tree;
    std::unique_ptr<hier_node> m_sections_root;
    section_types_tree_t m_section_types_tree;
    std::set<size_t> m_indents;
    text_format_t m_text_format{text_format_t::TF_UNKNOWN};

    std::vector<section_key_t> path_for_range(size_t start, size_t stop);

    std::vector<breadcrumb::possibility> possibility_provider(
        const std::vector<section_key_t>& path);
};

metadata discover_metadata(const attr_line_t& al);

metadata discover_structure(attr_line_t& al,
                            struct line_range lr,
                            text_format_t tf = text_format_t::TF_UNKNOWN);

}  // namespace document
}  // namespace lnav

#endif
