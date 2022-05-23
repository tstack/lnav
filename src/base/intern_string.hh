/**
 * Copyright (c) 2014, Timothy Stack
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
 * @file intern_string.hh
 */

#ifndef intern_string_hh
#define intern_string_hh

#include <string>
#include <vector>

#include <string.h>
#include <sys/types.h>

#include "fmt/format.h"
#include "optional.hpp"
#include "strnatcmp.h"
#include "ww898/cp_utf8.hpp"

struct string_fragment {
    using iterator = const char*;

    explicit string_fragment(const char* str = "", int begin = 0, int end = -1)
        : sf_string(str), sf_begin(begin),
          sf_end(end == -1 ? strlen(str) : end){};

    explicit string_fragment(const unsigned char* str,
                             int begin = 0,
                             int end = -1)
        : sf_string((const char*) str), sf_begin(begin),
          sf_end(end == -1 ? strlen((const char*) str) : end){};

    string_fragment(const std::string& str)
        : sf_string(str.c_str()), sf_begin(0), sf_end(str.length())
    {
    }

    bool is_valid() const { return this->sf_begin != -1; }

    int length() const { return this->sf_end - this->sf_begin; }

    Result<size_t, const char*> utf8_length() const
    {
        size_t retval = 0;

        for (ssize_t byte_index = this->sf_begin; byte_index < this->sf_end;) {
            auto ch_size
                = TRY(ww898::utf::utf8::char_size([this, byte_index]() {
                      return std::make_pair(this->sf_string[byte_index],
                                            this->sf_end - byte_index);
                  }));
            byte_index += ch_size;
            retval += 1;
        }

        return Ok(retval);
    }

    const char* data() const { return &this->sf_string[this->sf_begin]; }

    char front() const { return this->sf_string[this->sf_begin]; }

    iterator begin() const { return &this->sf_string[this->sf_begin]; }

    iterator end() const { return &this->sf_string[this->sf_end]; }

    bool empty() const { return !this->is_valid() || length() == 0; }

    char operator[](int index) const
    {
        return this->sf_string[sf_begin + index];
    }

    bool operator==(const std::string& str) const
    {
        if (this->length() != (int) str.length()) {
            return false;
        }

        return memcmp(
                   &this->sf_string[this->sf_begin], str.c_str(), str.length())
            == 0;
    }

    bool operator==(const string_fragment& sf) const
    {
        if (this->length() != sf.length()) {
            return false;
        }

        return memcmp(this->data(), sf.data(), sf.length()) == 0;
    }

    bool iequal(const string_fragment& sf) const
    {
        if (this->length() != sf.length()) {
            return false;
        }

        return strnatcasecmp(
                   this->length(), this->data(), sf.length(), sf.data())
            == 0;
    }

    bool operator==(const char* str) const
    {
        size_t len = strlen(str);

        return len == (size_t) this->length()
            && strncmp(this->data(), str, this->length()) == 0;
    }

    bool operator!=(const char* str) const
    {
        return !(*this == str);
    }

    bool startswith(const char* prefix) const
    {
        const auto* iter = this->begin();

        while (*prefix != '\0' && *prefix == *iter && iter < this->end()) {
            prefix += 1;
            iter += 1;
        }

        return *prefix == '\0';
    }

    string_fragment substr(int begin) const
    {
        return string_fragment{
            this->sf_string, this->sf_begin + begin, this->sf_end};
    }

    nonstd::optional<size_t> find(char ch) const
    {
        for (int lpc = this->sf_begin; lpc < this->sf_end; lpc++) {
            if (this->sf_string[lpc] == ch) {
                return lpc;
            }
        }

        return nonstd::nullopt;
    }

    template<typename P>
    nonstd::optional<string_fragment> consume(P predicate) const
    {
        int consumed = 0;
        while (consumed < this->length()) {
            if (!predicate(this->data()[consumed])) {
                break;
            }

            consumed += 1;
        }

        if (consumed == 0) {
            return nonstd::nullopt;
        }

        return string_fragment{
            this->sf_string,
            this->sf_begin + consumed,
            this->sf_end,
        };
    }

