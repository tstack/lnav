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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file intern_string.hh
 */

#ifndef intern_string_hh
#define intern_string_hh

#include <string.h>
#include <sys/types.h>

#include <string>

#include "optional.hpp"
#include "strnatcmp.h"
#include "fmt/format.h"

struct string_fragment {
    using iterator = const char *;

    explicit string_fragment(const char *str, int begin = 0, int end = -1)
        : sf_string(str), sf_begin(begin), sf_end(end == -1 ? strlen(str) : end) {
    };

    explicit string_fragment(const unsigned char *str, int begin = 0, int end = -1)
        : sf_string((const char *) str),
          sf_begin(begin),
          sf_end(end == -1 ? strlen((const char *) str) : end) {
    };

    string_fragment(const std::string &str)
        : sf_string(str.c_str()), sf_begin(0), sf_end(str.length()) {

    }

    bool is_valid() const {
        return this->sf_begin != -1;
    };

    int length() const {
        return this->sf_end - this->sf_begin;
    };

    const char *data() const {
        return &this->sf_string[this->sf_begin];
    }

    iterator begin() const {
        return &this->sf_string[this->sf_begin];
    }

    iterator end() const {
        return &this->sf_string[this->sf_end];
    }

    bool empty() const {
        return length() == 0;
    };

    char operator[](int index) const {
        return this->sf_string[sf_begin + index];
    };

    bool operator==(const std::string &str) const {
        if (this->length() != (int) str.length()) {
            return false;
        }

        return memcmp(&this->sf_string[this->sf_begin],
                      str.c_str(),
                      str.length()) == 0;
    };

    bool operator==(const string_fragment &sf) const {
        if (this->length() != sf.length()) {
            return false;
        }

        return memcmp(this->data(), sf.data(), sf.length()) == 0;
    };

    bool iequal(const string_fragment &sf) const {
        if (this->length() != sf.length()) {
            return false;
        }

        return strnatcasecmp(this->length(), this->data(),
            sf.length(), sf.data()) == 0;
    };

    bool operator==(const char *str) const {
        size_t len = strlen(str);

        return len == (size_t) this->length() &&
               strncmp(this->data(), str, this->length()) == 0;
    };

    bool operator!=(const char *str) const {
        return !(*this == str);
    }

    bool startswith(const char *prefix) const {
        auto iter = this->begin();

        while (*prefix != '\0' && *prefix == *iter && iter < this->end()) {
            prefix += 1;
            iter += 1;
        }

        return *prefix == '\0';
    }

    string_fragment substr(int begin) {
        return string_fragment{
            this->sf_string,
            this->sf_begin + begin,
            this->sf_end
        };
    }

    nonstd::optional<size_t> find(char ch) const {
        for (int lpc = this->sf_begin; lpc < this->sf_end; lpc++) {
            if (this->sf_string[lpc] == ch) {
                return lpc;
            }
        }

        return nonstd::nullopt;
    }

    const char *to_string(char *buf) const {
        memcpy(buf, this->data(), this->length());
        buf[this->length()] = '\0';

        return buf;
    };

    std::string to_string() const {
        return std::string(this->data(), this->length());
    }

    void clear() {
        this->sf_begin = 0;
        this->sf_end = 0;
    };

    void invalidate() {
        this->sf_begin = -1;
        this->sf_end = -1;
    };

