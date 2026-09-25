/**
 * Copyright (c) 2025, Timothy Stack
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
 */

#ifndef lnav_ryml_hh
#define lnav_ryml_hh

#include <stdexcept>

#include <ctype.h>
#include <string>

#include "intern_string.hh"
#include "ryml_all.hpp"

namespace lnav::ryml {

inline ::ryml::csubstr
to_csubstr(const string_fragment& sf)
{
    return {sf.data(), (size_t) sf.length()};
}

/**
 * @return Callbacks for a ryml::Parser that throw a std::runtime_error on an
 * error.  The default callbacks call abort().
 */
inline ::ryml::Callbacks
throwing_callbacks()
{
    return ::ryml::Callbacks(
        nullptr,
        nullptr,
        nullptr,
        +[](const char* msg, size_t len, ::ryml::Location, void*) {
            // Some messages count their NUL terminator in the length and
            // others end with newlines.
            while (len > 0
                   && (msg[len - 1] == '\0'
                       || isspace((unsigned char) msg[len - 1])))
            {
                len -= 1;
            }
            throw std::runtime_error(std::string(msg, len));
        });
}

}  // namespace lnav::ryml

#endif
