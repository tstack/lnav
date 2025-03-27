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

#include "md4cpp.hh"

#include "base/is_utf8.hh"
#include "base/lnav_log.hh"
#include "emojis-json.h"
#include "pcrepp/pcre2pp.hh"
#include "xml-entities-json.h"
#include "yajlpp/yajlpp_def.hh"

namespace md4cpp {

static const typed_json_path_container<xml_entity_map>&
get_xml_entity_map_handlers()
{
    static const typed_json_path_container<xml_entity> xml_entity_handlers = {
        yajlpp::property_handler("characters").for_field(&xml_entity::xe_chars),
    };

    static const typed_json_path_container<xml_entity_map> retval = {
        yajlpp::pattern_property_handler("(?<var_name>\\&\\w+;?)")
            .with_synopsis("<name>")
            .with_path_provider<xml_entity_map>(
                [](struct xml_entity_map* xem,
                   std::vector<std::string>& paths_out) {
                    for (const auto& iter : xem->xem_entities) {
                        paths_out.emplace_back(iter.first);
                    }
                })
            .with_obj_provider<xml_entity, xml_entity_map>(
                [](const yajlpp_provider_context& ypc, xml_entity_map* xem) {
                    auto entity_name = ypc.get_substr(0);
                    return &xem->xem_entities[entity_name];
                })
            .with_children(xml_entity_handlers),
    };

    return retval;
}

static const typed_json_path_container<emoji_map>&
get_emoji_map_handlers()
{
    static const typed_json_path_container<emoji> emoji_handlers = {
        yajlpp::property_handler("emoji").for_field(&emoji::e_value),
        yajlpp::property_handler("shortname").for_field(&emoji::e_shortname),
    };

    static const typed_json_path_container<emoji_map> retval = {
        yajlpp::property_handler("emojis#")
            .for_field(&emoji_map::em_emojis)
            .with_children(emoji_handlers),
    };

    return retval;
}

static xml_entity_map
load_xml_entity_map()
{
    static const intern_string_t name
        = intern_string::lookup(xml_entities_json.get_name());
    auto sfp = xml_entities_json.to_string_fragment_producer();
    auto parse_res = get_xml_entity_map_handlers()
                         .parser_for(name)
                         .with_ignore_unused(true)
                         .of(*sfp);

    assert(parse_res.isOk());

    return parse_res.unwrap();
}

const xml_entity_map&
get_xml_entity_map()
{
    static const auto retval = load_xml_entity_map();

    return retval;
}

static emoji_map
load_emoji_map()
{
    static const intern_string_t name
        = intern_string::lookup(emojis_json.get_name());
    auto sfp = emojis_json.to_string_fragment_producer();
    auto parse_res
        = get_emoji_map_handlers().parser_for(name).with_ignore_unused(true).of(
            *sfp);

    assert(parse_res.isOk());

    auto retval = parse_res.unwrap();
    for (auto& em : retval.em_emojis) {
        retval.em_shortname2emoji.emplace(em.e_shortname, em);
    }

    return retval;
}

const emoji_map&
get_emoji_map()
{
    static const auto retval = load_emoji_map();

    return retval;
}

std::string
escape_html(string_fragment content)
{
    std::string retval;

    retval.reserve(content.length());
    for (const auto ch : content) {
        switch (ch) {
            case '"':
                retval.append("&quot;");
                break;
            case '\'':
                retval.append("&apos;");
                break;
            case '<':
                retval.append("&lt;");
                break;
            case '>':
                retval.append("&gt;");
                break;
            case '&':
                retval.append("&amp;");
                break;
            default:
                retval.push_back(ch);
                break;
        }
    }

    return retval;
}

struct parse_userdata {
    event_handler& pu_handler;
    std::string pu_error_msg;
};

void
event_handler::set_line_number_from(const char* text)
{
    if (this->eh_fragment.begin() <= text && text < this->eh_fragment.end()) {
        auto off = text - this->eh_fragment.begin();

        this->eh_tree->visit_overlapping(off, off + 1, [this](const auto& cintv) {
            this->eh_line_number = cintv.value;
        });
    }
}

event_handler::block
event_handler::build_block(MD_BLOCKTYPE type, void* detail)
{
    switch (type) {
        case MD_BLOCK_DOC:
            return block_doc{};
        case MD_BLOCK_QUOTE:
            return block_quote{};
        case MD_BLOCK_UL:
            return static_cast<MD_BLOCK_UL_DETAIL*>(detail);
        case MD_BLOCK_OL:
            return static_cast<MD_BLOCK_OL_DETAIL*>(detail);
        case MD_BLOCK_LI:
            return static_cast<MD_BLOCK_LI_DETAIL*>(detail);
        case MD_BLOCK_HR:
            return block_hr{};
        case MD_BLOCK_H:
            return static_cast<MD_BLOCK_H_DETAIL*>(detail);
        case MD_BLOCK_CODE: {
            auto retval = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);

            this->set_line_number_from(retval->lang.text);
            return retval;
        }
        case MD_BLOCK_HTML:
            return block_html{};
        case MD_BLOCK_P:
            return block_p{};
        case MD_BLOCK_TABLE:
            return static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
        case MD_BLOCK_THEAD:
            return block_thead{};
        case MD_BLOCK_TBODY:
            return block_tbody{};
        case MD_BLOCK_TR:
            return block_tr{};
        case MD_BLOCK_TH:
            return block_th{};
        case MD_BLOCK_TD:
            return static_cast<MD_BLOCK_TD_DETAIL*>(detail);
    }

