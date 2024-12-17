/**
 * Copyright (c) 2017, Timothy Stack
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

#ifndef LNAV_HELP_TEXT_FORMATTER_HH
#define LNAV_HELP_TEXT_FORMATTER_HH

#include <functional>

#include "base/attr_line.hh"
#include "help_text.hh"

using help_example_to_attr_line_fun_t
    = std::function<const attr_line_t&(const help_text&, const help_example&)>;

enum class help_text_content {
    synopsis,
    synopsis_and_summary,
    full,
};

void format_help_text_for_term(const help_text& ht,
                               size_t width,
                               attr_line_t& out,
                               help_text_content htc = help_text_content::full);

void format_example_text_for_term(const help_text& ht,
                                  help_example_to_attr_line_fun_t eval,
                                  size_t width,
                                  attr_line_t& out,
                                  help_example::language lang
                                  = help_example::language::undefined);

void format_help_text_for_rst(const help_text& ht,
                              help_example_to_attr_line_fun_t eval,
                              FILE* rst_file);

#endif  // LNAV_HELP_TEXT_FORMATTER_HH
