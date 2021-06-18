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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file attr_line.hh
 */

#ifndef attr_line_hh
#define attr_line_hh

#include <limits.h>

#include <string>
#include <vector>

#include "base/lnav_log.hh"
#include "base/string_util.hh"
#include "base/intern_string.hh"
#include "string_attr_type.hh"

/**
 * Encapsulates a range in a string.
 */
struct line_range {
    int lr_start;
    int lr_end;

    explicit line_range(int start = -1, int end = -1) : lr_start(start), lr_end(end) { };

    bool is_valid() const {
        return this->lr_start != -1;
    }

    int length() const
    {
        return this->lr_end == -1 ? INT_MAX : this->lr_end - this->lr_start;
    };

    bool contains(int pos) const {
        return this->lr_start <= pos && pos < this->lr_end;
    };

    bool contains(const struct line_range &other) const {
        return this->contains(other.lr_start) && other.lr_end <= this->lr_end;
    };

    bool intersects(const struct line_range &other) const {
        return this->contains(other.lr_start) || this->contains(other.lr_end);
    };

    line_range intersection(const struct line_range &other) const {
        int actual_end;

        if (this->lr_end == -1) {
            actual_end = other.lr_end;
        } else if (other.lr_end == -1) {
            actual_end = this->lr_end;
        } else {
            actual_end = std::min(this->lr_end, other.lr_end);
        }
        return line_range{std::max(this->lr_start, other.lr_start), actual_end};
    };

    line_range &shift(int32_t start, int32_t amount) {
        if (this->lr_start >= start) {
            this->lr_start = std::max(0, this->lr_start + amount);
        }
        if (this->lr_end != -1 && start <= this->lr_end) {
            this->lr_end += amount;
            if (this->lr_end < this->lr_start) {
                this->lr_end = this->lr_start;
            }
        }

        return *this;
    };

    void ltrim(const char *str) {
        while (this->lr_start < this->lr_end && isspace(str[this->lr_start])) {
            this->lr_start += 1;
        }
    };

    bool operator<(const struct line_range &rhs) const
    {
        if (this->lr_start < rhs.lr_start) { return true; }
        else if (this->lr_start > rhs.lr_start) { return false; }

        if (this->lr_end == rhs.lr_end) { return false; }

        if (this->lr_end < rhs.lr_end) { return true; }
        return false;
    };

    bool operator==(const struct line_range &rhs) const {
        return (this->lr_start == rhs.lr_start && this->lr_end == rhs.lr_end);
    };

    const char *substr(const std::string &str) const {
        if (this->lr_start == -1) {
            return str.c_str();
        }
        return &(str.c_str()[this->lr_start]);
    }

    size_t sublen(const std::string &str) const {
        if (this->lr_start == -1) {
            return str.length();
        }
        if (this->lr_end == -1) {
            return str.length() - this->lr_start;
        }
        return this->length();
    }
};

/**
 * Container for attribute values for a substring.
 */
typedef union {
    const void *sav_ptr;
    int64_t sav_int;
} string_attr_value_t;

struct string_attr {
    string_attr(const struct line_range &lr, string_attr_type_t type, void *val)
        : sa_range(lr), sa_type(type) {
        require(lr.is_valid());
        require(type);
        this->sa_value.sav_ptr = val;
    };

    string_attr(const struct line_range &lr, string_attr_type_t type, std::string val)
        : sa_range(lr), sa_type(type), sa_str_value(std::move(val)) {
        require(lr.is_valid());
        require(type);
    };

    string_attr(const struct line_range &lr, string_attr_type_t type, intern_string_t val)
        : sa_range(lr), sa_type(type) {
        require(lr.is_valid());
        require(type);
        this->sa_value.sav_ptr = val.unwrap();
    };

    string_attr(const struct line_range &lr, string_attr_type_t type, int64_t val = 0)
        : sa_range(lr), sa_type(type) {
        require(lr.is_valid());
        require(type);
        this->sa_value.sav_int = val;
    };

    string_attr(const struct line_range &lr, string_attr_type_t type, string_attr_value_t val)
        : sa_range(lr), sa_type(type), sa_value(val) {
        require(lr.is_valid());
        require(type);
    };

    string_attr() : sa_type(nullptr) { };

