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

#ifndef lnav_string_attr_type_hh
#define lnav_string_attr_type_hh

#include <string>
#include <utility>

#include <stdint.h>

#include "base/intern_string.hh"
#include "mapbox/variant.hpp"

class logfile;
struct bookmark_metadata;

enum class ui_icon_t : int32_t {
    hidden,
};

/** Roles that can be mapped to curses attributes using attrs_for_role() */
enum class role_t : int32_t {
    VCR_NONE = -1,

    VCR_TEXT, /*< Raw text. */
    VCR_IDENTIFIER,
    VCR_SEARCH, /*< A search hit. */
    VCR_OK,
    VCR_INFO,
    VCR_ERROR, /*< An error message. */
    VCR_WARNING, /*< A warning message. */
    VCR_ALT_ROW, /*< Highlight for alternating rows in a list */
    VCR_HIDDEN,
    VCR_CURSOR_LINE,
    VCR_DISABLED_CURSOR_LINE,
    VCR_ADJUSTED_TIME,
    VCR_SKEWED_TIME,
    VCR_OFFSET_TIME,
    VCR_FILE_OFFSET,
    VCR_INVALID_MSG,
    VCR_STATUS, /*< Normal status line text. */
    VCR_WARN_STATUS,
    VCR_ALERT_STATUS, /*< Alert status line text. */
    VCR_ACTIVE_STATUS, /*< */
    VCR_ACTIVE_STATUS2, /*< */
    VCR_STATUS_TITLE,
    VCR_STATUS_SUBTITLE,
    VCR_STATUS_INFO,
    VCR_STATUS_STITCH_TITLE_TO_SUB,
    VCR_STATUS_STITCH_SUB_TO_TITLE,
    VCR_STATUS_STITCH_SUB_TO_NORMAL,
    VCR_STATUS_STITCH_NORMAL_TO_SUB,
    VCR_STATUS_STITCH_TITLE_TO_NORMAL,
    VCR_STATUS_STITCH_NORMAL_TO_TITLE,
    VCR_STATUS_TITLE_HOTKEY,
    VCR_STATUS_DISABLED_TITLE,
    VCR_STATUS_HOTKEY,
    VCR_INACTIVE_STATUS,
    VCR_INACTIVE_ALERT_STATUS,
    VCR_SCROLLBAR,
    VCR_SCROLLBAR_ERROR,
    VCR_SCROLLBAR_WARNING,
    VCR_FOCUSED,
    VCR_DISABLED_FOCUSED,
    VCR_POPUP,
    VCR_COLOR_HINT,

    VCR_QUOTED_CODE,
    VCR_CODE_BORDER,
    VCR_KEYWORD,
    VCR_STRING,
    VCR_COMMENT,
    VCR_DOC_DIRECTIVE,
    VCR_VARIABLE,
    VCR_SYMBOL,
    VCR_NULL,
    VCR_ASCII_CTRL,
    VCR_NON_ASCII,
    VCR_NUMBER,
    VCR_RE_SPECIAL,
    VCR_RE_REPEAT,
    VCR_FILE,

    VCR_DIFF_DELETE, /*< Deleted line in a diff. */
    VCR_DIFF_ADD, /*< Added line in a diff. */
    VCR_DIFF_SECTION, /*< Section marker in a diff. */

    VCR_LOW_THRESHOLD,
    VCR_MED_THRESHOLD,
    VCR_HIGH_THRESHOLD,

    VCR_H1,
    VCR_H2,
    VCR_H3,
    VCR_H4,
    VCR_H5,
    VCR_H6,

    VCR_HR,
    VCR_HYPERLINK,
    VCR_LIST_GLYPH,
    VCR_BREADCRUMB,
    VCR_TABLE_BORDER,
    VCR_TABLE_HEADER,
    VCR_QUOTE_BORDER,
    VCR_QUOTED_TEXT,
    VCR_FOOTNOTE_BORDER,
    VCR_FOOTNOTE_TEXT,
    VCR_SNIPPET_BORDER,
    VCR_INDENT_GUIDE,
    VCR_INLINE_CODE,
    VCR_FUNCTION,
    VCR_TYPE,
    VCR_SEP_REF_ACC,
    VCR_SUGGESTION,
    VCR_SELECTED_TEXT,

    VCR__MAX
};

struct text_attrs {
    bool empty() const
    {
        return this->ta_attrs == 0 && !this->ta_fg_color && !this->ta_bg_color;
    }

    text_attrs operator|(const text_attrs& other) const
    {
        return text_attrs{
            this->ta_attrs | other.ta_attrs,
            this->ta_fg_color ? this->ta_fg_color : other.ta_fg_color,
            this->ta_bg_color ? this->ta_bg_color : other.ta_bg_color,
        };
    }

    bool operator==(const text_attrs& other) const
    {
        return this->ta_attrs == other.ta_attrs
            && this->ta_fg_color == other.ta_fg_color
            && this->ta_bg_color == other.ta_bg_color;
    }

    int32_t ta_attrs{0};
    std::optional<short> ta_fg_color;
    std::optional<short> ta_bg_color;
};