    nonstd::optional<string_fragment> consume_n(int amount) const;

    template<typename P>
    string_fragment skip(P predicate) const
    {
        int offset = 0;
        while (offset < this->length() && predicate(this->data()[offset])) {
            offset += 1;
        }

        return string_fragment{
            this->sf_string,
            this->sf_begin + offset,
            this->sf_end,
        };
    }

    using split_result
        = nonstd::optional<std::pair<string_fragment, string_fragment>>;

    template<typename P>
    split_result split_while(P& predicate) const
    {
        int consumed = 0;
        while (consumed < this->length()) {
            if (!predicate(this->data()[consumed])) {
                break;
            }

            consumed += 1;
        }

        if (consumed == 0) {
            return nonstd::nullopt;
        }

        return std::make_pair(
            string_fragment{
                this->sf_string,
                this->sf_begin,
                this->sf_begin + consumed,
            },
            string_fragment{
                this->sf_string,
                this->sf_begin + consumed,
                this->sf_end,
            });
    }

    split_result split_n(int amount) const;

    std::vector<string_fragment> split_lines() const;

    struct tag1 {
        const char t_value;

        bool operator()(char ch) const { return this->t_value == ch; }
    };

    struct quoted_string_body {
        bool qs_in_escape{false};

        bool operator()(char ch)
        {
            if (this->qs_in_escape) {
                this->qs_in_escape = false;
                return true;
            } else if (ch == '\\') {
                this->qs_in_escape = true;
                return true;
            } else if (ch == '"') {
                return false;
            } else {
                return true;
            }
        }
    };

    const char* to_string(char* buf) const
    {
        memcpy(buf, this->data(), this->length());
        buf[this->length()] = '\0';

        return buf;
    }

    std::string to_string() const
    {
        return {this->data(), (size_t) this->length()};
    }

    void clear()
    {
        this->sf_begin = 0;
        this->sf_end = 0;
    }

    void invalidate()
    {
        this->sf_begin = -1;
        this->sf_end = -1;
    }

    void trim(const char* tokens);

    string_fragment prepend(const char* str, int amount) const
    {
        return string_fragment{
            str,
            this->sf_begin + amount,
            this->sf_end + amount,
        };
    }

    string_fragment erase_before(const char* str, int amount) const
    {
        return string_fragment{
            str,
            this->sf_begin - amount,
            this->sf_end - amount,
        };
    }

    string_fragment erase(const char* str, int amount) const
    {
        return string_fragment{
            str,
            this->sf_begin,
            this->sf_end - amount,
        };
    }

    const char* sf_string;
    int sf_begin;
    int sf_end;
};

inline bool
operator<(const char* left, const string_fragment& right)
{
    int rc = strncmp(left, right.data(), right.length());
    return rc < 0;
}

inline bool
operator<(const string_fragment& left, const char* right)
{
    return strncmp(left.data(), right, left.length()) < 0;
}

class intern_string {
public:
    static const intern_string* lookup(const char* str, ssize_t len) noexcept;

    static const intern_string* lookup(const string_fragment& sf) noexcept;

    static const intern_string* lookup(const std::string& str) noexcept;

    const char* get() const
    {
        return this->is_str.c_str();
    };

    size_t size() const
    {
        return this->is_str.size();
    }

    std::string to_string() const
    {
        return this->is_str;
    }

    string_fragment to_string_fragment() const
    {
        return string_fragment{this->is_str};
    }

    bool startswith(const char* prefix) const;

    struct intern_table;
    static std::shared_ptr<intern_table> get_table_lifetime();

private:
    friend intern_table;

    intern_string(const char* str, ssize_t len)
        : is_next(nullptr), is_str(str, (size_t) len)
    {
    }

    intern_string* is_next;
    std::string is_str;
};

using intern_table_lifetime = std::shared_ptr<intern_string::intern_table>;

class intern_string_t {
public:
    using iterator = const char*;

    intern_string_t(const intern_string* is = nullptr) : ist_interned_string(is)
    {
    }

