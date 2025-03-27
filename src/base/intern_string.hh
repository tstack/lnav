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

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <assert.h>
#include <string.h>
#include <sys/types.h>

#include "fmt/format.h"
#include "mapbox/variant.hpp"
#include "result.h"
#include "strnatcmp.h"

unsigned long hash_str(const char* str, size_t len);

struct string_fragment {
    using iterator = const char*;

    static constexpr string_fragment invalid()
    {
        string_fragment retval;

        retval.invalidate();
        return retval;
    }

    static string_fragment from_string_view(std::string_view str)
    {
        return string_fragment{str.data(), 0, (int) str.size()};
    }

    static string_fragment from_c_str(const char* str)
    {
        return string_fragment{str, 0, str != nullptr ? (int) strlen(str) : 0};
    }

    static string_fragment from_c_str(const unsigned char* str)
    {
        return string_fragment{
            str, 0, str != nullptr ? (int) strlen((char*) str) : 0};
    }

    template<typename T, std::size_t N>
    static constexpr string_fragment from_const(const T (&str)[N])
    {
        return string_fragment{str, 0, (int) N - 1};
    }

    static string_fragment from_str(const std::string& str)
    {
        return string_fragment{str.c_str(), 0, (int) str.size()};
    }

    static string_fragment from_substr(const std::string& str,
                                       size_t offset,
                                       size_t length)
    {
        return string_fragment{
            str.c_str(), (int) offset, (int) (offset + length)};
    }

    static string_fragment from_str_range(const std::string& str,
                                          size_t begin,
                                          size_t end)
    {
        return string_fragment{str.c_str(), (int) begin, (int) end};
    }

    static string_fragment from_bytes(const char* bytes, size_t len)
    {
        return string_fragment{bytes, 0, (int) len};
    }

    static string_fragment from_bytes(const unsigned char* bytes, size_t len)
    {
        return string_fragment{(const char*) bytes, 0, (int) len};
    }

    static string_fragment from_memory_buffer(const fmt::memory_buffer& buf)
    {
        return string_fragment{buf.data(), 0, (int) buf.size()};
    }

    static string_fragment from_byte_range(const char* bytes,
                                           size_t begin,
                                           size_t end)
    {
        return string_fragment{bytes, (int) begin, (int) end};
    }

    constexpr string_fragment() : sf_string(nullptr), sf_begin(0), sf_end(0) {}

    explicit constexpr string_fragment(const char* str,
                                       int begin = 0,
                                       int end = -1)
        : sf_string(str), sf_begin(begin),
          sf_end(end == -1
                     ? static_cast<int>(std::string::traits_type::length(str))
                     : end)
    {
    }

    explicit string_fragment(const unsigned char* str,
                             int begin = 0,
                             int end = -1)
        : sf_string((const char*) str), sf_begin(begin),
          sf_end(end == -1 ? strlen((const char*) str) : end)
    {
    }

    string_fragment(const std::string& str)
        : sf_string(str.c_str()), sf_begin(0), sf_end(str.length())
    {
    }

    constexpr bool is_valid() const
    {
        return this->sf_begin != -1 && this->sf_begin <= this->sf_end;
    }

    constexpr int length() const { return this->sf_end - this->sf_begin; }

    Result<ssize_t, const char*> utf8_length() const;

    size_t column_to_byte_index(size_t col) const;

    size_t byte_to_column_index(size_t byte_index) const;

    std::tuple<int, int> byte_to_column_index(size_t byte_start,
                                              size_t byte_end) const
    {
        return {
            this->byte_to_column_index(byte_start),
            this->byte_to_column_index(byte_end),
        };
    }

    size_t column_width() const;

    constexpr const char* data() const
    {
        return &this->sf_string[this->sf_begin];
    }

    const unsigned char* udata() const
    {
        return (const unsigned char*) &this->sf_string[this->sf_begin];
    }

    char* writable_data(int offset = 0)
    {
        return (char*) &this->sf_string[this->sf_begin + offset];
    }

    constexpr char front() const { return this->sf_string[this->sf_begin]; }

    uint32_t front_codepoint() const;

    constexpr char back() const { return this->sf_string[this->sf_end - 1]; }

