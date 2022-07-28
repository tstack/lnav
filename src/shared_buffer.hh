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
 * @file shared_buffer.hh
 */

#ifndef shared_buffer_hh
#define shared_buffer_hh

#include <string>
#include <vector>

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "base/auto_mem.hh"
#include "base/intern_string.hh"
#include "base/lnav_log.hh"

class shared_buffer;

struct shared_buffer_ref {
public:
    shared_buffer_ref(char* data = nullptr, size_t len = 0)
        : sb_owner(nullptr), sb_data(data), sb_length(len)
    {
    }

    ~shared_buffer_ref() { this->disown(); }

    shared_buffer_ref(const shared_buffer_ref& other)
    {
        this->sb_owner = nullptr;
        this->sb_data = nullptr;
        this->sb_length = 0;

        this->copy_ref(other);
    }

    shared_buffer_ref(shared_buffer_ref&& other) noexcept;

    shared_buffer_ref& operator=(const shared_buffer_ref& other)
    {
        if (this != &other) {
            this->disown();
            this->copy_ref(other);
        }

        return *this;
    }

    bool empty() const
    {
        return this->sb_data == nullptr || this->sb_length == 0;
    }

    const char* get_data() const { return this->sb_data; }

    const char* get_data_at(off_t offset) const
    {
        return &this->sb_data[offset];
    }

    size_t length() const { return this->sb_length; }

    shared_buffer_ref& rtrim(bool pred(char))
    {
        while (this->sb_length > 0 && pred(this->sb_data[this->sb_length - 1]))
        {
            this->sb_length -= 1;
        }

        return *this;
    }

    bool contains(const char* ptr) const
    {
        const char* buffer_end = this->sb_data + this->sb_length;

        return (this->sb_data <= ptr && ptr < buffer_end);
    }

    char* get_writable_data()
    {
        if (this->take_ownership()) {
            return this->sb_data;
        }

        return nullptr;
    }

    string_fragment to_string_fragment(off_t offset, size_t len) const
    {
        return string_fragment{
            this->sb_data, (int) offset, (int) (offset + len)};
    }

    string_fragment to_string_fragment() const
    {
        return string_fragment::from_bytes(this->sb_data, this->length());
    }

    using narrow_result = std::pair<char*, size_t>;
    narrow_result narrow(size_t new_data, size_t new_length);

    void widen(narrow_result old_data_length);

    void share(shared_buffer& sb, char* data, size_t len);

    bool subset(shared_buffer_ref& other, off_t offset, size_t len);

    bool take_ownership();

    void disown();

private:
    void copy_ref(const shared_buffer_ref& other);

    auto_mem<char*> sb_backtrace;
    shared_buffer* sb_owner;
    char* sb_data;
    size_t sb_length;
};

class shared_buffer {
public:
    ~shared_buffer() { this->invalidate_refs(); }

    void add_ref(shared_buffer_ref& ref) { this->sb_refs.push_back(&ref); }

    bool invalidate_refs()
    {
        bool retval = true;

        while (!this->sb_refs.empty()) {
            auto iter = this->sb_refs.begin();

            retval = retval && (*iter)->take_ownership();
        }

        return retval;
    }

    std::vector<shared_buffer_ref*> sb_refs;
};

struct tmp_shared_buffer {
    explicit tmp_shared_buffer(const char* str, size_t len = -1)
    {
        if (len == (size_t) -1) {
            len = strlen(str);
        }

        this->tsb_ref.share(this->tsb_manager, (char*) str, len);
    };

    shared_buffer tsb_manager;
    shared_buffer_ref tsb_ref;
};

inline std::string
to_string(const shared_buffer_ref& sbr)
{
    return {sbr.get_data(), sbr.length()};
}

#endif
