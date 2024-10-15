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

#include "string_attr_type.hh"

#include "config.h"

string_attr_type<void> SA_ORIGINAL_LINE("original_line");
string_attr_type<void> SA_BODY("body");
string_attr_type<void> SA_HIDDEN("hidden");
string_attr_type<const intern_string_t> SA_FORMAT("format");
string_attr_type<void> SA_REMOVED("removed");
string_attr_type<void> SA_PREFORMATTED("preformatted");
string_attr_type<std::string> SA_INVALID("invalid");
string_attr_type<std::string> SA_ERROR("error");
string_attr_type<int64_t> SA_LEVEL("level");
string_attr_type<int64_t> SA_ORIGIN_OFFSET("origin-offset");

string_attr_type<role_t> VC_ROLE("role");
string_attr_type<role_t> VC_ROLE_FG("role-fg");
string_attr_type<text_attrs> VC_STYLE("style");
string_attr_type<int64_t> VC_GRAPHIC("graphic");
string_attr_type<block_elem_t> VC_BLOCK_ELEM("block-elem");
string_attr_type<int64_t> VC_FOREGROUND("foreground");
string_attr_type<int64_t> VC_BACKGROUND("background");
string_attr_type<std::string> VC_HYPERLINK("hyperlink");
string_attr_type<ui_icon_t> VC_ICON("icon");
