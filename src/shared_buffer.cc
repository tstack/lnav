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
 * @file shared_buffer.cc
 */

#include "config.h"

#ifdef HAVE_EXECINFO_H
// clang-format off
#    include <sys/types.h>
// clang-format on

#    include <execinfo.h>
#endif

#include <algorithm>

#include "base/ansi_scrubber.hh"
#include "shared_buffer.hh"

void
shared_buffer_ref::share(shared_buffer& sb, const char* data, size_t len)
{
#if SHARED_BUFFER_TRACE
    void* frames[128];
    int rc;

    rc = backtrace(frames, 128);
    this->sb_backtrace.reset(backtrace_symbols(frames, rc));
#endif

    this->disown();

    sb.add_ref(*this);
    this->sb_owner = &sb;
    this->sb_data = data;
    this->sb_length = len;

    ensure(this->sb_length < (10 * 1024 * 1024));
}

bool
shared_buffer_ref::subset(shared_buffer_ref& other, off_t offset, size_t len)
{
    this->disown();

    if (offset != -1) {
        this->sb_owner = other.sb_owner;
        this->sb_length = len;
        if (this->sb_owner == nullptr) {
            if ((this->sb_data = (char*) malloc(this->sb_length)) == nullptr) {
                return false;
            }

            memcpy(
                const_cast<char*>(this->sb_data), &other.sb_data[offset], len);
        } else {
            this->sb_owner->add_ref(*this);
            this->sb_data = &other.sb_data[offset];
        }
    }
    return true;
}

shared_buffer_ref::
shared_buffer_ref(shared_buffer_ref&& other) noexcept
{
    if (other.sb_data == nullptr) {
        this->sb_owner = nullptr;
        this->sb_data = nullptr;
        this->sb_length = 0;
    } else if (other.sb_owner != nullptr) {
        auto owner_ref_iter = std::find(other.sb_owner->sb_refs.rbegin(),
                                        other.sb_owner->sb_refs.rend(),
                                        &other);
        *owner_ref_iter = this;
        this->sb_owner = std::exchange(other.sb_owner, nullptr);
        this->sb_data = std::exchange(other.sb_data, nullptr);
        this->sb_length = std::exchange(other.sb_length, 0);
    } else {
        this->sb_owner = nullptr;
        this->sb_data = other.sb_data;
        this->sb_length = other.sb_length;
        other.sb_data = nullptr;
        other.sb_length = 0;
    }
    this->sb_metadata = other.sb_metadata;
    other.sb_metadata = {};
}

shared_buffer_ref&
shared_buffer_ref::operator=(shared_buffer_ref&& other) noexcept
{
    this->disown();

    if (other.sb_data == nullptr) {
        this->sb_owner = nullptr;
        this->sb_data = nullptr;
        this->sb_length = 0;
    } else if (other.sb_owner != nullptr) {
        auto owner_ref_iter = std::find(other.sb_owner->sb_refs.rbegin(),
                                        other.sb_owner->sb_refs.rend(),
                                        &other);
        *owner_ref_iter = this;
        this->sb_owner = std::exchange(other.sb_owner, nullptr);
        this->sb_data = std::exchange(other.sb_data, nullptr);
        this->sb_length = std::exchange(other.sb_length, 0);
    } else {
        this->sb_owner = nullptr;
        this->sb_data = other.sb_data;
        this->sb_length = other.sb_length;
        other.sb_data = nullptr;
        other.sb_length = 0;
    }
    this->sb_metadata = other.sb_metadata;
    other.sb_metadata = {};

    return *this;
}

bool
shared_buffer_ref::take_ownership(size_t length)
{
    if ((this->sb_owner != nullptr && this->sb_data != nullptr)
        || this->sb_length != length)
    {
        auto* new_data = (char*) malloc(length);
        if (new_data == nullptr) {
            return false;
        }

        memcpy(new_data, this->sb_data, std::min(length, this->sb_length));
        this->sb_length = length;
        this->sb_data = new_data;
        this->sb_owner->sb_refs.erase(find(this->sb_owner->sb_refs.begin(),
                                           this->sb_owner->sb_refs.end(),
                                           this));
        this->sb_owner = nullptr;
    }
    return true;
}

void
shared_buffer_ref::disown()
{
    if (this->sb_owner == nullptr) {
        if (this->sb_data != nullptr) {
            free(const_cast<char*>(this->sb_data));
        }
    } else {
        this->sb_owner->sb_refs.erase(find(this->sb_owner->sb_refs.begin(),
                                           this->sb_owner->sb_refs.end(),
                                           this));
    }
    this->sb_owner = nullptr;
    this->sb_data = nullptr;
    this->sb_length = 0;
    this->sb_metadata = {};
}

void
shared_buffer_ref::copy_ref(const shared_buffer_ref& other)
{
    if (other.sb_data == nullptr) {
        this->sb_owner = nullptr;
        this->sb_data = nullptr;
        this->sb_length = 0;
    } else if (other.sb_owner != nullptr) {
        this->share(*other.sb_owner, other.sb_data, other.sb_length);
    } else {
        this->sb_owner = nullptr;
        this->sb_data = (char*) malloc(other.sb_length);
        memcpy(
            const_cast<char*>(this->sb_data), other.sb_data, other.sb_length);
        this->sb_length = other.sb_length;
    }
    this->sb_metadata = other.sb_metadata;
}

shared_buffer_ref::narrow_result
shared_buffer_ref::narrow(size_t new_data, size_t new_length)
{
    return std::make_pair(
        std::exchange(this->sb_data, this->sb_data + new_data),
        std::exchange(this->sb_length, new_length));
}

void
shared_buffer_ref::widen(narrow_result old_data_length)
{
    this->sb_data = old_data_length.first;
    this->sb_length = old_data_length.second;
}

void
shared_buffer_ref::erase_ansi()
{
    if (!this->sb_metadata.m_valid_utf || !this->sb_metadata.m_has_ansi) {
        return;
    }

    auto* writable_data = this->get_writable_data();
    auto new_len = erase_ansi_escapes(
        string_fragment::from_bytes(writable_data, this->sb_length));

    this->sb_length = new_len;
    this->sb_metadata.m_has_ansi = false;
}
