/**
 * Copyright (c) 2015, Timothy Stack
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

#ifndef pretty_printer_hh
#define pretty_printer_hh

#include <deque>
#include <map>
#include <sstream>
#include <stack>
#include <utility>
#include <vector>

#include <sys/types.h>

#include "base/attr_line.hh"
#include "base/file_range.hh"
#include "base/opt_util.hh"
#include "data_scanner.hh"
#include "intervaltree/IntervalTree.h"

class pretty_printer {
public:
    using key_t = mapbox::util::variant<std::string, size_t>;
    using pretty_interval = interval_tree::Interval<file_off_t, key_t>;
    using pretty_tree = interval_tree::IntervalTree<file_off_t, key_t>;

    struct element {
        element(data_token_t token, pcre_context& pc)
            : e_token(token), e_capture(*pc.all())
        {
        }

        element(data_token_t token, pcre_context::capture_t& cap)
            : e_token(token), e_capture(cap)
        {
        }

        data_token_t e_token;
        pcre_context::capture_t e_capture;
    };

    struct hier_node {
        hier_node* hn_parent{nullptr};
        file_off_t hn_start{0};
        std::multimap<std::string, hier_node*> hn_named_children;
        std::vector<std::unique_ptr<hier_node>> hn_children;

        nonstd::optional<hier_node*> lookup_child(key_t key) const;

        static nonstd::optional<const hier_node*> lookup_path(
            const hier_node* root, const std::vector<key_t>& path);

        template<typename F>
        static void depth_first(hier_node* root, F func)
        {
            for (auto& child : root->hn_children) {
                depth_first(child.get(), func);
            }
            func(root);
        }
    };

    pretty_printer(data_scanner* ds, string_attrs_t sa, int leading_indent = 0)
        : pp_leading_indent(leading_indent), pp_scanner(ds),
          pp_attrs(std::move(sa))
    {
        this->pp_body_lines.push(0);

        pcre_context_static<30> pc;
        data_token_t dt;

        this->pp_scanner->reset();
        while (this->pp_scanner->tokenize2(pc, dt)) {
            if (dt == DT_XML_CLOSE_TAG || dt == DT_XML_DECL_TAG) {
                pp_is_xml = true;
                break;
            }
        }

        this->pp_interval_state.resize(1);
        this->pp_hier_nodes.push_back(std::make_unique<hier_node>());
    }

    void append_to(attr_line_t& al);

    std::vector<pretty_interval> take_intervals()
    {
        return std::move(this->pp_intervals);
    }

    std::unique_ptr<hier_node> take_hier_root()
    {
        return std::move(this->pp_hier_stage);
    }

private:
    void descend();

    void ascend();

    void start_new_line();

    bool flush_values(bool start_on_depth = false);

    void append_indent();

    void write_element(const element& el);

    void append_child_node();

    struct interval_state {
        nonstd::optional<file_off_t> is_start;
        std::string is_name;
    };

    int pp_leading_indent;
    int pp_depth{0};
    int pp_line_length{0};
    int pp_soft_indent{0};
    std::stack<int> pp_body_lines{};
    data_scanner* pp_scanner;
    string_attrs_t pp_attrs;
    std::ostringstream pp_stream;
    std::deque<element> pp_values{};
    int pp_shift_accum{0};
    bool pp_is_xml{false};
    std::vector<interval_state> pp_interval_state;
    std::vector<pretty_interval> pp_intervals;
    std::vector<std::unique_ptr<hier_node>> pp_hier_nodes;
    std::unique_ptr<hier_node> pp_hier_stage;
};

#endif