    const intern_string* unwrap() const { return this->ist_interned_string; }

    void clear() { this->ist_interned_string = nullptr; };

    bool empty() const { return this->ist_interned_string == nullptr; }

    const char* get() const
    {
        if (this->empty()) {
            return "";
        }
        return this->ist_interned_string->get();
    }

    const char* c_str() const { return this->get(); }

    iterator begin() const { return this->get(); }

    iterator end() const { return this->get() + this->size(); }

    size_t size() const
    {
        if (this->ist_interned_string == nullptr) {
            return 0;
        }
        return this->ist_interned_string->size();
    }

    size_t hash() const
    {
        auto ptr = (uintptr_t) this->ist_interned_string;

        return ptr;
    }

    std::string to_string() const
    {
        if (this->ist_interned_string == nullptr) {
            return "";
        }
        return this->ist_interned_string->to_string();
    }

    string_fragment to_string_fragment() const
    {
        if (this->ist_interned_string == nullptr) {
            return string_fragment{"", 0, 0};
        }
        return this->ist_interned_string->to_string_fragment();
    }

    bool operator<(const intern_string_t& rhs) const
    {
        return strcmp(this->get(), rhs.get()) < 0;
    }

    bool operator==(const intern_string_t& rhs) const
    {
        return this->ist_interned_string == rhs.ist_interned_string;
    }

    bool operator!=(const intern_string_t& rhs) const
    {
        return !(*this == rhs);
    }

    bool operator==(const char* rhs) const
    {
        return strcmp(this->get(), rhs) == 0;
    }

    bool operator!=(const char* rhs) const
    {
        return strcmp(this->get(), rhs) != 0;
    }

    static bool case_lt(const intern_string_t& lhs, const intern_string_t& rhs)
    {
        return strnatcasecmp(lhs.size(), lhs.get(), rhs.size(), rhs.get()) < 0;
    }

private:
    const intern_string* ist_interned_string;
};

unsigned long hash_str(const char* str, size_t len);

namespace fmt {
template<>
struct formatter<string_fragment> : formatter<string_view> {
    template<typename FormatContext>
    auto format(const string_fragment& sf, FormatContext& ctx)
    {
        return formatter<string_view>::format(
            string_view{sf.data(), (size_t) sf.length()}, ctx);
    }
};

template<>
struct formatter<intern_string_t> : formatter<string_view> {
    template<typename FormatContext>
    auto format(const intern_string_t& is, FormatContext& ctx)
    {
        return formatter<string_view>::format(
            string_view{is.get(), (size_t) is.size()}, ctx);
    }
};
}  // namespace fmt

namespace std {
template<>
struct hash<const intern_string_t> {
    std::size_t operator()(const intern_string_t& ist) const
    {
        return ist.hash();
    }
};
}  // namespace std

inline bool
operator<(const char* left, const intern_string_t& right)
{
    int rc = strncmp(left, right.get(), right.size());
    return rc < 0;
}

inline bool
operator<(const intern_string_t& left, const char* right)
{
    return strncmp(left.get(), right, left.size()) < 0;
}

inline bool
operator==(const intern_string_t& left, const string_fragment& sf)
{
    return ((int) left.size() == sf.length())
        && (memcmp(left.get(), sf.data(), left.size()) == 0);
}

inline bool
operator==(const string_fragment& left, const intern_string_t& right)
{
    return (left.length() == (int) right.size())
        && (memcmp(left.data(), right.get(), left.length()) == 0);
}

namespace std {
inline string
to_string(const string_fragment& s)
{
    return {s.data(), (size_t) s.length()};
}

inline string
to_string(const intern_string_t& s)
{
    return s.to_string();
}
}  // namespace std

inline string_fragment
to_string_fragment(const string_fragment& s)
{
    return s;
}

inline string_fragment
to_string_fragment(const intern_string_t& s)
{
    return string_fragment(s.get(), 0, s.size());
}

inline string_fragment
to_string_fragment(const std::string& s)
{
    return string_fragment(s.c_str(), 0, s.length());
}

#endif
