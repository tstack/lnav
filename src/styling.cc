/**
 * Copyright (c) 2019, Timothy Stack
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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <stdlib.h>

#include <string>

#include "yajlpp/yajlpp.hh"
#include "yajlpp/yajlpp_def.hh"
#include "styling.hh"
#include "ansi-palette-json.h"
#include "xterm-palette-json.h"

using namespace std;

static struct json_path_container term_color_rgb_handler = {
    yajlpp::property_handler("r")
        .FOR_FIELD(rgb_color, rc_r),
    yajlpp::property_handler("g")
        .FOR_FIELD(rgb_color, rc_g),
    yajlpp::property_handler("b")
        .FOR_FIELD(rgb_color, rc_b)
};

static struct json_path_container term_color_handler = {
    yajlpp::property_handler("colorId")
        .FOR_FIELD(term_color, xc_id),
    yajlpp::property_handler("name")
        .FOR_FIELD(term_color, xc_name),
    yajlpp::property_handler("rgb")
        .with_obj_provider<rgb_color, term_color>(
            [](const auto &pc, term_color *xc) { return &xc->xc_color; })
        .with_children(term_color_rgb_handler)
};

static struct json_path_container root_color_handler = {
    yajlpp::property_handler("#")
        .with_obj_provider<term_color, vector<term_color>>(
        [](const yajlpp_provider_context &ypc, vector<term_color> *palette) {
            palette->resize(ypc.ypc_index + 1);
            return &((*palette)[ypc.ypc_index]);
        })
        .with_children(term_color_handler)
};

term_color_palette xterm_colors(xterm_palette_json.bsf_data);
term_color_palette ansi_colors(ansi_palette_json.bsf_data);

term_color_palette *ACTIVE_PALETTE = &ansi_colors;

bool rgb_color::from_str(const string_fragment &color,
                         rgb_color &rgb_out,
                         std::string &errmsg)
{
    if (color.empty()) {
        return true;
    }

    if (color[0] == '#') {
        switch (color.length()) {
            case 4:
                if (sscanf(color.data(), "#%1hx%1hx%1hx",
                           &rgb_out.rc_r, &rgb_out.rc_g, &rgb_out.rc_b) == 3) {
                    rgb_out.rc_r |= rgb_out.rc_r << 4;
                    rgb_out.rc_g |= rgb_out.rc_g << 4;
                    rgb_out.rc_b |= rgb_out.rc_b << 4;
                    return true;
                }
                break;
            case 7:
                if (sscanf(color.data(), "#%2hx%2hx%2hx",
                           &rgb_out.rc_r, &rgb_out.rc_g, &rgb_out.rc_b) == 3) {
                    return true;
                }
                break;
        }
        errmsg = "Could not parse color: " + color.to_string();
        return false;
    }

    for (const auto &xc : xterm_colors.tc_palette) {
        if (color.iequal(xc.xc_name)) {
            rgb_out = xc.xc_color;
            return true;
        }
    }

    errmsg = "Unknown color: '" + color.to_string() +
             "'.  See https://jonasjacek.github.io/colors/ for a list of supported color names";
    return false;
}

bool rgb_color::operator<(const rgb_color &rhs) const
{
    if (rc_r < rhs.rc_r)
        return true;
    if (rhs.rc_r < rc_r)
        return false;
    if (rc_g < rhs.rc_g)
        return true;
    if (rhs.rc_g < rc_g)
        return false;
    return rc_b < rhs.rc_b;
}

bool rgb_color::operator>(const rgb_color &rhs) const
{
    return rhs < *this;
}

bool rgb_color::operator<=(const rgb_color &rhs) const
{
    return !(rhs < *this);
}

bool rgb_color::operator>=(const rgb_color &rhs) const
{
    return !(*this < rhs);
}

bool rgb_color::operator==(const rgb_color &rhs) const
{
    return rc_r == rhs.rc_r &&
           rc_g == rhs.rc_g &&
           rc_b == rhs.rc_b;
}

bool rgb_color::operator!=(const rgb_color &rhs) const
{
    return !(rhs == *this);
}

term_color_palette::term_color_palette(const unsigned char *json)
{
    yajlpp_parse_context ypc_xterm("palette.json", &root_color_handler);
    yajl_handle handle;

    handle = yajl_alloc(&ypc_xterm.ypc_callbacks, nullptr, &ypc_xterm);
    ypc_xterm
        .with_ignore_unused(true)
        .with_obj(this->tc_palette)
        .with_handle(handle);
    yajl_status st = ypc_xterm.parse(json, strlen((const char *) json));
    ensure(st == yajl_status_ok);
    st = ypc_xterm.complete_parse();
    ensure(st == yajl_status_ok);
    yajl_free(handle);

    for (auto &xc : this->tc_palette) {
        xc.xc_lab_color = lab_color(xc.xc_color);
    }
}

short term_color_palette::match_color(const lab_color &to_match)
{
    double lowest = 1000.0;
    short lowest_id = -1;

    for (auto &xc : this->tc_palette) {
        double xc_delta = xc.xc_lab_color.deltaE(to_match);

        if (lowest_id == -1) {
            lowest = xc_delta;
            lowest_id = xc.xc_id;
            continue;
        }

        if (xc_delta < lowest) {
            lowest = xc_delta;
            lowest_id = xc.xc_id;
        }
    }

    return lowest_id;
}
