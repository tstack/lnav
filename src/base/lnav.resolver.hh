/**
 * Copyright (c) 2025, Timothy Stack
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
 * @file lnav.resolver.hh
 */

#ifndef lnav_resolver_hh
#define lnav_resolver_hh

#include <map>
#include <string>
#include <vector>

#include "base/intern_string.hh"
#include "base/types.hh"
#include "fmt/format.h"
#include "mapbox/variant.hpp"

using scoped_value_t = mapbox::util::
    variant<std::string, string_fragment, int64_t, double, null_value_t, bool>;

namespace fmt {
template<>
struct formatter<scoped_value_t> : formatter<std::string> {
    template<typename FormatContext>
    auto format(const scoped_value_t& sv, FormatContext& ctx) const
    {
        auto retval
            = sv.match([](std::string str) { return str; },
                       [](string_fragment sf) { return sf.to_string(); },
                       [](null_value_t) { return std::string("<NULL>"); },
                       [](int64_t value) { return fmt::to_string(value); },
                       [](double value) { return fmt::to_string(value); },
                       [](bool value) { return value ? "true" : "false"; });

        return fmt::formatter<std::string>::format(retval, ctx);
    }
};
}  // namespace fmt

class scoped_resolver {
public:
    scoped_resolver(
        std::initializer_list<const std::map<std::string, scoped_value_t>*> l)
    {
        this->sr_stack.insert(this->sr_stack.end(), l.begin(), l.end());
    }

    using const_iterator
        = std::map<std::string, scoped_value_t>::const_iterator;

    const_iterator find(const std::string& str) const
    {
        const_iterator retval;

        for (const auto* scope : this->sr_stack) {
            if ((retval = scope->find(str)) != scope->end()) {
                return retval;
            }
        }

        return this->end();
    }

    const_iterator end() const { return this->sr_stack.back()->end(); }

    std::vector<const std::map<std::string, scoped_value_t>*> sr_stack;
};

#endif
