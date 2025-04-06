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
 * @file lnav_util.cc
 *
 * Dumping ground for useful functions with no other home.
 */

#include "lnav_util.hh"

#include <stdio.h>
#include <sys/stat.h>

#include "base/ansi_scrubber.hh"
#include "base/itertools.hh"
#include "base/result.h"
#include "bookmarks.hh"
#include "config.h"
#include "fmt/format.h"
#include "log_format_fwd.hh"
#include "view_curses.hh"
#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"

bool
change_to_parent_dir()
{
    bool retval = false;
    char cwd[3] = "";

    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        /* perror("getcwd"); */
    }
    if (strcmp(cwd, "/") != 0) {
        if (chdir("..") == -1) {
            perror("chdir('..')");
        } else {
            retval = true;
        }
    }

    return retval;
}

bool
is_dev_null(const struct stat& st)
{
    struct stat null_stat;

    stat("/dev/null", &null_stat);

    return st.st_dev == null_stat.st_dev && st.st_ino == null_stat.st_ino;
}

bool
is_dev_null(int fd)
{
    struct stat fd_stat;

    fstat(fd, &fd_stat);
    return is_dev_null(fd_stat);
}

size_t
write_line_to(FILE* outfile, const attr_line_t& al)
{
    const auto& al_attrs = al.get_attrs();
    const auto lr = find_string_attr_range(al_attrs, &SA_ORIGINAL_LINE);

    if (lr.empty() || !lr.is_valid() || lr.lr_start > 1) {
        // If the line is prefixed with some extra information, include that
        // in the output.  For example, the log file name or time offset.
        lnav::console::println(outfile, al);
        return al.column_width();
    }
    const auto sub_al = al.subline(lr.lr_start, lr.length());
    lnav::console::println(outfile, sub_al);

    return sub_al.column_width();
}