    bool operator<(const struct string_attr &rhs) const
    {
        return this->sa_range < rhs.sa_range;
    };

    intern_string_t to_string() const {
        return intern_string_t((const intern_string *) this->sa_value.sav_ptr);
    };

    struct line_range sa_range;
    string_attr_type_t sa_type;
    string_attr_value_t sa_value;
    std::string sa_str_value;
};

/** A map of line ranges to attributes for that range. */
typedef std::vector<string_attr> string_attrs_t;

inline string_attrs_t::const_iterator
find_string_attr(const string_attrs_t &sa, string_attr_type_t type, int start = 0)
{
    string_attrs_t::const_iterator iter;

    for (iter = sa.begin(); iter != sa.end(); ++iter) {
        if (iter->sa_type == type && iter->sa_range.lr_start >= start) {
            break;
        }
    }

    return iter;
}

inline nonstd::optional<const string_attr*>
get_string_attr(const string_attrs_t &sa, string_attr_type_t type, int start = 0)
{
    auto iter = find_string_attr(sa, type, start);

    if (iter == sa.end()) {
        return nonstd::nullopt;
    }

    return nonstd::make_optional(&(*iter));
}

template<typename T>
inline string_attrs_t::const_iterator
find_string_attr_containing(const string_attrs_t &sa, string_attr_type_t type, T x)
{
    string_attrs_t::const_iterator iter;

    for (iter = sa.begin(); iter != sa.end(); ++iter) {
        if (iter->sa_type == type && iter->sa_range.contains(x)) {
            break;
        }
    }

    return iter;
}

inline string_attrs_t::iterator
find_string_attr(string_attrs_t &sa, const struct line_range &lr)
{
    string_attrs_t::iterator iter;

    for (iter = sa.begin(); iter != sa.end(); ++iter) {
        if (lr.contains(iter->sa_range)) {
            break;
        }
    }

    return iter;
}

