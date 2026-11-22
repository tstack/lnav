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

#ifndef lnav_error_type_hh
#define lnav_error_type_hh

#include <cstddef>
#include <cstdint>
#include <optional>

#include "distributed_slice.hh"
#include "intern_string.hh"

namespace lnav {

/**
 * A kind of error that a user_message can be marked with, so a program
 * reading the message, like a client of the external-access server, can tell
 * the errors apart without reading their text.  Each one is defined next to
 * the code that raises it, in the "error_types" distributed slice, like:
 *
 *   const DIST_SLICE(error_types) lnav::error_type_t ERR_EXAMPLE(
 *       "example", 422, "What went wrong");
 *
 * so there is no list of them to keep up to date.  The name is what clients
 * see, so it must be unique and should not change.
 */
class error_type_t {
public:
    using type_container = dist_slice_container<error_type_t>;

    static const type_container& get_all_types();

    static std::optional<const error_type_t*> find_type(
        const string_fragment& name);

    template<std::size_t N, std::size_t D>
    constexpr error_type_t(const char (&name)[N],
                           uint16_t http_status,
                           const char (&description)[D])
        : et_name(string_fragment::from_const(name)),
          et_http_status(http_status),
          et_description(string_fragment::from_const(description))
    {
    }

    ~error_type_t() = default;

    error_type_t(const error_type_t&) = delete;
    error_type_t(error_type_t&&) = delete;

    error_type_t& operator=(const error_type_t&) = delete;
    error_type_t& operator=(error_type_t&&) = delete;

    const string_fragment& get_name() const { return this->et_name; }

    /** The HTTP status of a request that fails with this error. */
    uint16_t get_http_status() const { return this->et_http_status; }

    const string_fragment& get_description() const
    {
        return this->et_description;
    }

private:
    const string_fragment et_name;
    const uint16_t et_http_status;
    const string_fragment et_description;
};

/** A file that was asked for does not exist. */
extern const error_type_t ERR_FILE_NOT_FOUND;

/** lnav is busy with something the request needs, and can be asked again. */
extern const error_type_t ERR_BUSY;

/**
 * Errors raised inside of SQLite, like by an SQL function, only reach the
 * statement as text.  The code raising one marks it here, and the code that
 * turns the statement's failure into a user_message takes it.
 */
void set_pending_error_type(const error_type_t& et);

/** The error type marked since the last call, if any, which is cleared. */
const error_type_t* take_pending_error_type();

}  // namespace lnav

#endif
