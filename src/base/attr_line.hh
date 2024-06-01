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
 *
 * @file attr_line.hh
 */

#ifndef attr_line_hh
#define attr_line_hh

#include <new>
#include <optional>
#include <string>
#include <vector>

#include <limits.h>

#include "fmt/format.h"
#include "intern_string.hh"
#include "line_range.hh"
#include "string_attr_type.hh"
#include "string_util.hh"

inline line_range
to_line_range(const string_fragment& frag)
{
    return line_range{frag.sf_begin, frag.sf_end};
}

struct string_attr {
    string_attr(const struct line_range& lr, const string_attr_pair& value)
        : sa_range(lr), sa_type(value.first), sa_value(value.second)
    {
    }

    string_attr() = default;

    bool operator<(const struct string_attr& rhs) const
    {
        if (this->sa_range < rhs.sa_range) {
            return true;
        }
        if (this->sa_range == rhs.sa_range && this->sa_type == rhs.sa_type
            && this->sa_type == &VC_ROLE
            && this->sa_value.get<role_t>() < rhs.sa_value.get<role_t>())
        {
            return true;
        }

        return false;
    }

    struct line_range sa_range;
    const string_attr_type_base* sa_type{nullptr};
    string_attr_value sa_value;
};

template<typename T>
struct string_attr_wrapper {
    explicit string_attr_wrapper(const string_attr* sa) : saw_string_attr(sa) {}

    template<typename U = T>
    std::enable_if_t<!std::is_void<U>::value, const U&> get() const
    {
        return this->saw_string_attr->sa_value.template get<T>();
    }

    const string_attr* saw_string_attr;
};

/** A map of line ranges to attributes for that range. */
using string_attrs_t = std::vector<string_attr>;

string_attrs_t::const_iterator find_string_attr(
    const string_attrs_t& sa, const string_attr_type_base* type, int start = 0);

std::optional<const string_attr*> get_string_attr(
    const string_attrs_t& sa, const string_attr_type_base* type, int start = 0);

template<typename T>
inline std::optional<string_attr_wrapper<T>>
get_string_attr(const string_attrs_t& sa,
                const string_attr_type<T>& type,
                int start = 0)
{
    auto iter = find_string_attr(sa, &type, start);

    if (iter == sa.end()) {
        return std::nullopt;
    }

    return std::make_optional(string_attr_wrapper<T>(&(*iter)));
}

template<typename T>
inline string_attrs_t::const_iterator
find_string_attr_containing(const string_attrs_t& sa,
                            const string_attr_type_base* type,
                            T x)
{
    string_attrs_t::const_iterator iter;

    for (iter = sa.begin(); iter != sa.end(); ++iter) {
        if (iter->sa_type == type && iter->sa_range.contains(x)) {
            break;
        }
    }

    return iter;
}

string_attrs_t::iterator find_string_attr(string_attrs_t& sa,
                                          const struct line_range& lr);

string_attrs_t::const_iterator find_string_attr(const string_attrs_t& sa,
                                                size_t near);

template<typename T>
inline string_attrs_t::const_iterator
rfind_string_attr_if(const string_attrs_t& sa, ssize_t near, T predicate)
{
    auto nearest = sa.end();
    ssize_t last_diff = INT_MAX;

    for (auto iter = sa.begin(); iter != sa.end(); ++iter) {
        const auto& lr = iter->sa_range;

        if (lr.lr_start > near) {
            continue;
        }

        if (!predicate(*iter)) {
            continue;
        }

        ssize_t diff = near - lr.lr_start;
        if (diff < last_diff) {
            last_diff = diff;
            nearest = iter;
        }
    }

    return nearest;
}

struct line_range find_string_attr_range(const string_attrs_t& sa,
                                         string_attr_type_base* type);

void remove_string_attr(string_attrs_t& sa, const struct line_range& lr);

void remove_string_attr(string_attrs_t& sa, string_attr_type_base* type);

void shift_string_attrs(string_attrs_t& sa, int32_t start, int32_t amount);

void shift_string_attrs(string_attrs_t& sa,
                        const line_range& cover,
                        int32_t amount);

struct text_wrap_settings {
    text_wrap_settings& with_indent(int indent)
    {
        this->tws_indent = indent;
        return *this;
    }

    text_wrap_settings& with_padding_indent(int indent)
    {
        this->tws_padding_indent = indent;
        return *this;
    }

    text_wrap_settings& with_width(int width)
    {
        this->tws_width = width;
        return *this;
    }

