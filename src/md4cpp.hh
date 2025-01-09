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

#ifndef lnav_md4cpp_hh
#define lnav_md4cpp_hh

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/intern_string.hh"
#include "base/result.h"
#include "mapbox/variant.hpp"
#include "md4c/md4c.h"

namespace md4cpp {

struct xml_entity {
    std::string xe_chars;
};

struct xml_entity_map {
    std::map<std::string, xml_entity> xem_entities;
};

struct emoji {
    std::string e_shortname;
    std::string e_value;
};

struct emoji_map {
    std::vector<emoji> em_emojis;
    std::unordered_map<std::string, std::reference_wrapper<emoji>>
        em_shortname2emoji;
};

class event_handler {
public:
    virtual ~event_handler() = default;

    struct block_doc {};
    struct block_quote {};
    struct block_hr {};
    struct block_html {};
    struct block_p {};
    struct block_thead {};
    struct block_tbody {};
    struct block_tr {};
    struct block_th {};

    using block = mapbox::util::variant<block_doc,
                                        block_quote,
                                        MD_BLOCK_UL_DETAIL*,
                                        MD_BLOCK_OL_DETAIL*,
                                        MD_BLOCK_LI_DETAIL*,
                                        block_hr,
                                        MD_BLOCK_H_DETAIL*,
                                        MD_BLOCK_CODE_DETAIL*,
                                        block_html,
                                        block_p,
                                        MD_BLOCK_TABLE_DETAIL*,
                                        block_thead,
                                        block_tbody,
                                        block_tr,
                                        block_th,
                                        MD_BLOCK_TD_DETAIL*>;

    virtual Result<void, std::string> enter_block(const block& bl) = 0;
    virtual Result<void, std::string> leave_block(const block& bl) = 0;

    struct span_em {};
    struct span_strong {};
    struct span_code {};
    struct span_del {};
    struct span_u {};

    using span = mapbox::util::variant<span_em,
                                       span_strong,
                                       MD_SPAN_A_DETAIL*,
                                       MD_SPAN_IMG_DETAIL*,
                                       span_code,
                                       span_del,
                                       span_u>;

    virtual Result<void, std::string> enter_span(const span& bl) = 0;
    virtual Result<void, std::string> leave_span(const span& bl) = 0;

    virtual Result<void, std::string> text(MD_TEXTTYPE tt,
                                           const string_fragment& sf)
        = 0;
};

namespace details {
Result<void, std::string> parse(const string_fragment& sf, event_handler& eh);
}

template<typename T>
class typed_event_handler : public event_handler {
public:
    virtual T get_result() = 0;
};

template<typename T>
Result<T, std::string>
parse(const string_fragment& sf, typed_event_handler<T>& eh)
{
    TRY(details::parse(sf, eh));

    return Ok(eh.get_result());
}

const xml_entity_map& get_xml_entity_map();

const emoji_map& get_emoji_map();

std::string escape_html(string_fragment content);

namespace literals {

inline std::string
operator"" _emoji(const char* str, std::size_t len)
{
    const auto& em = get_emoji_map();
    const auto key = std::string(str, len);

    const auto iter = em.em_shortname2emoji.find(key);
    assert(iter != em.em_shortname2emoji.end());

    return iter->second.get().e_value;
}

}  // namespace literals

}  // namespace md4cpp

#endif
