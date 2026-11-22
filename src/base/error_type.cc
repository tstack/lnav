/**
 * Copyright (c) 2026, Timothy Stack
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
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "error_type.hh"

#include <utility>

#include "config.h"

namespace lnav {

const DIST_SLICE(error_types) error_type_t ERR_FILE_NOT_FOUND(
    "file-not-found", 404, "A file that was asked for does not exist");

const DIST_SLICE(error_types) error_type_t
    ERR_BUSY("busy",
             503,
             "lnav is busy with something that the request needs, like "
             "scanning the source paths, so the request can be tried again "
             "later");

const error_type_t::type_container&
error_type_t::get_all_types()
{
    static auto retval = DIST_SLICE_CONTAINER(error_type_t, error_types);

    return retval;
}

std::optional<const error_type_t*>
error_type_t::find_type(const string_fragment& name)
{
    for (const auto& et : get_all_types()) {
        if (et.get_name() == name) {
            return &et;
        }
    }

    return std::nullopt;
}

static thread_local const error_type_t* tl_pending_error_type = nullptr;

void
set_pending_error_type(const error_type_t& et)
{
    tl_pending_error_type = &et;
}

const error_type_t*
take_pending_error_type()
{
    return std::exchange(tl_pending_error_type, nullptr);
}

}  // namespace lnav