    return {};
}

event_handler::span
event_handler::build_span(MD_SPANTYPE type, void* detail)
{
    switch (type) {
        case MD_SPAN_EM:
            return span_em{};
        case MD_SPAN_STRONG:
            return span_strong{};
        case MD_SPAN_A:
            return static_cast<MD_SPAN_A_DETAIL*>(detail);
        case MD_SPAN_IMG:
            return static_cast<MD_SPAN_IMG_DETAIL*>(detail);
        case MD_SPAN_CODE:
            return span_code{};
        case MD_SPAN_DEL:
            return span_del{};
        case MD_SPAN_U:
            return span_u{};
        default:
            break;
    }

    return {};
}

static int
md4cpp_enter_block(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    auto* pu = static_cast<parse_userdata*>(userdata);

    auto enter_res
        = pu->pu_handler.enter_block(pu->pu_handler.build_block(type, detail));
    if (enter_res.isErr()) {
        pu->pu_error_msg = enter_res.unwrapErr();
        return 1;
    }

    return 0;
}

static int
md4cpp_leave_block(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    auto* pu = static_cast<parse_userdata*>(userdata);
    auto leave_res
        = pu->pu_handler.leave_block(pu->pu_handler.build_block(type, detail));
    if (leave_res.isErr()) {
        pu->pu_error_msg = leave_res.unwrapErr();
        return 1;
    }

    return 0;
}

static int
md4cpp_enter_span(MD_SPANTYPE type, void* detail, void* userdata)
{
    auto* pu = static_cast<parse_userdata*>(userdata);

    auto enter_res
        = pu->pu_handler.enter_span(pu->pu_handler.build_span(type, detail));
    if (enter_res.isErr()) {
        pu->pu_error_msg = enter_res.unwrapErr();
        return 1;
    }

    return 0;
}

static int
md4cpp_leave_span(MD_SPANTYPE type, void* detail, void* userdata)
{
    auto* pu = static_cast<parse_userdata*>(userdata);

    auto leave_res
        = pu->pu_handler.leave_span(pu->pu_handler.build_span(type, detail));
    if (leave_res.isErr()) {
        pu->pu_error_msg = leave_res.unwrapErr();
        return 1;
    }

    return 0;
}

static int
md4cpp_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    auto* pu = static_cast<parse_userdata*>(userdata);

    auto text_res = pu->pu_handler.text(type, string_fragment(text, 0, size));
    if (text_res.isErr()) {
        pu->pu_error_msg = text_res.unwrapErr();
        return 1;
    }

    return 0;
}

namespace details {
Result<void, std::string>
parse(const string_fragment& sf, event_handler& eh)
{
    auto scan_res = is_utf8(sf);
    if (!scan_res.is_valid()) {
        return Err(
            fmt::format(FMT_STRING("file has invalid UTF-8 at offset {}: {}"),
                        scan_res.usr_valid_frag.sf_end,
                        scan_res.usr_message));
    }

    auto eols = std::vector<event_handler::line_type_t>{};
    int lineno = 1;

    auto rest_sf = sf;
    while (!rest_sf.empty()) {
        auto split_pair = rest_sf.split_when(string_fragment::tag1{'\n'});
        auto line_sf = split_pair.first;

        eols.emplace_back(line_sf.sf_begin, line_sf.sf_end, lineno++);
        rest_sf = split_pair.second;
    }

    MD_PARSER parser = {0};
    eh.eh_fragment = sf;
    eh.eh_tree = std::make_unique<event_handler::lines_tree_t>(std::move(eols));
    auto pu = parse_userdata{eh};

    parser.abi_version = 0;
    parser.flags
        = (MD_DIALECT_GITHUB | MD_FLAG_UNDERLINE | MD_FLAG_STRIKETHROUGH)
        & ~(MD_FLAG_PERMISSIVEAUTOLINKS);
    parser.enter_block = md4cpp_enter_block;
    parser.leave_block = md4cpp_leave_block;
    parser.enter_span = md4cpp_enter_span;
    parser.leave_span = md4cpp_leave_span;
    parser.text = md4cpp_text;

    auto rc = md_parse(sf.data(), sf.length(), &parser, &pu);

    if (rc == 0) {
        return Ok();
    }

    return Err(pu.pu_error_msg);
}
}  // namespace details

}  // namespace md4cpp
