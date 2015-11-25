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

#ifndef __intern_string_hh
#define __intern_string_hh

#include <string.h>
#include <sys/types.h>

#include <string>

class intern_string {

public:
    static const intern_string *lookup(const char *str, ssize_t len);

    static const intern_string *lookup(const std::string &str);

    const char *get(void) const {
        return this->is_str;
    };

    size_t size(void) const {
        return this->is_len;
    }

    std::string to_string() const {
        return std::string(this->is_str, this->is_len);
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
            : is_next(NULL), is_str(str), is_len(len) {

    }

    intern_string *is_next;
    const char *is_str;
    ssize_t is_len;
};

class intern_string_t {
public:
    intern_string_t(const intern_string *is = NULL) : ist_interned_string(is) {

    }

    const intern_string *unwrap() const {
        return this->ist_interned_string;
    }

    bool empty(void) const {
        return this->ist_interned_string == NULL;
    }

    const char *get(void) const {
        if (this->empty()) {
            return "";
        }
        return this->ist_interned_string->get();
    }

    size_t size(void) const {
        if (this->ist_interned_string == NULL) {
            return 0;
        }
        return this->ist_interned_string->size();
    }

    std::string to_string(void) const {
        if (this->ist_interned_string == NULL) {
            return "";
        }
        return this->ist_interned_string->to_string();
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

    void operator=(const intern_string_t &rhs) {
        this->ist_interned_string = rhs.ist_interned_string;
    }

private:
    const intern_string *ist_interned_string;
};

unsigned long hash_str(const char *str, size_t len);

#endif
