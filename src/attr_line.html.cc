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

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "attr_line.html.hh"

#include "attr_line.render.hh"
#include "base/log_level_enum.hh"
#include "config.h"
#include "fmt/format.h"
#include "view_curses.hh"

namespace lnav::html {

namespace {

/**
 * Schemes that are safe to link to, which leaves out ones like
 * "javascript:" that run code when the link is clicked.
 */
constexpr string_fragment SAFE_SCHEMES[] = {
    "http"_frag,
    "https"_frag,
    "file"_frag,
    "ftp"_frag,
    "mailto"_frag,
};

/**
 * @return True if the link target is relative or uses one of the
 *   SAFE_SCHEMES.
 */
bool
is_safe_href(const std::string& href)
{
    const auto sf = string_fragment::from_str(href);
    const auto colon = sf.find(':');

    if (!colon) {
        return true;
    }

    const auto scheme = sf.sub_range(0, colon.value());
    // A colon after a path, query, or fragment separator does not end a
    // scheme.
    if (scheme.find('/') || scheme.find('?') || scheme.find('#')) {
        return true;
    }
    return std::any_of(std::begin(SAFE_SCHEMES),
                       std::end(SAFE_SCHEMES),
                       [&scheme](const string_fragment& safe) {
                           return scheme.iequal(safe);
                       });
}

/**
 * CSS has no reverse video, so the colors are swapped, using the text's
 * colors for the ones that are not set.
 */
text_attrs
reversed_colors(const text_attrs& ta)
{
    const auto filled
        = ta | view_colors::singleton().theme_attrs_for_role(role_t::VCR_TEXT);
    auto retval = text_attrs{};

    retval.ta_fg_color = filled.ta_bg_color;
    retval.ta_bg_color = filled.ta_fg_color;
    return retval;
}

void
append_escaped(std::string& out, string_fragment sf)
{
    for (const auto ch : sf) {
        switch (ch) {
            case '"':
                out.append("&quot;");
                break;
            case '\'':
                out.append("&apos;");
                break;
            case '<':
                out.append("&lt;");
                break;
            case '>':
                out.append("&gt;");
                break;
            case '&':
                out.append("&amp;");
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
}

/**
 * Like append_escaped(), but for an attribute value, so line breaks and tabs
 * are escaped too.  That keeps a multi-line note on the same output line.
 */
void
append_escaped_attr(std::string& out, string_fragment sf)
{
    for (const auto ch : sf) {
        switch (ch) {
            case '\n':
                out.append("&#10;");
                break;
            case '\t':
                out.append("&#9;");
                break;
            default:
                append_escaped(out, string_fragment::from_bytes(&ch, 1));
                break;
        }
    }
}

/**
 * Append a title attribute for the notes, if there are any.
 */
void
append_title(std::string& out, const std::vector<std::string>& notes)
{
    if (notes.empty()) {
        return;
    }

    auto sep = "";
    out.append(" title=\"");
    for (const auto& note : notes) {
        append_escaped_attr(out, string_fragment::from_c_str(sep));
        append_escaped_attr(out, string_fragment::from_str(note));
        sep = "\n";
    }
    out.append("\"");
}

/**
 * Append the CSS declarations for everything in the attributes except
 * reverse video, which the callers handle differently.  The declarations
 * contain nothing that needs to be escaped in an attribute value.
 *
 * @return True if anything was appended.
 */
bool
append_css_declarations(std::string& out, const text_attrs& ta)
{
    const auto mark = out.size();
    auto sep = "";
    auto add = [&out, &sep](const char* decl) {
        out.append(sep);
        out.append(decl);
        sep = "; ";
    };

    auto add_color = [&out, &sep](const char* name,
                                  const styling::color_unit& cu) {
        auto rgb = lnav::render::to_rgb(cu);

        if (!rgb) {
            return;
        }
        fmt::format_to(std::back_inserter(out),
                       FMT_STRING("{}{}: #{:02x}{:02x}{:02x}"),
                       sep,
                       name,
                       rgb->rc_r,
                       rgb->rc_g,
                       rgb->rc_b);
        sep = "; ";
    };

    add_color("color", ta.ta_fg_color);
    add_color("background-color", ta.ta_bg_color);
    if (ta.has_style(text_attrs::style::bold)) {
        add("font-weight: bold");
    }
    if (ta.has_style(text_attrs::style::italic)) {
        add("font-style: italic");
    }

    const auto underline = ta.has_style(text_attrs::style::underline)
        || ta.has_style(text_attrs::style::undercurl);
    const auto struck = ta.has_style(text_attrs::style::struck);
    if (underline || struck) {
        add("text-decoration: ");
        if (underline) {
            out.append("underline");
        }
        if (struck) {
            out.append(underline ? " line-through" : "line-through");
        }
        if (ta.has_style(text_attrs::style::undercurl)) {
            add("text-decoration-style: wavy");
        }
    }

    return out.size() != mark;
}

struct html_style {
    /** Kept sorted by name and without duplicates. */
    std::vector<intern_string_t> hs_classes;
    text_attrs hs_attrs;
    const std::string* hs_href{nullptr};
    const std::string* hs_id{nullptr};
    std::vector<std::string> hs_notes;

    void add_class(const intern_string_t& cls)
    {
        if (cls.empty()) {
            return;
        }

        auto name = cls.to_string_fragment().to_string_view();
        auto iter = std::lower_bound(
            this->hs_classes.begin(),
            this->hs_classes.end(),
            name,
            [](const intern_string_t& lhs, std::string_view rhs) {
                return lhs.to_string_fragment().to_string_view() < rhs;
            });
        if (iter == this->hs_classes.end() || !(*iter == cls)) {
            this->hs_classes.insert(iter, cls);
        }
    }

    bool same_style(const html_style& other) const
    {
        return this->hs_classes == other.hs_classes
            && this->hs_attrs == other.hs_attrs
            && lnav::render::same_str(this->hs_href, other.hs_href)
            && this->hs_notes == other.hs_notes;
    }
};

/**
 * Tracks the parts of a run that set one of its colors.
 */
struct color_setters {
    int cs_count{0};
    /** True if the class rules alone would not give the right color. */
    bool cs_inline{false};

    void add(const styling::color_unit& cu,
             bool has_class,
             const styling::color_unit& rule_cu)
    {
        if (cu.empty()) {
            return;
        }
        this->cs_count += 1;
        if (!has_class || !(cu == rule_cu)) {
            this->cs_inline = true;
        }
    }

    bool needs_inline() const { return this->cs_inline || this->cs_count > 1; }
};

/**
 * Roles and levels become classes so the theme's stylesheet applies.  The
 * stylesheet cannot say in which order the classes were applied, so a color
 * is given inline when more than one part sets it, when it is not the one in
 * the class's rule (e.g. a color that varies with the text), or when reverse
 * video is involved.  The other attributes are folded into inline ones.
 */
void
to_html_style(const lnav::render::styled_run& sr, html_style& out)
{
    using kind_t = lnav::render::style_part::kind_t;

    const auto& vc = view_colors::singleton();
    auto resolved = text_attrs{};
    auto fg_setters = color_setters{};
    auto bg_setters = color_setters{};
    auto has_reverse = false;

    out.hs_classes.clear();
    out.hs_attrs = text_attrs{};
    for (const auto& part : sr.sr_parts) {
        const auto ta = lnav::render::attrs_for_part(part);
        auto cls = intern_string_t{};
        auto rule = text_attrs{};

        switch (part.sp_kind) {
            case kind_t::role:
                cls = vc.class_for_role(part.sp_role);
                rule = vc.theme_attrs_for_role(part.sp_role);
                break;
            case kind_t::level:
                cls = vc.class_for_level(part.sp_level);
                rule = vc.theme_attrs_for_level(part.sp_level);
                break;
            default:
                break;
        }

        const auto has_class = !cls.empty();
        if (has_class) {
            out.add_class(cls);
        } else {
            out.hs_attrs.ta_attrs |= ta.ta_attrs;
        }
        fg_setters.add(ta.ta_fg_color, has_class, rule.ta_fg_color);
        bg_setters.add(ta.ta_bg_color, has_class, rule.ta_bg_color);
        if (ta.has_style(text_attrs::style::reverse)) {
            has_reverse = true;
        }
        resolved = lnav::render::apply_attrs(ta, resolved);
    }
    out.hs_attrs.clear_style(text_attrs::style::reverse);

    if (has_reverse) {
        const auto colors = resolved.has_style(text_attrs::style::reverse)
            ? reversed_colors(resolved)
            : resolved | vc.theme_attrs_for_role(role_t::VCR_TEXT);

        out.hs_attrs.ta_fg_color = colors.ta_fg_color;
        out.hs_attrs.ta_bg_color = colors.ta_bg_color;
    } else {
        if (fg_setters.needs_inline()) {
            out.hs_attrs.ta_fg_color = resolved.ta_fg_color;
        }
        if (bg_setters.needs_inline()) {
            out.hs_attrs.ta_bg_color = resolved.ta_bg_color;
        }
    }
    out.hs_attrs.ta_fg_color = vc.ansi_to_theme_rgb(out.hs_attrs.ta_fg_color);
    out.hs_attrs.ta_bg_color = vc.ansi_to_theme_rgb(out.hs_attrs.ta_bg_color);

    out.hs_href = sr.sr_href != nullptr && is_safe_href(*sr.sr_href)
        ? sr.sr_href
        : nullptr;
    out.hs_id = sr.sr_id;
    out.hs_notes = sr.sr_notes;
}

/**
 * Append the opening tag of a span for the style, if it needs one.
 *
 * @return True if a span was opened.
 */
bool
open_span(std::string& out, const html_style& hs)
{
    const auto mark = out.size();

    out.append("<span");
    if (hs.hs_id != nullptr) {
        out.append(" id=\"");
        append_escaped_attr(out, string_fragment::from_str(*hs.hs_id));
        out.append("\"");
    }
    append_title(out, hs.hs_notes);

    if (!hs.hs_classes.empty()) {
        auto sep = "";

        out.append(" class=\"");
        for (const auto& cls : hs.hs_classes) {
            out.append(sep);
            append_escaped(out, cls.to_string_fragment());
            sep = " ";
        }
        out.append("\"");
    }

    const auto style_mark = out.size();
    out.append(" style=\"");
    if (append_css_declarations(out, hs.hs_attrs)) {
        out.append("\"");
    } else {
        out.resize(style_mark);
    }

    if (out.size() == mark + strlen("<span")) {
        out.resize(mark);
        return false;
    }
    out.append(">");
    return true;
}

}  // namespace

std::string
to_html(const attr_line_t& al)
{
    std::string retval;
    html_style curr;
    html_style next;
    auto have_curr = false;
    auto span_open = false;
    const std::string* open_href = nullptr;
    // An empty line has nothing to show a tooltip for.
    const auto notes = al.empty() ? std::vector<std::string>{}
                                  : lnav::render::line_notes(al);

    retval.reserve(al.get_string().size());
    // The notes for the whole line go on a span around all of it, instead of
    // being repeated on the span for each run.
    if (!notes.empty()) {
        retval.append("<span");
        append_title(retval, notes);
        retval.append(">");
    }
    lnav::render::for_each_run(al, [&](const lnav::render::styled_run& sr) {
        to_html_style(sr, next);

        // Adjacent runs that look the same share a span, unless the new one
        // starts an anchor.
        if (!have_curr || next.hs_id != nullptr || !curr.same_style(next)) {
            if (span_open) {
                retval.append("</span>");
            }
            if (!lnav::render::same_str(next.hs_href, open_href)) {
                if (open_href != nullptr) {
                    retval.append("</a>");
                }
                if (next.hs_href != nullptr) {
                    retval.append("<a href=\"");
                    append_escaped_attr(
                        retval, string_fragment::from_str(*next.hs_href));
                    retval.append("\">");
                }
                open_href = next.hs_href;
            }
            span_open = open_span(retval, next);
            std::swap(curr, next);
            have_curr = true;
        }
        append_escaped(retval, string_fragment::from_str(sr.sr_text));
    });
    if (span_open) {
        retval.append("</span>");
    }
    if (open_href != nullptr) {
        retval.append("</a>");
    }
    if (!notes.empty()) {
        retval.append("</span>");
    }

    return retval;
}

std::string
theme_stylesheet()
{
    const auto& vc = view_colors::singleton();
    std::string retval;
    // Only the first rule for a class would be used by to_html(), so a
    // later one with the same name must not override it.
    std::unordered_set<const intern_string*> seen;

    auto append_rule = [&retval, &seen](const intern_string_t& cls,
                                        const text_attrs& ta) {
        if (cls.empty() || !seen.insert(cls.unwrap()).second) {
            return;
        }

        auto rule = ta;
        if (ta.has_style(text_attrs::style::reverse)) {
            const auto colors = reversed_colors(ta);

            rule.ta_fg_color = colors.ta_fg_color;
            rule.ta_bg_color = colors.ta_bg_color;
        }

        auto decls = std::string{};
        append_css_declarations(decls, rule);
        if (decls.empty()) {
            return;
        }
        retval.append(
            fmt::format(FMT_STRING(".{} {{ {}; }}\n"), cls.c_str(), decls));
    };

    for (int32_t role_index = 0;
         role_index < lnav::enums::to_underlying(role_t::VCR__MAX);
         role_index++)
    {
        auto role = role_t(role_index);

        append_rule(vc.class_for_role(role), vc.theme_attrs_for_role(role));
    }
    for (int level_index = 0; level_index < LEVEL__MAX; level_index++) {
        auto level = static_cast<log_level_t>(level_index);

        append_rule(vc.class_for_level(level),
                    vc.theme_attrs_for_level(level));
    }
    return retval;
}

}  // namespace lnav::html
