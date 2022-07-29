// Copyright 2017 Elias Kosunen
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This file is a part of scnlib:
//     https://github.com/eliaskosunen/scnlib
//
// The contents of this file are based on utfcpp:
//     https://github.com/nemtrif/utfcpp
//     Copyright (c) 2006 Nemanja Trifunovic
//     Distributed under the Boost Software License, version 1.0

#ifndef SCN_UNICODE_UTF16_H
#define SCN_UNICODE_UTF16_H

#include "../detail/error.h"
#include "../util/expected.h"
#include "common.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        namespace utf16 {
            template <typename U16>
            SCN_CONSTEXPR14 int get_sequence_length(U16 ch)
            {
                uint16_t lead = mask16(ch);
                if (is_lead_surrogate(lead)) {
                    return 2;
                }
                if (SCN_UNLIKELY(is_trail_surrogate(lead))) {
                    return 0;
                }
                return 1;
            }

            template <typename I, typename S>
            SCN_CONSTEXPR14 error validate_next(I& it, S end, code_point& cp)
            {
                SCN_EXPECT(it != end);

                uint16_t lead = mask16(*it);
                if (is_lead_surrogate(lead)) {
                    ++it;
                    if (it == end) {
                        return {error::invalid_encoding,
                                "Lone utf16 lead surrogate"};
                    }
                    uint16_t trail = mask16(*it);
                    if (!is_trail_surrogate(trail)) {
                        return {error::invalid_encoding,
                                "Invalid utf16 trail surrogate"};
                    }
                    ++it;
                    cp = static_cast<code_point>(
                        static_cast<uint32_t>(lead << 10u) + trail +
                        surrogate_offset);
                    return {};
                }
                if (is_trail_surrogate(lead)) {
                    return {error::invalid_encoding,
                            "Lone utf16 trail surrogate"};
                }

                cp = static_cast<code_point>(*it);
                ++it;
                return {};
            }

            template <typename I, typename S>
            SCN_CONSTEXPR14 expected<I> parse_code_point(I begin,
                                                         S end,
                                                         code_point& cp)
            {
                auto e = validate_next(begin, end, cp);
                if (!e) {
                    return e;
                }
                return {begin};
            }

            template <typename I, typename S>
            SCN_CONSTEXPR14 expected<I> encode_code_point(I begin,
                                                          S end,
                                                          code_point cp)
            {
                SCN_EXPECT(begin + 2 <= end);

                if (!is_valid_code_point(cp)) {
                    return error(error::invalid_encoding,
                                 "Invalid code point, cannot encode in UTF-16");
                }

                if (cp > 0xffffu) {
                    *begin++ = static_cast<uint16_t>(
                        (static_cast<uint32_t>(cp) >> 10u) + lead_offset);
                    *begin++ = static_cast<uint16_t>(
                        (static_cast<uint32_t>(cp) & 0x3ffu) +
                        trail_surrogate_min);
                }
                else {
                    *begin++ = static_cast<uint16_t>(cp);
                }
                return {begin};
            }

            template <typename I, typename S>
            SCN_CONSTEXPR14 expected<std::ptrdiff_t> code_point_distance(
                I begin,
                S end)
            {
                std::ptrdiff_t dist{};
                code_point cp{};
                for (; begin < end; ++dist) {
                    auto e = validate_next(begin, end, cp);
                    if (!e) {
                        return e;
                    }
                }
                return {dist};
            }
        }  // namespace utf16
    }      // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn

#endif