    int tws_indent{2};
    int tws_width{80};
    int tws_padding_indent{0};
};

/**
 * A line that has attributes.
 */
class attr_line_t {
public:
    attr_line_t() = default;

    attr_line_t(std::string str) : al_string(std::move(str)) {}

    attr_line_t(const char* str) : al_string(str) {}

    static inline attr_line_t from_ansi_str(const char* str)
    {
        attr_line_t retval;

        return retval.with_ansi_string("%s", str);
    }

    static inline attr_line_t from_ansi_str(const std::string& str)
    {
        attr_line_t retval;

        return retval.with_ansi_string(str);
    }

    /** @return The string itself. */
    std::string& get_string() { return this->al_string; }

    const std::string& get_string() const { return this->al_string; }

    /** @return The attributes for the string. */
    string_attrs_t& get_attrs() { return this->al_attrs; }

    const string_attrs_t& get_attrs() const { return this->al_attrs; }

    attr_line_t& with_string(const std::string& str)
    {
        this->al_string = str;
        return *this;
    }

    attr_line_t& with_ansi_string(const char* str, ...);

    attr_line_t& with_ansi_string(const std::string& str);

    attr_line_t& with_attr(const string_attr& sa)
    {
        this->al_attrs.push_back(sa);
        return *this;
    }

    attr_line_t& ensure_space()
    {
        if (!this->al_string.empty() && this->al_string.back() != ' '
            && this->al_string.back() != '[')
        {
            this->append(1, ' ');
        }

        return *this;
    }

    template<typename S>
    attr_line_t& append(S str, const string_attr_pair& value)
    {
        size_t start_len = this->al_string.length();

        this->al_string.append(str);

        line_range lr{(int) start_len, (int) this->al_string.length()};

        this->al_attrs.emplace_back(lr, value);

        return *this;
    }

    template<typename S>
    attr_line_t& append(const std::pair<S, string_attr_pair>& value)
    {
        size_t start_len = this->al_string.length();

        this->al_string.append(std::move(value.first));

        line_range lr{(int) start_len, (int) this->al_string.length()};

        this->al_attrs.emplace_back(lr, value.second);

        return *this;
    }

    template<typename S>
    attr_line_t& append_quoted(const std::pair<S, string_attr_pair>& value)
    {
        this->al_string.append("\u201c");

        size_t start_len = this->al_string.length();

        this->append(std::move(value.first));

        line_range lr{(int) start_len, (int) this->al_string.length()};

        this->al_attrs.emplace_back(lr, value.second);

        this->al_string.append("\u201d");

        return *this;
    }

    attr_line_t& append_quoted(const intern_string_t str)
    {
        this->al_string.append("\u201c");
        this->al_string.append(str.get(), str.size());
        this->al_string.append("\u201d");

        return *this;
    }

    template<typename S>
    attr_line_t& append_quoted(S s)
    {
        this->al_string.append("\u201c");
        this->append(std::move(s));
        this->al_string.append("\u201d");

        return *this;
    }

    attr_line_t& append(const intern_string_t str)
    {
        this->al_string.append(str.get(), str.size());
        return *this;
    }

    attr_line_t& append(const string_fragment& str)
    {
        this->al_string.append(str.data(), str.length());
        return *this;
    }

    attr_line_t& append(const std::string& str)
    {
        this->al_string.append(str);
        return *this;
    }

    attr_line_t& append(const char* str)
    {
        this->al_string.append(str);
        return *this;
    }

    template<typename V>
    attr_line_t& append(const V& v)
    {
        this->al_string.append(fmt::to_string(v));
        return *this;
    }

    template<typename... Args>
    attr_line_t& appendf(fmt::format_string<Args...> fstr, Args&&... args)
    {
        fmt::vformat_to(std::back_inserter(this->al_string),
                        fstr,
                        fmt::make_format_args(args...));
        return *this;
    }

    attr_line_t& with_attr_for_all(const string_attr_pair& sap)
    {
        this->al_attrs.emplace_back(line_range{0, -1}, sap);
        return *this;
    }

    template<typename C, typename F>
    attr_line_t& join(const C& container,
                      const string_attr_pair& sap,
                      const F& fill)
    {
        bool init = true;
        for (const auto& elem : container) {
            if (!init) {
                this->append(fill);
            }
            this->append(std::make_pair(elem, sap));
            init = false;
        }

        return *this;
    }

