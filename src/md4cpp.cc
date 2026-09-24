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

#include <algorithm>

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

text_auto_buffer
escape_html(string_fragment content)
{
    auto retval = auto_buffer::alloc(content.length());

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

    return text_auto_buffer{std::move(retval)};
}

file
parse_file(const std::filesystem::path& src, const string_fragment& sf)
{
    static const auto FRONTMATTER_RE = lnav::pcre2pp::code::from_const(
        R"(\A(?:---\r?\n(?:(.*?)\r?\n)?---\r?\n|\+\+\+\r?\n(?:(.*?)\r?\n)?\+\+\+\r?\n))",
        PCRE2_DOTALL);
    thread_local auto md = FRONTMATTER_RE.create_match_data();

    auto frontmatter_sf = string_fragment{};
    auto frontmatter_format = text_format_t::TF_PLAINTEXT;
    auto content_sf = sf;

    auto cap_res = FRONTMATTER_RE.capture_from(content_sf)
                       .into(md)
                       .matches()
                       .ignore_error();
    if (cap_res) {
        // The capture is absent for an empty block, so the delimiter
        // decides the format.
        if (content_sf.startswith("---")) {
            frontmatter_format = text_format_t::TF_YAML;
            frontmatter_sf = md[1].value_or(string_fragment{});
        } else {
            frontmatter_format = text_format_t::TF_TOML;
            frontmatter_sf = md[2].value_or(string_fragment{});
        }
        content_sf = cap_res->f_remaining;
    } else if (content_sf.startswith("{")) {
        yajlpp_parse_context ypc(intern_string::lookup(src));
        auto handle = yajlpp::alloc_handle(&ypc.ypc_callbacks, &ypc);

        yajl_config(handle.in(), yajl_allow_trailing_garbage, 1);
        ypc.with_ignore_unused(true)
            .with_handle(handle.in())
            .with_error_reporter([&src](const auto& ypc, const auto& um) {
                log_error(
                    "%s: failed to parse JSON front matter "
                    "-- %s",
                    src.c_str(),
                    um.um_reason.al_string.c_str());
            });
        if (ypc.parse_doc(content_sf)) {
            ssize_t consumed = ypc.ypc_total_consumed;
            auto rest_sf = content_sf.substr(consumed);
            if (rest_sf.startswith("\n") || rest_sf.startswith("\r\n")) {
                frontmatter_format = text_format_t::TF_JSON;
                frontmatter_sf = sf.sub_range(0, consumed);
                content_sf = content_sf.substr(consumed);
            }
        }
    }

    return {
        frontmatter_sf,
        frontmatter_format,
        content_sf,
    };
}

struct parse_userdata {
    event_handler& pu_handler;
    std::string pu_error_msg;
};

void
event_handler::set_line_number_from(const char* text)
{
    // md4c only hands back pointers into the source for some details (e.g.
    // not for indented code or an escaped language), so zero out the line
    // number instead of leaving the previous block's value.
    this->eh_line_number = 0;
    if (text == nullptr || text < this->eh_fragment.begin()
        || this->eh_fragment.end() <= text)
    {
        return;
    }

    size_t off = text - this->eh_fragment.begin();
    this->eh_tree->visit_overlapping(off, [this](const auto& cintv) {
        this->eh_line_number = cintv.value;
    });
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

    log_warning("unhandled markdown block type: %d", type);
    return block_unknown{};
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

    log_warning("unhandled markdown span type: %d", type);
    return span_unknown{};
}

template<typename F>
static int
md4cpp_call(void* userdata, F&& func)
{
    auto* pu = static_cast<parse_userdata*>(userdata);

    auto res = func(pu->pu_handler);
    if (res.isErr()) {
        pu->pu_error_msg = res.unwrapErr();
        return 1;
    }

    return 0;
}

static int
md4cpp_enter_block(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    return md4cpp_call(userdata, [&](event_handler& eh) {
        return eh.enter_block(eh.build_block(type, detail));
    });
}

static int
md4cpp_leave_block(MD_BLOCKTYPE type, void* detail, void* userdata)
{
    return md4cpp_call(userdata, [&](event_handler& eh) {
        return eh.leave_block(eh.build_block(type, detail));
    });
}

static int
md4cpp_enter_span(MD_SPANTYPE type, void* detail, void* userdata)
{
    return md4cpp_call(userdata, [&](event_handler& eh) {
        return eh.enter_span(eh.build_span(type, detail));
    });
}

static int
md4cpp_leave_span(MD_SPANTYPE type, void* detail, void* userdata)
{
    return md4cpp_call(userdata, [&](event_handler& eh) {
        return eh.leave_span(eh.build_span(type, detail));
    });
}

static int
md4cpp_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    return md4cpp_call(userdata, [&](event_handler& eh) {
        return eh.text(type, string_fragment(text, 0, size));
    });
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
    // The fragment can start partway into a file (e.g. the body after the
    // front matter), so count the lines that precede it to keep the line
    // numbers relative to the start of the file.
    int lineno = 1
        + std::count(sf.sf_string, sf.sf_string + sf.sf_begin, '\n');

    auto rest_sf = sf;
    while (!rest_sf.empty()) {
        auto split_pair = rest_sf.split_when(string_fragment::tag1{'\n'});
        auto line_sf = split_pair.first;

        eols.emplace_back(line_sf.sf_begin - sf.sf_begin,
                          line_sf.sf_end - sf.sf_begin,
                          lineno++);
        rest_sf = split_pair.second;
    }

    MD_PARSER parser = {0};
    eh.eh_fragment = sf;
    eh.eh_tree = std::make_unique<event_handler::lines_tree_t>(std::move(eols));
    auto pu = parse_userdata{eh};

    parser.abi_version = 0;
    parser.flags
        = (MD_DIALECT_GITHUB | MD_FLAG_UNDERLINE) & ~MD_FLAG_PERMISSIVEAUTOLINKS;
    parser.enter_block = md4cpp_enter_block;
    parser.leave_block = md4cpp_leave_block;
    parser.enter_span = md4cpp_enter_span;
    parser.leave_span = md4cpp_leave_span;
    parser.text = md4cpp_text;

    auto rc = md_parse(sf.data(), sf.length(), &parser, &pu);

    // The fragment belongs to the caller and need not outlive this call.
    eh.eh_fragment = string_fragment{};
    eh.eh_tree.reset();

    if (rc == 0) {
        return Ok();
    }

    if (pu.pu_error_msg.empty()) {
        return Err(fmt::format(FMT_STRING("markdown parser failed ({})"), rc));
    }

    return Err(pu.pu_error_msg);
}
}  // namespace details

}  // namespace md4cpp