struct block_elem_t {
    wchar_t value;
    role_t role;
};

using string_attr_value = mapbox::util::variant<int64_t,
                                                role_t,
                                                text_attrs,
                                                const intern_string_t,
                                                std::string,
                                                std::shared_ptr<logfile>,
                                                bookmark_metadata*,
                                                timespec,
                                                string_fragment,
                                                block_elem_t,
                                                ui_icon_t>;

class string_attr_type_base {
public:
    explicit string_attr_type_base(const char* name) noexcept : sat_name(name)
    {
    }

    const char* const sat_name;
};

using string_attr_pair
    = std::pair<const string_attr_type_base*, string_attr_value>;

template<typename T>
class string_attr_type : public string_attr_type_base {
public:
    using value_type = T;

    explicit string_attr_type(const char* name) noexcept
        : string_attr_type_base(name)
    {
    }

    template<typename U = T>
    std::enable_if_t<!std::is_void_v<U>, string_attr_pair> value(
        const U& val) const
    {
        return std::make_pair(this, val);
    }

    template<typename U = T>
    std::enable_if_t<std::is_void_v<U>, string_attr_pair> value() const
    {
        return std::make_pair(this, string_attr_value{});
    }
};

extern string_attr_type<void> SA_ORIGINAL_LINE;
extern string_attr_type<void> SA_BODY;
extern string_attr_type<void> SA_HIDDEN;
extern string_attr_type<const intern_string_t> SA_FORMAT;
extern string_attr_type<void> SA_REMOVED;
extern string_attr_type<void> SA_PREFORMATTED;
extern string_attr_type<std::string> SA_INVALID;
extern string_attr_type<std::string> SA_ERROR;
extern string_attr_type<int64_t> SA_LEVEL;
extern string_attr_type<int64_t> SA_ORIGIN_OFFSET;

extern string_attr_type<role_t> VC_ROLE;
extern string_attr_type<role_t> VC_ROLE_FG;
extern string_attr_type<text_attrs> VC_STYLE;
extern string_attr_type<int64_t> VC_GRAPHIC;
extern string_attr_type<block_elem_t> VC_BLOCK_ELEM;
extern string_attr_type<int64_t> VC_FOREGROUND;
extern string_attr_type<int64_t> VC_BACKGROUND;
extern string_attr_type<std::string> VC_HYPERLINK;
extern string_attr_type<ui_icon_t> VC_ICON;

namespace lnav {

namespace string {
namespace attrs {

template<typename S>
inline std::pair<S, string_attr_pair>
preformatted(S str)
{
    return std::make_pair(std::move(str), SA_PREFORMATTED.value());
}

template<typename S>
inline std::pair<S, string_attr_pair>
href(S str, std::string href)
{
    return std::make_pair(std::move(str), VC_HYPERLINK.value(std::move(href)));
}

}  // namespace attrs
}  // namespace string

namespace roles {

template<typename S>
inline std::pair<S, string_attr_pair>
error(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_ERROR));
}

template<typename S>
inline std::pair<S, string_attr_pair>
warning(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_WARNING));
}

template<typename S>
inline std::pair<S, string_attr_pair>
status(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_STATUS));
}

template<typename S>
inline std::pair<S, string_attr_pair>
inactive_status(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_INACTIVE_STATUS));
}

template<typename S>
inline std::pair<S, string_attr_pair>
status_title(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_STATUS_TITLE));
}

template<typename S>
inline std::pair<S, string_attr_pair>
ok(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_OK));
}

template<typename S>
inline std::pair<S, string_attr_pair>
hidden(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_HIDDEN));
}

template<typename S>
inline std::pair<S, string_attr_pair>
file(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_FILE));
}

template<typename S>
inline std::pair<S, string_attr_pair>
symbol(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_SYMBOL));
}

template<typename S>
inline std::pair<S, string_attr_pair>
keyword(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_KEYWORD));
}

template<typename S>
inline std::pair<S, string_attr_pair>
variable(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_VARIABLE));
}

template<typename S>
inline std::pair<S, string_attr_pair>
number(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_NUMBER));
}

template<typename S>
inline std::pair<S, string_attr_pair>
comment(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_COMMENT));
}

template<typename S>
inline std::pair<S, string_attr_pair>
identifier(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_IDENTIFIER));
}

template<typename S>
inline std::pair<S, string_attr_pair>
string(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_STRING));
}

template<typename S>
inline std::pair<S, string_attr_pair>
hr(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_HR));
}

template<typename S>
inline std::pair<S, string_attr_pair>
hyperlink(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_HYPERLINK));
}

template<typename S>
inline std::pair<S, string_attr_pair>
list_glyph(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_LIST_GLYPH));
}

template<typename S>
inline std::pair<S, string_attr_pair>
breadcrumb(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_BREADCRUMB));
}

template<typename S>
inline std::pair<S, string_attr_pair>
quoted_code(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_QUOTED_CODE));
}

template<typename S>
inline std::pair<S, string_attr_pair>
code_border(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_CODE_BORDER));
}

template<typename S>
inline std::pair<S, string_attr_pair>
snippet_border(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_SNIPPET_BORDER));
}