    constexpr void pop_back()
    {
        if (!this->empty()) {
            this->sf_end -= 1;
        }
    }

    iterator begin() const { return &this->sf_string[this->sf_begin]; }

    iterator end() const { return &this->sf_string[this->sf_end]; }

    constexpr bool empty() const { return !this->is_valid() || length() == 0; }

    Result<ssize_t, const char*> codepoint_to_byte_index(
        ssize_t cp_index) const;

    string_fragment sub_cell_range(int cell_start, int cell_end) const;

    constexpr const char& operator[](size_t index) const
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

    bool operator!=(const string_fragment& rhs) const
    {
        return !(*this == rhs);
    }

    bool operator<(const string_fragment& rhs) const
    {
        auto rc = strncmp(
            this->data(), rhs.data(), std::min(this->length(), rhs.length()));
        if (rc < 0 || (rc == 0 && this->length() < rhs.length())) {
            return true;
        }

        return false;
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

    template<std::size_t N>
    bool operator==(const char (&str)[N]) const
    {
        return (N - 1) == (size_t) this->length()
            && strncmp(this->data(), str, N - 1) == 0;
    }

    bool operator!=(const char* str) const { return !(*this == str); }

    template<typename... Args>
    bool is_one_of(Args... args) const
    {
        return (this->operator==(args) || ...);
    }

    bool startswith(const char* prefix) const
    {
        const auto* iter = this->begin();

        while (*prefix != '\0' && iter < this->end() && *prefix == *iter) {
            prefix += 1;
            iter += 1;
        }

        return *prefix == '\0';
    }

    bool endswith(const char* suffix) const
    {
        int suffix_len = strlen(suffix);

        if (suffix_len > this->length()) {
            return false;
        }

        const auto* curr = this->end() - suffix_len;
        while (*suffix != '\0' && *curr == *suffix) {
            suffix += 1;
            curr += 1;
        }

        return *suffix == '\0';
    }

    constexpr string_fragment substr(int begin) const
    {
        return string_fragment{
            this->sf_string, this->sf_begin + begin, this->sf_end};
    }

    string_fragment sub_range(int begin, int end) const
    {
        if (this->sf_begin + begin > this->sf_end) {
            begin = this->sf_end - this->sf_begin;
        }
        if (this->sf_begin + end > this->sf_end) {
            end = this->sf_end - this->sf_begin;
        }
        return string_fragment{
            this->sf_string, this->sf_begin + begin, this->sf_begin + end};
    }

    constexpr bool contains(const string_fragment& sf) const
    {
        return this->sf_string == sf.sf_string && this->sf_begin <= sf.sf_begin
            && sf.sf_end <= this->sf_end;
    }

    constexpr size_t count(char ch) const
    {
        size_t retval = 0;

        for (int lpc = this->sf_begin; lpc < this->sf_end; lpc++) {
            if (this->sf_string[lpc] == ch) {
                retval += 1;
            }
        }

        return retval;
    }

    std::optional<int> find(char ch) const
    {
        for (int lpc = this->sf_begin; lpc < this->sf_end; lpc++) {
            if (this->sf_string[lpc] == ch) {
                return lpc - this->sf_begin;
            }
        }

        return std::nullopt;
    }

    std::optional<int> rfind(char ch) const;

    std::optional<int> next_word(int start_col) const;
    std::optional<int> prev_word(int start_col) const;

    template<typename P>
    string_fragment find_left_boundary(size_t start,
                                       P&& predicate,
                                       size_t count = 1) const
    {
        assert((int) start <= this->length());

        if (this->empty()) {
            return *this;
        }

        if (start > 0 && start == static_cast<size_t>(this->length())) {
            start -= 1;
        }
        while (true) {
            if (predicate(this->data()[start])) {
                count -= 1;
                if (count == 0) {
                    start += 1;
                    break;
                }
            } else if (start == 0) {
                break;
            }
            start -= 1;
        }

        return string_fragment{
            this->sf_string,
            this->sf_begin + (int) start,
            this->sf_end,
        };
    }

    template<typename P>
    string_fragment find_right_boundary(size_t start,
                                        P&& predicate,
                                        size_t count = 1) const
    {
        while ((int) start < this->length()) {
            if (predicate(this->data()[start])) {
                count -= 1;
                if (count == 0) {
                    break;
                }
            }
            start += 1;
        }

        return string_fragment{
            this->sf_string,
            this->sf_begin,
            this->sf_begin + (int) start,
        };
    }

    template<typename P>
    string_fragment find_boundaries_around(size_t start,
                                           P&& predicate,
                                           size_t count = 1) const
    {
        auto left = this->find_left_boundary(start, predicate, count);

        return left.find_right_boundary(
            start - left.sf_begin, predicate, count);
    }

    std::optional<std::pair<uint32_t, string_fragment>> consume_codepoint()
        const
    {
        auto cp = this->front_codepoint();
        auto index_res = this->codepoint_to_byte_index(1);

        if (index_res.isErr()) {
            return std::nullopt;
        }

        return std::make_pair(cp, this->substr(index_res.unwrap()));
    }

    template<typename P>
    std::optional<string_fragment> consume(P predicate) const
    {
        int consumed = 0;
        while (consumed < this->length()) {
            if (!predicate(this->data()[consumed])) {
                break;
            }

            consumed += 1;
        }

        if (consumed == 0) {
            return std::nullopt;
        }

        return string_fragment{
            this->sf_string,
            this->sf_begin + consumed,
            this->sf_end,
        };
    }

    std::optional<string_fragment> consume_n(int amount) const;

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
        = std::optional<std::pair<string_fragment, string_fragment>>;

    template<typename P>
    split_result split_while(P&& predicate) const
    {
        int consumed = 0;
        while (consumed < this->length()) {
            if (!predicate(this->data()[consumed])) {
                break;
            }

            consumed += 1;
        }

        if (consumed == 0) {
            return std::nullopt;
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

    using split_when_result = std::pair<string_fragment, string_fragment>;

    template<typename P>
    split_when_result split_when(P&& predicate) const
    {
        int consumed = 0;
        while (consumed < this->length()) {
            if (predicate(this->data()[consumed])) {
                break;
            }

            consumed += 1;
        }

        return std::make_pair(
            string_fragment{
                this->sf_string,
                this->sf_begin,
                this->sf_begin + consumed,
            },
            string_fragment{
                this->sf_string,
                this->sf_begin + consumed
                    + ((consumed == this->length()) ? 0 : 1),
                this->sf_end,
            });
    }

    template<typename P>
    split_result split_pair(P&& predicate) const
    {
        int consumed = 0;
        while (consumed < this->length()) {
            if (predicate(this->data()[consumed])) {
                break;
            }

            consumed += 1;
        }

        if (consumed == this->length()) {
            return std::nullopt;
        }

        return std::make_pair(
            string_fragment{
                this->sf_string,
                this->sf_begin,
                this->sf_begin + consumed,
            },
            string_fragment{
                this->sf_string,
                this->sf_begin + consumed + 1,
                this->sf_end,
            });
    }

    template<typename P>
    split_result rsplit_pair(P&& predicate) const
    {
        if (this->empty()) {
            return std::nullopt;
        }

        auto curr = this->sf_end - 1;
        while (curr >= this->sf_begin) {
            if (predicate(this->sf_string[curr])) {
                return std::make_pair(
                    string_fragment{
                        this->sf_string,
                        this->sf_begin,
                        this->sf_begin + curr,
                    },
                    string_fragment{
                        this->sf_string,
                        this->sf_begin + curr + 1,
                        this->sf_end,
                    });
            }

            curr -= 1;
        }

        return std::nullopt;
    }

    split_result split_n(int amount) const;

    std::vector<string_fragment> split_lines() const;

    struct tag1 {
        const char t_value;

        constexpr explicit tag1(const char value) : t_value(value) {}

        constexpr bool operator()(char ch) const { return this->t_value == ch; }
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

    std::string to_unquoted_string() const;

    void clear()
    {
        this->sf_begin = 0;
        this->sf_end = 0;
    }

    constexpr void invalidate()
    {
        this->sf_begin = -1;
        this->sf_end = -1;
    }

    string_fragment trim(const char* tokens) const;
    string_fragment rtrim(const char* tokens) const;
    string_fragment trim() const;

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

    template<typename A>
    const char* to_c_str(A allocator) const
    {
        auto* retval = allocator.allocate(this->length() + 1);
        memcpy(retval, this->data(), this->length());
        retval[this->length()] = '\0';
        return retval;
    }

    template<typename A>
    string_fragment to_owned(A allocator) const
    {
        return string_fragment{
            this->to_c_str(allocator),
            0,
            this->length(),
        };
    }

    std::string_view to_string_view() const
    {
        return std::string_view{
            this->data(),
            static_cast<std::string_view::size_type>(this->length())};
    }

    enum class case_style {
        lower,
        upper,
        camel,
        mixed,
    };

    case_style detect_text_case_style() const;

    std::string to_string_with_case_style(case_style style) const;

    unsigned long hash() const
    {
        return hash_str(this->data(), this->length());
    }

    const char* sf_string;
    int sf_begin;
    int sf_end;
};

inline bool
operator==(const std::string& left, const string_fragment& right)
{
    return right == left;
}

inline bool
operator<(const char* left, const string_fragment& right)
{
    int rc = strncmp(left, right.data(), right.length());
    return rc < 0;
}

inline void
operator+=(std::string& left, const string_fragment& right)
{
    left.append(right.data(), right.length());
}

inline bool
operator<(const string_fragment& left, const char* right)
{
    return strncmp(left.data(), right, left.length()) < 0;
}

inline std::ostream&
operator<<(std::ostream& os, const string_fragment& sf)
{
    os.write(sf.data(), sf.length());
    return os;
}

class string_fragment_producer {
public:
    struct eof {};
    struct error {
        std::string what;
    };
    using next_result = mapbox::util::variant<eof, string_fragment, error>;
    static std::unique_ptr<string_fragment_producer> from(string_fragment sf);

    Result<void, std::string> for_each(
        std::function<Result<void, std::string>(string_fragment)> cb)
    {
        while (true) {
            auto next_res = this->next();
            if (next_res.is<error>()) {
                auto err = next_res.get<error>();

                return Err(err.what);
            }

            if (next_res.is<eof>()) {
                break;
            }

            const auto sf = next_res.get<string_fragment>();
            auto cb_res = cb(sf);

            if (cb_res.isErr()) {
                return Err(cb_res.unwrapErr());
            }
        }

        return Ok();
    }

    virtual ~string_fragment_producer() {}

    virtual next_result next() = 0;

    std::string to_string();
};

class intern_string {
public:
    static const intern_string* lookup(const char* str, ssize_t len) noexcept;

    static const intern_string* lookup(const string_fragment& sf) noexcept;

    static const intern_string* lookup(const std::string& str) noexcept;

    const char* get() const { return this->is_str.c_str(); };

    size_t size() const { return this->is_str.size(); }

    std::string to_string() const { return this->is_str; }

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

namespace fmt {
template<>
struct formatter<string_fragment> : formatter<string_view> {
    template<typename FormatContext>
    auto format(const string_fragment& sf, FormatContext& ctx) const
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
operator<(const intern_string_t& left, const string_fragment& sf)
{
    return left.to_string_fragment() < sf;
}

inline bool
operator<(const string_fragment& lhs, const intern_string_t& rhs)
{
    return lhs < rhs.to_string_fragment();
}

inline bool
operator==(const string_fragment& left, const intern_string_t& right)
{
    return (left.length() == (int) right.size())
        && (memcmp(left.data(), right.get(), left.length()) == 0);
}

constexpr string_fragment
operator"" _frag(const char* str, std::size_t len)
{
    return string_fragment{str, 0, (int) len};
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

inline string_fragment
to_string_fragment(const std::string_view& sv)
{
    return string_fragment::from_bytes(sv.data(), sv.length());
}

struct frag_hasher {
    size_t operator()(const string_fragment& sf) const
    {
        return hash_str(sf.data(), sf.length());
    }
};

struct intern_hasher {
    size_t operator()(const intern_string_t& is) const
    {
        return hash_str(is.c_str(), is.size());
    }
};

#endif
