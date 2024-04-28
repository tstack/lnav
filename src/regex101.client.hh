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

#ifndef regex101_client_hh
#define regex101_client_hh

#include <string>
#include <vector>

#include "base/lnav.console.hh"
#include "base/result.h"
#include "mapbox/variant.hpp"

namespace regex101 {
namespace client {

struct unit_test {
    enum class criteria {
        DOES_MATCH,
        DOES_NOT_MATCH,
    };

    std::string ut_description;
    std::string ut_test_string;
    std::string ut_target{"REGEX"};
    criteria ut_criteria{criteria::DOES_MATCH};

    bool operator==(const unit_test& rhs) const;
    bool operator!=(const unit_test& rhs) const;
};

struct entry {
    std::string e_date_created;
    std::string e_regex;
    std::string e_test_string;
    std::string e_flags{"gs"};
    std::string e_delimiter{"/"};
    std::string e_flavor{"pcre"};
    std::vector<unit_test> e_unit_tests;
    std::optional<std::string> e_permalink_fragment;

    bool operator==(const entry& rhs) const;
    bool operator!=(const entry& rhs) const;
};

struct upsert_response {
    std::string cr_delete_code;
    std::string cr_permalink_fragment;
    int32_t cr_version;
};

Result<upsert_response, lnav::console::user_message> upsert(entry& en);

struct no_entry {};

using retrieve_result_t
    = mapbox::util::variant<entry, no_entry, lnav::console::user_message>;

retrieve_result_t retrieve(const std::string& permalink);

Result<void, lnav::console::user_message> delete_entry(
    const std::string& delete_code);

std::string to_edit_url(const std::string& permalink);

}  // namespace client
}  // namespace regex101

#endif