    void trim(const char *tokens) {
        while (this->sf_begin < this->sf_end) {
            bool found = false;

            for (int lpc = 0; tokens[lpc] != '\0'; lpc++) {
                if (this->sf_string[this->sf_begin] == tokens[lpc]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                break;
            }

            this->sf_begin += 1;
        }
        while (this->sf_begin < this->sf_end) {
            bool found = false;

            for (int lpc = 0; tokens[lpc] != '\0'; lpc++) {
                if (this->sf_string[this->sf_end - 1] == tokens[lpc]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                break;
            }

            this->sf_end -= 1;
        }
    }

    const char *sf_string;
    int sf_begin;
    int sf_end;
};

namespace fmt {
template<>
struct formatter<string_fragment> : formatter<string_view> {
    template<typename FormatContext>
    auto format(const string_fragment& sf, FormatContext &ctx)
    {
        return formatter<string_view>::format(
            string_view{sf.data(), (size_t) sf.length()}, ctx);
    }
};
}

inline bool operator<(const char *left, const string_fragment &right) {
    int rc = strncmp(left, right.data(), right.length());
    return rc < 0;
}

inline bool operator<(const string_fragment &left, const char *right) {
    return strncmp(left.data(), right, left.length()) < 0;
}

class intern_string {

public:
    static const intern_string *lookup(const char *str, ssize_t len) noexcept;

    static const intern_string *lookup(const string_fragment &sf) noexcept;

    static const intern_string *lookup(const std::string &str) noexcept;

    const char *get() const {
        return this->is_str;
    };

    size_t size() const {
        return this->is_len;
    }

    std::string to_string() const {
        return std::string(this->is_str, this->is_len);
    }

    string_fragment to_string_fragment() const {
        return string_fragment{this->is_str, 0, (int) this->is_len};
    }

    bool startswith(const char *prefix) const {
        const char *curr = this->is_str;

        while (*prefix != '\0' && *prefix == *curr) {
            prefix += 1;
            curr += 1;
        }

        return *prefix == '\0';
    }

private:
    intern_string(const char *str, ssize_t len)
            : is_next(nullptr), is_str(str), is_len(len) {

    }

    intern_string *is_next;
    const char *is_str;
    ssize_t is_len;
};

class intern_string_t {
public:
    using iterator = const char *;

    intern_string_t(const intern_string *is = nullptr) : ist_interned_string(is) {

    }

    const intern_string *unwrap() const {
        return this->ist_interned_string;
    }

    void clear() {
        this->ist_interned_string = nullptr;
    };

    bool empty() const {
        return this->ist_interned_string == nullptr;
    }

    const char *get() const {
        if (this->empty()) {
            return "";
        }
        return this->ist_interned_string->get();
    }

    iterator begin() const {
        return this->get();
    }

    iterator end() const {
        return this->get() + this->size();
    }

    size_t size() const {
        if (this->ist_interned_string == nullptr) {
            return 0;
        }
        return this->ist_interned_string->size();
    }

    size_t hash() const {
        uintptr_t ptr = (uintptr_t) this->ist_interned_string;

        return ptr;
    }

    std::string to_string() const {
        if (this->ist_interned_string == nullptr) {
            return "";
        }
        return this->ist_interned_string->to_string();
    }

    string_fragment to_string_fragment() const {
        if (this->ist_interned_string == nullptr) {
            return string_fragment{"", 0, 0};
        }
        return this->ist_interned_string->to_string_fragment();
    }

    bool operator<(const intern_string_t &rhs) const {
        return strcmp(this->get(), rhs.get()) < 0;
    }

    bool operator==(const intern_string_t &rhs) const {
        return this->ist_interned_string == rhs.ist_interned_string;
    }

    bool operator!=(const intern_string_t &rhs) const {
        return !(*this == rhs);
    }

    bool operator==(const char *rhs) const {
        return strcmp(this->get(), rhs) == 0;
    }

    bool operator!=(const char *rhs) const {
        return strcmp(this->get(), rhs) != 0;
    }

    intern_string_t &operator=(const intern_string_t &rhs) {
        this->ist_interned_string = rhs.ist_interned_string;
        return *this;
    }

private:
    const intern_string *ist_interned_string;
};

unsigned long hash_str(const char *str, size_t len);

namespace std {
    template <>
    struct hash<const intern_string_t> {
        std::size_t operator()(const intern_string_t &ist) const {
            return ist.hash();
        }
    };
}

inline bool operator<(const char *left, const intern_string_t &right) {
    int rc = strncmp(left, right.get(), right.size());
    return rc < 0;
}

inline bool operator<(const intern_string_t &left, const char *right) {
    return strncmp(left.get(), right, left.size()) < 0;
}

inline bool operator==(const intern_string_t &left, const string_fragment &sf) {
    return ((int) left.size() == sf.length()) &&
           (memcmp(left.get(), sf.data(), left.size()) == 0);
}

inline bool operator==(const string_fragment &left, const intern_string_t &right) {
    return (left.length() == (int) right.size()) &&
           (memcmp(left.data(), right.get(), left.length()) == 0);
}

namespace std {
    inline string to_string(const string_fragment &s) {
        return string(s.data(), s.length());
    }

    inline string to_string(const intern_string_t &s) {
        return s.to_string();
    }
}

inline string_fragment to_string_fragment(const string_fragment &s) {
    return s;
}

inline string_fragment to_string_fragment(const intern_string_t &s) {
    return string_fragment(s.get(), 0, s.size());
}

inline string_fragment to_string_fragment(const std::string &s) {
    return string_fragment(s.c_str(), 0, s.length());
}

#endif
