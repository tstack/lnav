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
 * @file shared_buffer.hh
 */

#ifndef __shared_buffer_hh
#define __shared_buffer_hh

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/queue.h>

#include "auto_mem.hh"
#include "lnav_log.hh"

class shared_buffer;

struct shared_buffer_ref {
public:
    shared_buffer_ref(char *data = NULL, size_t len = 0)
        : sb_owner(NULL), sb_data(data), sb_length(len) {
        memset(&this->sb_link, 0, sizeof(this->sb_link));
    };

    ~shared_buffer_ref() {
        this->disown();
    };

    shared_buffer_ref(const shared_buffer_ref &other) {
        this->sb_owner = NULL;
        this->sb_data = NULL;
        this->sb_length = 0;

        this->copy_ref(other);
    };

    shared_buffer_ref &operator=(const shared_buffer_ref &other) {
        if (this != &other) {
            this->disown();
            this->copy_ref(other);
        }

        return *this;
    };

    bool empty() const { return this->sb_data == NULL; };

    const char *get_data() const { return this->sb_data; };

    const char *get_data_at(off_t offset) const {
        return &this->sb_data[offset];
    };

    size_t length() const { return this->sb_length; };

    bool contains(const char *ptr) const {
        const char *buffer_end = this->sb_data + this->sb_length;

        return (this->sb_data <= ptr && ptr < buffer_end);
    };

    char *get_writable_data() {
        if (this->take_ownership()) {
            return this->sb_data;
        }

        return NULL;
    };

    void share(shared_buffer &sb, char *data, size_t len);

    bool subset(shared_buffer_ref &other, off_t offset, size_t len);

    bool take_ownership() {
        if (this->sb_owner != NULL && this->sb_data != NULL) {
            char *new_data;
        
            if ((new_data = (char *)malloc(this->sb_length)) == NULL) {
                return false;
            }

            memcpy(new_data, this->sb_data, this->sb_length);
            this->sb_data = new_data;
            LIST_REMOVE(this, sb_link);
            this->sb_owner = NULL;
        }
        return true;
    };

    void disown() {
        if (this->sb_owner == NULL) {
            if (this->sb_data != NULL) {
                free(this->sb_data);
            }
        } else {
            LIST_REMOVE(this, sb_link);
        }
        this->sb_owner = NULL;
        this->sb_data = NULL;
        this->sb_length = 0;
    };

    LIST_ENTRY(shared_buffer_ref) sb_link;
private:
    void copy_ref(const shared_buffer_ref &other) {
        if (other.sb_data == NULL) {
            this->sb_owner = NULL;
            this->sb_data = NULL;
            this->sb_length = 0;
        }
        else if (other.sb_owner != NULL) {
            this->share(*other.sb_owner, other.sb_data, other.sb_length);
        } else {
            this->sb_owner = NULL;
            this->sb_data = (char *)malloc(other.sb_length);
            memcpy(this->sb_data, other.sb_data, other.sb_length);
            this->sb_length = other.sb_length;
        }
    }

    auto_mem<char *> sb_backtrace;
    shared_buffer *sb_owner;
    char *sb_data;
    size_t sb_length;
};

class shared_buffer {
public:
    shared_buffer() {
        LIST_INIT(&this->sb_refs);
    };

    ~shared_buffer() {
        this->invalidate_refs();
    }

    void add_ref(shared_buffer_ref &ref) {
        LIST_INSERT_HEAD(&this->sb_refs, &ref, sb_link);
    };

    bool invalidate_refs() {
        shared_buffer_ref *ref;
        bool retval = true;

        for (ref = LIST_FIRST(&this->sb_refs);
             ref != NULL;
             ref = LIST_FIRST(&this->sb_refs)) {
            retval = retval && ref->take_ownership();
        }

        return retval;
    };

    LIST_HEAD(shared_buffer_head, shared_buffer_ref) sb_refs;
};

struct tmp_shared_buffer {
    tmp_shared_buffer(const char *str, size_t len = -1) {
        if (len == (size_t)-1) {
            len = strlen(str);
        }

        this->tsb_ref.share(this->tsb_manager, (char *)str, len);
    };

    shared_buffer tsb_manager;
    shared_buffer_ref tsb_ref;
};

#endif
