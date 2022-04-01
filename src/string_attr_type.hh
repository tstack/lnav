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

#ifndef lnav_string_attr_type_hh
#define lnav_string_attr_type_hh

#include <string>
#include <utility>

#include <stdint.h>

#include "base/intern_string.hh"
#include "mapbox/variant.hpp"

class logfile;
struct bookmark_metadata;

using string_attr_value = mapbox::util::variant<int64_t,
                                                const intern_string_t,
                                                std::string,
                                                std::shared_ptr<logfile>,
                                                bookmark_metadata*>;

class string_attr_type_base {
public:
    explicit string_attr_type_base(const char* name) noexcept : sat_name(name)
    {
    }

    const char* const sat_name;
};

template<typename T>
class string_attr_type : public string_attr_type_base {
public:
    using value_type = T;

    explicit string_attr_type(const char* name) noexcept
        : string_attr_type_base(name)
    {
    }

    template<typename U = T>
    std::enable_if_t<!std::is_void<U>::value,
                     std::pair<const string_attr_type_base*, string_attr_value>>
    value(const U& val) const
    {
        return std::make_pair(this, val);
    }

    template<typename U = T>
    std::enable_if_t<std::is_void<U>::value,
                     std::pair<const string_attr_type_base*, string_attr_value>>
    value() const
    {
        return std::make_pair(this, string_attr_value{});
    }
};

extern string_attr_type<void> SA_ORIGINAL_LINE;
extern string_attr_type<void> SA_BODY;
extern string_attr_type<void> SA_HIDDEN;
extern string_attr_type<const intern_string_t> SA_FORMAT;
extern string_attr_type<void> SA_REMOVED;
extern string_attr_type<std::string> SA_INVALID;
extern string_attr_type<std::string> SA_ERROR;

#endif
