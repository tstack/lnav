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

#ifndef lnav_text_anonymizer_hh
#define lnav_text_anonymizer_hh

#include <string>
#include <vector>

#include "base/intern_string.hh"
#include "robin_hood/robin_hood.h"

namespace lnav {

class text_anonymizer {
public:
    text_anonymizer() = default;

    std::string next(string_fragment line);

private:
    template<typename F>
    const std::string& get_default(
        robin_hood::unordered_map<std::string, std::string>& mapping,
        const std::string& input,
        F provider)
    {
        auto iter = mapping.find(input);
        if (iter == mapping.end()) {
            auto emp_res
                = mapping.emplace(input, provider(mapping.size(), input));

            iter = emp_res.first;
        }

        return iter->second;
    }

    robin_hood::unordered_map<std::string, std::string> ta_mac_addresses;
    robin_hood::unordered_map<std::string, std::string> ta_ipv4_addresses;
    robin_hood::unordered_map<std::string, std::string> ta_ipv6_addresses;
    robin_hood::unordered_map<std::string, std::string> ta_user_names;
    robin_hood::unordered_map<std::string, std::string> ta_host_names;
    robin_hood::unordered_map<std::string, std::string> ta_symbols;
};

}  // namespace lnav

#endif