inline string_attrs_t::const_iterator
find_string_attr(const string_attrs_t &sa, size_t near)
{
    string_attrs_t::const_iterator iter, nearest = sa.end();
    ssize_t last_diff = INT_MAX;

    for (iter = sa.begin(); iter != sa.end(); ++iter) {
        auto &lr = iter->sa_range;

        if (!lr.is_valid() || !lr.contains(near)) {
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

template<typename T>
inline string_attrs_t::const_iterator
rfind_string_attr_if(const string_attrs_t &sa, ssize_t near, T predicate)
{
    string_attrs_t::const_iterator iter, nearest = sa.end();
    ssize_t last_diff = INT_MAX;

    for (iter = sa.begin(); iter != sa.end(); ++iter) {
        auto &lr = iter->sa_range;

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

inline struct line_range
find_string_attr_range(const string_attrs_t &sa, string_attr_type_t type)
{
    auto iter = find_string_attr(sa, type);

    if (iter != sa.end()) {
        return iter->sa_range;
    }

    return line_range();
}

inline void remove_string_attr(string_attrs_t &sa, const struct line_range &lr)
{
    string_attrs_t::iterator iter;

    while ((iter = find_string_attr(sa, lr)) != sa.end()) {
        sa.erase(iter);
    }
}

inline void remove_string_attr(string_attrs_t &sa, string_attr_type_t type)
{
    for (auto iter = sa.begin(); iter != sa.end();) {
        if (iter->sa_type == type) {
            iter = sa.erase(iter);
        } else {
            ++iter;
        }
    }
}

inline void shift_string_attrs(string_attrs_t &sa, int32_t start, int32_t amount)
{
    for (auto &iter : sa) {
        iter.sa_range.shift(start, amount);
    }
}

struct text_wrap_settings {
    text_wrap_settings() : tws_indent(2), tws_width(80) {};

    text_wrap_settings &with_indent(int indent) {
        this->tws_indent = indent;
        return *this;
    };

    text_wrap_settings &with_width(int width) {
        this->tws_width = width;
        return *this;
    };

    int tws_indent;
    int tws_width;
};

/**
 * A line that has attributes.
 */
class attr_line_t {
public:
    attr_line_t() {
        this->al_attrs.reserve(RESERVE_SIZE);
    };

    attr_line_t(std::string str) : al_string(std::move(str)) {
        this->al_attrs.reserve(RESERVE_SIZE);
    };

    attr_line_t(const char *str) : al_string(str) {
        this->al_attrs.reserve(RESERVE_SIZE);
    };

    static inline attr_line_t from_ansi_str(const char *str) {
        attr_line_t retval;

        return retval.with_ansi_string("%s", str);
    };

    /** @return The string itself. */
    std::string &get_string() { return this->al_string; };

    const std::string &get_string() const { return this->al_string; };

    /** @return The attributes for the string. */
    string_attrs_t &get_attrs() { return this->al_attrs; };

    const string_attrs_t &get_attrs() const { return this->al_attrs; };

    attr_line_t &with_string(const std::string &str) {
        this->al_string = str;
        return *this;
    }

    attr_line_t &with_ansi_string(const char *str, ...);

    attr_line_t &with_ansi_string(const std::string &str);

    attr_line_t &with_attr(const string_attr &sa) {
        this->al_attrs.push_back(sa);
        return *this;
    };

    attr_line_t &ensure_space() {
        if (!this->al_string.empty() &&
            this->al_string.back() != ' ' &&
            this->al_string.back() != '[') {
            this->append(1, ' ');
        }

        return *this;
    };

    template<typename S, typename T = void *>
    attr_line_t &append(S str,
                        string_attr_type_t type = nullptr,
                        T val = T()) {
        size_t start_len = this->al_string.length();

        this->al_string.append(str);
        if (type != nullptr) {
            line_range lr{(int) start_len, (int) this->al_string.length()};

            this->al_attrs.emplace_back(lr, type, val);
        }
        return *this;
    };

    attr_line_t &append(const char *str, size_t len) {
        this->al_string.append(str, len);

        return *this;
    };

    attr_line_t &insert(size_t index, const attr_line_t &al, text_wrap_settings *tws = nullptr);

    attr_line_t &append(const attr_line_t &al, text_wrap_settings *tws = nullptr) {
        return this->insert(this->al_string.length(), al, tws);
    };

    attr_line_t &append(size_t len, char c) {
        this->al_string.append(len, c);
        return *this;
    };

    attr_line_t &insert(size_t index, size_t len, char c) {
        this->al_string.insert(index, len, c);

        shift_string_attrs(this->al_attrs, index, len);

        return *this;
    }

    attr_line_t &insert(size_t index, const char *str) {
        this->al_string.insert(index, str);

        shift_string_attrs(this->al_attrs, index, strlen(str));

        return *this;
    }

    attr_line_t &erase(size_t pos, size_t len = std::string::npos) {
        this->al_string.erase(pos, len);

        shift_string_attrs(this->al_attrs, pos, -((int32_t) len));

        return *this;
    };

    attr_line_t &erase_utf8_chars(size_t start) {
        auto byte_index = utf8_char_to_byte_index(this->al_string, start);
        this->erase(byte_index);

        return *this;
    };

    attr_line_t &right_justify(unsigned long width);

    ssize_t length() const {
        size_t retval = this->al_string.length();

        for (const auto &al_attr : this->al_attrs) {
            retval = std::max(retval, (size_t) al_attr.sa_range.lr_start);
            if (al_attr.sa_range.lr_end != -1) {
                retval = std::max(retval, (size_t) al_attr.sa_range.lr_end);
            }
        }

        return retval;
    };

    std::string get_substring(const line_range &lr) const {
        if (!lr.is_valid()) {
            return "";
        }
        return this->al_string.substr(lr.lr_start, lr.length());
    };

    string_attrs_t::const_iterator find_attr(size_t near) const {
        near = std::min(near, this->al_string.length() - 1);

        while (near > 0 && isspace(this->al_string[near])) {
            near -= 1;
        }

        return find_string_attr(this->al_attrs, near);
    };

    bool empty() const {
        return this->length() == 0;
    };

    /** Clear the string and the attributes for the string. */
    attr_line_t &clear()
    {
        this->al_string.clear();
        this->al_attrs.clear();

        return *this;
    };

    attr_line_t subline(size_t start, size_t len = std::string::npos) const;

    void split_lines(std::vector<attr_line_t> &lines) const;

    size_t nearest_text(size_t x) const;

    void apply_hide();

private:
    const static size_t RESERVE_SIZE = 128;

    std::string    al_string;
    string_attrs_t al_attrs;
};

#endif
