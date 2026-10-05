/**
 * Copyright (c) 2026, Timothy Stack
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

#ifndef lnav_attr_line_html_hh
#define lnav_attr_line_html_hh

#include <string>

#include "base/attr_line.hh"

namespace lnav::html {

/**
 * Render an attributed line as inline HTML.
 *
 * Roles and log levels become CSS classes named after the theme's properties
 * (e.g. "-lnav_styles_error"), the same names md2attr_line accepts in a
 * <span class="...">, so the styling comes from theme_stylesheet().  Explicit
 * colors and text styles become inline styles.  Attributes that are not
 * drawn, like the file the line came from, become a tooltip: a title on the
 * span of the text they cover, or, if they cover the whole line, on a span
 * that wraps it.  Otherwise, the output is a flat sequence of <span> and <a>
 * elements, so newlines and runs of spaces only survive if the caller puts it
 * in a <pre> or similar.
 *
 * @param al The line to render.
 * @return The HTML.
 */
std::string to_html(const attr_line_t& al);

/**
 * @return CSS rules for the classes to_html() uses, generated from the
 *   current theme.
 */
std::string theme_stylesheet();

}  // namespace lnav::html

#endif
