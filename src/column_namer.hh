/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file column_namer.hh
 */

#ifndef lnav_column_namer_hh
#define lnav_column_namer_hh

#include <unordered_map>
#include <vector>

#include "ArenaAlloc/arenaalloc.h"
#include "base/intern_string.hh"

class column_namer {
public:
    enum class language {
        SQL,
        JSON,
    };

    static const string_fragment BUILTIN_COL;

    explicit column_namer(language lang);

    bool existing_name(const string_fragment& in_name) const;

    string_fragment add_column(const string_fragment& in_name);

    ArenaAlloc::Alloc<char> cn_alloc{1024};
    language cn_language;
    std::vector<string_fragment> cn_builtin_names;
    std::vector<string_fragment> cn_names;
    std::unordered_map<
        string_fragment,
        size_t,
        frag_hasher,
        std::equal_to<string_fragment>,
        ArenaAlloc::Alloc<std::pair<const string_fragment, size_t>>>
        cn_name_counters;
};

#endif
