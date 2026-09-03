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

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/result.h"
#include "intervaltree/IntervalTree.h"
#include "mapbox/variant.hpp"
#include "md4c/md4c.h"
#include "text_format.hh"

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

    void set_line_number_from(const char* text);
    block build_block(MD_BLOCKTYPE type, void* detail);
    span build_span(MD_SPANTYPE type, void* detail);

    using line_type_t = interval_tree::Interval<size_t, int>;
    using lines_tree_t = interval_tree::IntervalTree<size_t, int>;

    string_fragment eh_fragment;
    std::unique_ptr<lines_tree_t> eh_tree;
    int eh_line_number{0};
};

namespace details {
Result<void, std::string> parse(const string_fragment& sf, event_handler& eh);
}  // namespace details

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

text_auto_buffer escape_html(string_fragment content);

struct file {
    string_fragment f_frontmatter;
    text_format_t f_frontmatter_format{text_format_t::TF_PLAINTEXT};
    string_fragment f_body;
};

file parse_file(const std::filesystem::path& src, const string_fragment& sf);

namespace literals {

struct emoji_literal {
    string_fragment el_shortname;
    string_fragment el_value;
};

/**
 * The shortcodes lnav itself writes.  Kept here so that using one costs
 * nothing: get_emoji_map() parses the emoji blob and builds a reverse index
 * over every emoji there is, which is a lot of work to spell a dozen glyphs
 * that are known when the code is written.  That map is still what a
 * shortcode in someone's markdown goes through.
 *
 * A test checks these against the map, so a typo here cannot go unnoticed.
 */
inline constexpr emoji_literal KNOWN_EMOJIS[] = {
    {":bar_chart:"_frag, "\U0001F4CA"_frag},
    {":bulb:"_frag, "\U0001F4A1"_frag},
    {":clipboard:"_frag, "\U0001F4CB"_frag},
    {":compass:"_frag, "\U0001F9ED"_frag},
    {":floppy_disk:"_frag, "\U0001F4BE"_frag},
    {":framed_picture:"_frag, "\U0001F5BC"_frag},  // NB: emojis.json lists this shortcode twice
    {":globe_with_meridians:"_frag, "\U0001F310"_frag},
    {":mag_right:"_frag, "\U0001F50E"_frag},
    {":mailbox:"_frag, "\U0001F4EB"_frag},
    {":memo:"_frag, "\U0001F4DD"_frag},
    {":open_file_folder:"_frag, "\U0001F4C2"_frag},
    {":play_button:"_frag, "\u25B6"_frag},
    {":small_red_triangle:"_frag, "\U0001F53A"_frag},
    {":speech_balloon:"_frag, "\U0001F4AC"_frag},
    {":star2:"_frag, "\U0001F31F"_frag},
    {":warning:"_frag, "\u26A0\uFE0F"_frag},  // NB: emojis.json lists this shortcode twice
};

/**
 * @return The glyph for one of the shortcodes in KNOWN_EMOJIS.
 *
 * The fragment is over a string literal, so it outlives any caller.  A
 * shortcode that is not in the table yields an empty fragment, which the
 * test above catches -- and is a hard error when this is evaluated as a
 * constant expression.
 */
constexpr string_fragment
operator""_emoji(const char* str, std::size_t len)
{
    for (const auto& lit : KNOWN_EMOJIS) {
        if (lit.el_shortname.length() == static_cast<int>(len)
            && __builtin_memcmp(lit.el_shortname.data(), str, len) == 0)
        {
            return lit.el_value;
        }
    }

    return string_fragment{};
}

}  // namespace literals

}  // namespace md4cpp

#endif
