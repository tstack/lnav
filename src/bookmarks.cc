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
 * @file bookmarks.cc
 */

#include "bookmarks.hh"

#include "base/itertools.hh"
#include "bookmarks.json.hh"
#include "config.h"

std::unordered_set<std::string> bookmark_metadata::KNOWN_TAGS;

typed_json_path_container<logmsg_annotations> logmsg_annotations_handlers = {
    yajlpp::pattern_property_handler("(?<annotation_id>.*)")
        .for_field(&logmsg_annotations::la_pairs),
};

void
bookmark_metadata::add_tag(const std::string& tag)
{
    if (!(this->bm_tags | lnav::itertools::find(tag))) {
        this->bm_tags.emplace_back(tag);
    }
}

bool
bookmark_metadata::remove_tag(const std::string& tag)
{
    auto iter = std::find(this->bm_tags.begin(), this->bm_tags.end(), tag);
    bool retval = false;

    if (iter != this->bm_tags.end()) {
        this->bm_tags.erase(iter);
        retval = true;
    }
    return retval;
}

bool
bookmark_metadata::empty(bookmark_metadata::categories props) const
{
    switch (props) {
        case categories::any:
            return this->bm_name.empty() && this->bm_opid.empty()
                && this->bm_comment.empty() && this->bm_tags.empty()
                && this->bm_annotations.la_pairs.empty();
        case categories::partition:
            return this->bm_name.empty();
        case categories::notes:
            return this->bm_comment.empty() && this->bm_tags.empty()
                && this->bm_annotations.la_pairs.empty();
        case categories::opid:
            return this->bm_opid.empty();
    }
}

void
bookmark_metadata::clear()
{
    this->bm_opid.clear();
    this->bm_comment.clear();
    this->bm_tags.clear();
    this->bm_annotations.la_pairs.clear();
}

std::optional<bookmark_type_t*>
bookmark_type_t::find_type(const std::string& name)
{
    return get_all_types()
        | lnav::itertools::find_if(
               [&name](const auto& elem) { return elem->bt_name == name; })
        | lnav::itertools::deref();
}

std::vector<bookmark_type_t*>&
bookmark_type_t::get_all_types()
{
    static std::vector<bookmark_type_t*> all_types;

    return all_types;
}

std::vector<const char*>
bookmark_type_t::get_type_names()
{
    std::vector<const char*> retval;

    for (const auto& bt : get_all_types()) {
        retval.emplace_back(bt->get_name().data());
    }
    std::stable_sort(retval.begin(), retval.end(),
        [](const char* lhs, const char* rhs) {
            return strcmp(lhs, rhs) < 0;
        });
    return retval;
}
