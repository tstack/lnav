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
 * @file shared_buffer.cc
 */

#include "config.h"

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#include "shared_buffer.hh"

static const bool DEBUG_TRACE = false;

void shared_buffer_ref::share(shared_buffer &sb, char *data, size_t len)
{
#ifdef HAVE_EXECINFO_H
    if (DEBUG_TRACE) {
        void *frames[128];
        int rc;

        rc = backtrace(frames, 128);
        this->sb_backtrace.reset(backtrace_symbols(frames, rc));
    }
#endif

    this->disown();

    sb.add_ref(*this);
    this->sb_owner = &sb;
    this->sb_data = data;
    this->sb_length = len;

    ensure(this->sb_length < (5 * 1024 * 1024));
}

bool shared_buffer_ref::subset(shared_buffer_ref &other, off_t offset, size_t len)
{
    this->disown();

    if (offset != -1) {
        this->sb_owner = other.sb_owner;
        this->sb_length = len;
        if (this->sb_owner == NULL) {
            if ((this->sb_data = (char *)malloc(this->sb_length)) == NULL) {
                return false;
            }

            memcpy(this->sb_data, &other.sb_data[offset], len);
        } else {
            LIST_INSERT_HEAD(&this->sb_owner->sb_refs, this, sb_link);
            this->sb_data = &other.sb_data[offset];
        }
    }
    return true;
}

shared_buffer_ref::shared_buffer_ref(shared_buffer_ref &&other)
{
    if (other.sb_data == nullptr) {
        this->sb_owner = nullptr;
        this->sb_data = nullptr;
        this->sb_length = 0;
    } else if (other.sb_owner != nullptr) {
        other.sb_owner->add_ref(*this);
        this->sb_owner = other.sb_owner;
        this->sb_data = other.sb_data;
        this->sb_length = other.sb_length;
        other.disown();
    } else {
        this->sb_owner = nullptr;
        this->sb_data = other.sb_data;
        this->sb_length = other.sb_length;
        other.sb_data = nullptr;
        other.sb_length = 0;
    }
}
