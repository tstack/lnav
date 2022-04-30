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

/** Roles that can be mapped to curses attributes using attrs_for_role() */
enum class role_t : int32_t {
    VCR_NONE = -1,

    VCR_TEXT, /*< Raw text. */
    VCR_IDENTIFIER,
    VCR_SEARCH, /*< A search hit. */
    VCR_OK,
    VCR_ERROR, /*< An error message. */
    VCR_WARNING, /*< A warning message. */
    VCR_ALT_ROW, /*< Highlight for alternating rows in a list */
    VCR_HIDDEN,
    VCR_ADJUSTED_TIME,
    VCR_SKEWED_TIME,
    VCR_OFFSET_TIME,
    VCR_INVALID_MSG,
    VCR_STATUS, /*< Normal status line text. */
    VCR_WARN_STATUS,
    VCR_ALERT_STATUS, /*< Alert status line text. */
    VCR_ACTIVE_STATUS, /*< */
    VCR_ACTIVE_STATUS2, /*< */
    VCR_STATUS_TITLE,
    VCR_STATUS_SUBTITLE,
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

    VCR_KEYWORD,
    VCR_STRING,
    VCR_COMMENT,
    VCR_DOC_DIRECTIVE,
    VCR_VARIABLE,
    VCR_SYMBOL,
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

    VCR_LIST_GLYPH,

    VCR__MAX
};

using string_attr_value = mapbox::util::variant<int64_t,
                                                role_t,
                                                const intern_string_t,
                                                std::string,
                                                std::shared_ptr<logfile>,
                                                bookmark_metadata*>;

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
    std::enable_if_t<!std::is_void<U>::value, string_attr_pair> value(
        const U& val) const
    {
        return std::make_pair(this, val);
    }

    template<typename U = T>
    std::enable_if_t<std::is_void<U>::value, string_attr_pair> value() const
    {
        return std::make_pair(this, string_attr_value{});
    }
};

extern string_attr_type<void> SA_ORIGINAL_LINE;
extern string_attr_type<void> SA_BODY;
extern string_attr_type<void> SA_HIDDEN;
extern string_attr_type<const intern_string_t> SA_FORMAT;
extern string_attr_type<void> SA_REMOVED;
extern string_attr_type<std::string> SA_INVALID;
extern string_attr_type<std::string> SA_ERROR;
extern string_attr_type<int64_t> SA_LEVEL;

extern string_attr_type<role_t> VC_ROLE;
extern string_attr_type<role_t> VC_ROLE_FG;
extern string_attr_type<int64_t> VC_STYLE;
extern string_attr_type<int64_t> VC_GRAPHIC;
extern string_attr_type<int64_t> VC_FOREGROUND;
extern string_attr_type<int64_t> VC_BACKGROUND;

namespace lnav {
namespace roles {

template<typename S>
inline std::pair<S, string_attr_pair>
error(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_ERROR));
}

template<typename S>
inline std::pair<S, string_attr_pair>
warning(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_WARNING));
}

template<typename S>
inline std::pair<S, string_attr_pair>
status(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_STATUS));
}

template<typename S>
inline std::pair<S, string_attr_pair>
ok(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_OK));
}

template<typename S>
inline std::pair<S, string_attr_pair>
file(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_FILE));
}

template<typename S>
inline std::pair<S, string_attr_pair>
symbol(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_SYMBOL));
}

template<typename S>
inline std::pair<S, string_attr_pair>
keyword(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_KEYWORD));
}

template<typename S>
inline std::pair<S, string_attr_pair>
variable(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_VARIABLE));
}

template<typename S>
inline std::pair<S, string_attr_pair>
number(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_NUMBER));
}

template<typename S>
inline std::pair<S, string_attr_pair>
comment(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_COMMENT));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h1(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_H1));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h2(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_H2));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h3(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_H3));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h4(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_H4));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h5(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_H5));
}

template<typename S>
inline std::pair<S, string_attr_pair>
h6(S str)
{
    return std::make_pair(std::move(str),
                          VC_ROLE.template value(role_t::VCR_H6));
}

namespace literals {

inline std::pair<std::string, string_attr_pair> operator"" _symbol(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.template value(role_t::VCR_SYMBOL));
}

inline std::pair<std::string, string_attr_pair> operator"" _keyword(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.template value(role_t::VCR_KEYWORD));
}

inline std::pair<std::string, string_attr_pair> operator"" _variable(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.template value(role_t::VCR_VARIABLE));
}

inline std::pair<std::string, string_attr_pair> operator"" _comment(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.template value(role_t::VCR_COMMENT));
}

inline std::pair<std::string, string_attr_pair> operator"" _h1(const char* str,
                                                               std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.template value(role_t::VCR_H1));
}

inline std::pair<std::string, string_attr_pair> operator"" _h2(const char* str,
                                                               std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.template value(role_t::VCR_H2));
}

inline std::pair<std::string, string_attr_pair> operator"" _h3(const char* str,
                                                               std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.template value(role_t::VCR_H3));
}

inline std::pair<std::string, string_attr_pair> operator"" _list_glyph(
    const char* str, std::size_t len)
{
    return std::make_pair(std::string(str, len),
                          VC_ROLE.template value(role_t::VCR_LIST_GLYPH));
}

}  // namespace literals

}  // namespace roles
}  // namespace lnav

#endif
