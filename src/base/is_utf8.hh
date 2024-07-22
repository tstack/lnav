/*
 * is_utf8 is distributed under the following terms:
 *
 * Copyright (c) 2013 Palard Julien. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _IS_UTF8_H
#define _IS_UTF8_H

#include <stdlib.h>
#include <sys/types.h>

#include "intern_string.hh"

struct utf8_scan_result {
    const char* usr_message{nullptr};
    size_t usr_faulty_bytes{0};
    string_fragment usr_valid_frag{string_fragment::invalid()};
    std::optional<string_fragment> usr_remaining;
    bool usr_has_ansi{false};
    size_t usr_column_width_guess{0};

    const char* remaining_ptr(const string_fragment& frag) const
    {
        if (this->usr_remaining) {
            return this->usr_remaining->begin();
        } else {
            return nullptr;
        }
    }
    bool is_valid() const { return this->usr_message == nullptr; }
};

utf8_scan_result is_utf8(string_fragment frag,
                         std::optional<unsigned char> terminator
                         = std::nullopt);

#endif /* _IS_UTF8_H */