template<typename S>
inline std::pair<S, string_attr_pair>
table_border(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_TABLE_BORDER));
}

template<typename S>
inline std::pair<S, string_attr_pair>
table_header(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_TABLE_HEADER));
}

template<typename S>
inline std::pair<S, string_attr_pair>
quote_border(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_QUOTE_BORDER));
}

template<typename S>
inline std::pair<S, string_attr_pair>
quoted_text(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_QUOTED_TEXT));
}

template<typename S>
inline std::pair<S, string_attr_pair>
footnote_border(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_FOOTNOTE_BORDER));
}

template<typename S>
inline std::pair<S, string_attr_pair>
footnote_text(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_FOOTNOTE_TEXT));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h1(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_H1));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h2(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_H2));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h3(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_H3));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h4(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_H4));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h5(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_H5));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h6(S str)
{
    return std::make_pair(std::move(str), VC_ROLE.value(role_t::VCR_H6));
}

template<typename S>
inline std::pair<S, string_attr_pair>
suggestion(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.value(role_t::VCR_SUGGESTION));
}

namespace literals {

inline std::pair<std::string, string_attr_pair> operator"" _ok(const char* str,
                                                               std::size_t len)
{
    return std::make_pair(std::string(str, len), VC_ROLE.value(role_t::VCR_OK));
}

inline std::pair<std::string, string_attr_pair> operator"" _error(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_ERROR));
}

inline std::pair<std::string, string_attr_pair> operator"" _warning(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_WARNING));
}

inline std::pair<std::string, string_attr_pair> operator"" _info(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_INFO));
}

inline std::pair<std::string, string_attr_pair> operator"" _status_title(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_STATUS_TITLE));
}

inline std::pair<std::string, string_attr_pair> operator"" _symbol(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_SYMBOL));
}

inline std::pair<std::string, string_attr_pair> operator"" _keyword(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_KEYWORD));
}

inline std::pair<std::string, string_attr_pair> operator"" _variable(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_VARIABLE));
}

inline std::pair<std::string, string_attr_pair> operator"" _comment(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_COMMENT));
}

inline std::pair<std::string, string_attr_pair> operator"" _hotkey(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_STATUS_HOTKEY));
}

inline std::pair<std::string, string_attr_pair> operator"" _h1(const char* str,
                                                               std::size_t len)
{
    return std::make_pair(std::string(str, len), VC_ROLE.value(role_t::VCR_H1));
}

inline std::pair<std::string, string_attr_pair> operator"" _h2(const char* str,
                                                               std::size_t len)
{
    return std::make_pair(std::string(str, len), VC_ROLE.value(role_t::VCR_H2));
}

inline std::pair<std::string, string_attr_pair> operator"" _h3(const char* str,
                                                               std::size_t len)
{
    return std::make_pair(std::string(str, len), VC_ROLE.value(role_t::VCR_H3));
}

inline std::pair<std::string, string_attr_pair> operator"" _h4(const char* str,
                                                               std::size_t len)
{
    return std::make_pair(std::string(str, len), VC_ROLE.value(role_t::VCR_H4));
}

inline std::pair<std::string, string_attr_pair> operator"" _h5(const char* str,
                                                               std::size_t len)
{
    return std::make_pair(std::string(str, len), VC_ROLE.value(role_t::VCR_H5));
}

inline std::pair<std::string, string_attr_pair> operator"" _hr(const char* str,
                                                               std::size_t len)
{
    return std::make_pair(std::string(str, len), VC_ROLE.value(role_t::VCR_HR));
}

inline std::pair<std::string, string_attr_pair> operator"" _hyperlink(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_HYPERLINK));
}

inline std::pair<std::string, string_attr_pair> operator"" _list_glyph(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_LIST_GLYPH));
}

inline std::pair<std::string, string_attr_pair> operator"" _breadcrumb(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_BREADCRUMB));
}

inline std::pair<std::string, string_attr_pair> operator"" _quoted_code(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_QUOTED_CODE));
}

inline std::pair<std::string, string_attr_pair> operator"" _code_border(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_CODE_BORDER));
}

inline std::pair<std::string, string_attr_pair> operator"" _table_header(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_TABLE_HEADER));
}

inline std::pair<std::string, string_attr_pair> operator"" _table_border(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_TABLE_BORDER));
}

inline std::pair<std::string, string_attr_pair> operator"" _quote_border(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_QUOTE_BORDER));
}

inline std::pair<std::string, string_attr_pair> operator"" _quoted_text(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_QUOTED_TEXT));
}

inline std::pair<std::string, string_attr_pair> operator"" _footnote_border(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_FOOTNOTE_BORDER));
}

inline std::pair<std::string, string_attr_pair> operator"" _footnote_text(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_FOOTNOTE_BORDER));
}

inline std::pair<std::string, string_attr_pair> operator"" _snippet_border(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.value(role_t::VCR_SNIPPET_BORDER));
}

inline std::pair<std::string, string_attr_pair> operator"" _link(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_HYPERLINK.value(std::string(str, len)));
}

}  // namespace literals

}  // namespace roles
}  // namespace lnav

#endif