namespace lnav {

std::string
to_json(const std::string& str)
{
    yajlpp_gen gen;

    yajl_gen_config(gen, yajl_gen_beautify, false);
    yajl_gen_string(gen, str);

    return gen.to_string_fragment().to_string();
}

static void
to_json(yajlpp_gen& gen, const attr_line_t& al)
{
    {
        yajlpp_map root_map(gen);

        root_map.gen("str");
        root_map.gen(al.get_string());

        root_map.gen("attrs");
        {
            yajlpp_array attr_array(gen);

            for (const auto& sa : al.get_attrs()) {
                yajlpp_map elem_map(gen);

                elem_map.gen("start");
                elem_map.gen(sa.sa_range.lr_start);
                elem_map.gen("end");
                elem_map.gen(sa.sa_range.lr_end);
                elem_map.gen("type");
                elem_map.gen(sa.sa_type->sat_name);
                elem_map.gen("value");
                sa.sa_value.match(
                    [&](int64_t i) { elem_map.gen(i); },
                    [&](role_t r) {
                        elem_map.gen(lnav::enums::to_underlying(r));
                    },
                    [&](const intern_string_t& str) { elem_map.gen(str); },
                    [&](const std::string& str) { elem_map.gen(str); },
                    [&](const text_attrs& ta) { elem_map.gen(ta.ta_attrs); },
                    [&](const std::shared_ptr<logfile>& lf) {
                        elem_map.gen("");
                    },
                    [&](const bookmark_metadata* bm) { elem_map.gen(""); },
                    [&](const timespec& ts) { elem_map.gen(""); },
                    [&](const string_fragment& sf) { elem_map.gen(sf); },
                    [&](const block_elem_t& be) { elem_map.gen(""); },
                    [&](const styling::color_unit& rgb) { elem_map.gen(""); },
                    [&](const ui_icon_t& ic) { elem_map.gen(""); },
                    [&](const ui_command& uc) { elem_map.gen(""); },
                    [&](const text_format_t tf) { elem_map.gen(""); });
            }
        }
    }
}

std::string
to_json(const attr_line_t& al)
{
    yajlpp_gen gen;

    yajl_gen_config(gen, yajl_gen_beautify, false);
    to_json(gen, al);

    return gen.to_string_fragment().to_string();
}

std::string
to_json(const lnav::console::user_message& um)
{
    yajlpp_gen gen;

    yajl_gen_config(gen, yajl_gen_beautify, false);

    {
        yajlpp_map root_map(gen);

        root_map.gen("level");
        switch (um.um_level) {
            case console::user_message::level::raw:
                root_map.gen("raw");
                break;
            case console::user_message::level::ok:
                root_map.gen("ok");
                break;
            case console::user_message::level::info:
                root_map.gen("info");
                break;
            case console::user_message::level::warning:
                root_map.gen("warning");
                break;
            case console::user_message::level::error:
                root_map.gen("error");
                break;
        }

        root_map.gen("message");
        to_json(gen, um.um_message);
        root_map.gen("reason");
        to_json(gen, um.um_reason);
        root_map.gen("snippets");
        {
            yajlpp_array snippet_array(gen);

            for (const auto& snip : um.um_snippets) {
                yajlpp_map snip_map(gen);

                snip_map.gen("source");
                snip_map.gen(snip.s_location.sl_source);
                snip_map.gen("line");
                snip_map.gen(snip.s_location.sl_line_number);
                snip_map.gen("content");
                to_json(gen, snip.s_content);
            }
        }
        root_map.gen("notes");
        {
            yajlpp_array notes_array(gen);

            for (const auto& note : um.um_notes) {
                to_json(gen, note);
            }
        }
        root_map.gen("help");
        to_json(gen, um.um_help);
    }

    return gen.to_string_fragment().to_string();
}

static int
read_string_attr_type(yajlpp_parse_context* ypc,
                      const unsigned char* str,
                      size_t len,
                      yajl_string_props_t*)
{
    auto* sa = (string_attr*) ypc->ypc_obj_stack.top();
    auto type = std::string((const char*) str, len);

    if (type == "role") {
        sa->sa_type = &VC_ROLE;
    } else if (type == "preformatted") {
        sa->sa_type = &SA_PREFORMATTED;
    } else if (type == "style") {
        sa->sa_type = &VC_STYLE;
    } else {
        log_error("unhandled string_attr type: %s", type.c_str());
        ensure(false);
    }
    return 1;
}

static int
read_string_attr_int_value(yajlpp_parse_context* ypc, long long in)
{
    auto* sa = (string_attr*) ypc->ypc_obj_stack.top();

    if (sa->sa_type == &VC_ROLE) {
        sa->sa_value = static_cast<role_t>(in);
    } else if (sa->sa_type == &VC_STYLE) {
        sa->sa_value = text_attrs{
            static_cast<uint32_t>(in),
        };
    }
    return 1;
}

static const struct json_path_container string_attr_handlers = {
    yajlpp::property_handler("start").for_field(&string_attr::sa_range,
                                                &line_range::lr_start),
    yajlpp::property_handler("end").for_field(&string_attr::sa_range,
                                              &line_range::lr_end),
    yajlpp::property_handler("type").add_cb(read_string_attr_type),
    yajlpp::property_handler("value").add_cb(read_string_attr_int_value),
};

static const typed_json_path_container<attr_line_t> attr_line_handlers = {
    yajlpp::property_handler("str").for_field(&attr_line_t::al_string),
    yajlpp::property_handler("attrs#")
        .for_field(&attr_line_t::al_attrs)
        .with_children(string_attr_handlers),
};

template<>
Result<attr_line_t, std::vector<console::user_message>>
from_json(const std::string& json)
{
    static const auto STRING_SRC = intern_string::lookup("string");

    return attr_line_handlers.parser_for(STRING_SRC).of(json);
}

static const json_path_container snippet_handlers = {
    yajlpp::property_handler("source").for_field(&console::snippet::s_location,
                                                 &source_location::sl_source),
    yajlpp::property_handler("line").for_field(
        &console::snippet::s_location, &source_location::sl_line_number),
    yajlpp::property_handler("content")
        .for_child(&console::snippet::s_content)
        .with_children(attr_line_handlers),
};

static const json_path_handler_base::enum_value_t LEVEL_ENUM[] = {
    {"raw", lnav::console::user_message::level::raw},
    {"ok", lnav::console::user_message::level::ok},
    {"info", lnav::console::user_message::level::info},
    {"warning", lnav::console::user_message::level::warning},
    {"error", lnav::console::user_message::level::error},

    json_path_handler_base::ENUM_TERMINATOR,
};

static const typed_json_path_container<console::user_message>
    user_message_handlers = {
        yajlpp::property_handler("level")
            .with_enum_values(LEVEL_ENUM)
            .for_field(&console::user_message::um_level),
        yajlpp::property_handler("message")
            .for_child(&console::user_message::um_message)
            .with_children(attr_line_handlers),
        yajlpp::property_handler("reason")
            .for_child(&console::user_message::um_reason)
            .with_children(attr_line_handlers),
        yajlpp::property_handler("snippets#")
            .for_field(&console::user_message::um_snippets)
            .with_children(snippet_handlers),
        yajlpp::property_handler("notes#")
            .for_field(&console::user_message::um_notes)
            .with_children(attr_line_handlers),
        yajlpp::property_handler("help")
            .for_child(&console::user_message::um_help)
            .with_children(attr_line_handlers),
};

template<>
Result<lnav::console::user_message, std::vector<console::user_message>>
from_json(const std::string& json)
{
    static const auto STRING_SRC = intern_string::lookup("string");

    return user_message_handlers.parser_for(STRING_SRC).of(json);
}

}  // namespace lnav