    template<typename C, typename F>
    attr_line_t& join(const C& container, const F& fill)
    {
        bool init = true;
        for (const auto& elem : container) {
            if (!init) {
                this->append(fill);
            }
            this->append(elem);
            init = false;
        }

        return *this;
    }

    attr_line_t& insert(size_t index,
                        const attr_line_t& al,
                        text_wrap_settings* tws = nullptr);

    attr_line_t& append(const attr_line_t& al,
                        text_wrap_settings* tws = nullptr)
    {
        return this->insert(this->al_string.length(), al, tws);
    }

    attr_line_t& append(size_t len, char c)
    {
        this->al_string.append(len, c);
        return *this;
    }

    attr_line_t& insert(size_t index, size_t len, char c)
    {
        this->al_string.insert(index, len, c);

        shift_string_attrs(this->al_attrs, index, len);

        return *this;
    }

    attr_line_t& insert(size_t index, const char* str)
    {
        this->al_string.insert(index, str);

        shift_string_attrs(this->al_attrs, index, strlen(str));

        return *this;
    }

    template<typename S>
    attr_line_t& insert(size_t index,
                        const std::pair<S, string_attr_pair>& value)
    {
        size_t start_len = this->al_string.length();

        this->insert(index, std::move(value.first));

        line_range lr{
            (int) index,
            (int) (index + (this->al_string.length() - start_len)),
        };

        this->al_attrs.emplace_back(lr, value.second);

        return *this;
    }

    template<typename... Args>
    attr_line_t& add_header(Args... args)
    {
        if (!this->blank()) {
            this->insert(0, args...);
        }
        return *this;
    }

    template<typename... Args>
    attr_line_t& with_default(Args... args)
    {
        if (this->blank()) {
            this->clear();
            this->append(args...);
        }

        return *this;
    }

    attr_line_t& erase(size_t pos, size_t len = std::string::npos);

    attr_line_t& rtrim(std::optional<const char*> chars = std::nullopt);

    attr_line_t& erase_utf8_chars(size_t start)
    {
        auto byte_index = utf8_char_to_byte_index(this->al_string, start);
        this->erase(byte_index);

        return *this;
    }

    attr_line_t& right_justify(unsigned long width);

    attr_line_t& pad_to(ssize_t size);

    ssize_t length() const
    {
        size_t retval = this->al_string.length();

        for (const auto& al_attr : this->al_attrs) {
            retval = std::max(retval, (size_t) al_attr.sa_range.lr_start);
            if (al_attr.sa_range.lr_end != -1) {
                retval = std::max(retval, (size_t) al_attr.sa_range.lr_end);
            }
        }

        return retval;
    }

    Result<size_t, const char*> utf8_length() const
    {
        return utf8_string_length(this->al_string);
    }

    ssize_t utf8_length_or_length() const
    {
        return utf8_string_length(this->al_string).unwrapOr(this->length());
    }

    size_t column_width() const
    {
        return string_fragment::from_str(this->al_string).column_width();
    }

    std::string get_substring(const line_range& lr) const
    {
        if (!lr.is_valid()) {
            return "";
        }
        return this->al_string.substr(lr.lr_start, lr.sublen(this->al_string));
    }

    string_fragment to_string_fragment(
        string_attrs_t::const_iterator iter) const
    {
        return string_fragment(this->al_string.c_str(),
                               iter->sa_range.lr_start,
                               iter->sa_range.end_for_string(this->al_string));
    }

    string_attrs_t::const_iterator find_attr(size_t near) const
    {
        near = std::min(near, this->al_string.length() - 1);

        while (near > 0 && isspace(this->al_string[near])) {
            near -= 1;
        }

        return find_string_attr(this->al_attrs, near);
    }

    bool empty() const { return this->length() == 0; }

    bool blank() const { return is_blank(this->al_string); }

    /** Clear the string and the attributes for the string. */
    attr_line_t& clear()
    {
        this->al_string.clear();
        this->al_attrs.clear();

        return *this;
    }

    attr_line_t subline(size_t start, size_t len = std::string::npos) const;

    void split_lines(std::vector<attr_line_t>& lines) const;

    std::vector<attr_line_t> split_lines() const
    {
        std::vector<attr_line_t> retval;

        this->split_lines(retval);
        return retval;
    }

    size_t nearest_text(size_t x) const;

    attr_line_t& wrap_with(text_wrap_settings* tws);

    void apply_hide();

    std::string al_string;
    string_attrs_t al_attrs;
};

#endif
