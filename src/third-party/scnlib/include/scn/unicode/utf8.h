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

#ifndef SCN_UNICODE_UTF8_H
#define SCN_UNICODE_UTF8_H

#include "../detail/error.h"
#include "../util/expected.h"
#include "common.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        namespace utf8 {
            template <typename Octet>
            constexpr bool is_trail(Octet o)
            {
                return (mask8(o) >> 6) == 2;
            }

            template <typename Octet>
            SCN_CONSTEXPR14 int get_sequence_length(Octet ch)
            {
                uint8_t lead = detail::mask8(ch);
                if (lead < 0x80) {
                    return 1;
                }
                else if ((lead >> 5) == 6) {
                    return 2;
                }
                else if ((lead >> 4) == 0xe) {
                    return 3;
                }
                else if ((lead >> 3) == 0x1e) {
                    return 4;
                }
                return 0;
            }

            SCN_CONSTEXPR14 bool is_overlong_sequence(code_point cp,
                                                      std::ptrdiff_t len)
            {
                if (cp < 0x80) {
                    if (len != 1) {
                        return true;
                    }
                }
                else if (cp < 0x800) {
                    if (len != 2) {
                        return true;
                    }
                }
                else if (cp < 0x10000) {
                    if (len != 3) {
                        return true;
                    }
                }

                return false;
            }

            template <typename I, typename S>
            SCN_CONSTEXPR14 error increase_safely(I& it, S end)
            {
                if (++it == end) {
                    return {error::invalid_encoding,
                            "Unexpected end of range when decoding utf8 "
                            "(partial codepoint)"};
                }
                if (!is_trail(*it)) {
                    return {error::invalid_encoding,
                            "Invalid utf8 codepoint parsed"};
                }
                return {};
            }

            template <typename I, typename S>
            SCN_CONSTEXPR14 error get_sequence_1(I& it, S end, code_point& cp)
            {
                SCN_EXPECT(it != end);
                cp = make_code_point(mask8(*it));
                return {};
            }
            template <typename I, typename S>
            SCN_CONSTEXPR14 error get_sequence_2(I& it, S end, code_point& cp)
            {
                SCN_EXPECT(it != end);
                uint32_t c = mask8(*it);

                auto e = increase_safely(it, end);
                if (!e) {
                    return e;
                }

                c = static_cast<uint32_t>((c << 6u) & 0x7ffu) +
                    (static_cast<uint32_t>(*it) & 0x3fu);
                cp = make_code_point(c);

                return {};
            }
            template <typename I, typename S>
            SCN_CONSTEXPR14 error get_sequence_3(I& it, S end, code_point& cp)
            {
                SCN_EXPECT(it != end);
                uint32_t c = mask8(*it);

                auto e = increase_safely(it, end);
                if (!e) {
                    return e;
                }

                c = static_cast<uint32_t>((c << 12u) & 0xffffu) +
                    (static_cast<uint32_t>(mask8(*it) << 6u) & 0xfffu);

                e = increase_safely(it, end);
                if (!e) {
                    return e;
                }

                c += static_cast<uint32_t>(*it) & 0x3fu;
                cp = make_code_point(c);

                return {};
            }
            template <typename I, typename S>
            SCN_CONSTEXPR14 error get_sequence_4(I& it, S end, code_point& cp)
            {
                SCN_EXPECT(it != end);
                uint32_t c = mask8(*it);

                auto e = increase_safely(it, end);
                if (!e) {
                    return e;
                }

                c = ((c << 18u) & 0x1fffffu) +
                    (static_cast<uint32_t>(mask8(*it) << 12u) & 0x3ffffu);

                e = increase_safely(it, end);
                if (!e) {
                    return e;
                }

                c += static_cast<uint32_t>(mask8(*it) << 6u) & 0xfffu;

                e = increase_safely(it, end);
                if (!e) {
                    return e;
                }

                c += static_cast<uint32_t>(*it) & 0x3fu;
                cp = make_code_point(c);

                return {};
            }

            template <typename I, typename S>
            SCN_CONSTEXPR14 error validate_next(I& it, S end, code_point& cp)
            {
                SCN_EXPECT(it != end);

                int len = get_sequence_length(*it);
                error e{};
                switch (len) {
                    case 1:
                        e = get_sequence_1(it, end, cp);
                        break;
                    case 2:
                        e = get_sequence_2(it, end, cp);
                        break;
                    case 3:
                        e = get_sequence_3(it, end, cp);
                        break;
                    case 4:
                        e = get_sequence_4(it, end, cp);
                        break;
                    default:
                        return {error::invalid_encoding,
                                "Invalid lead byte for utf8"};
                }

                if (!e) {
                    return e;
                }
                if (!is_valid_code_point(cp) || is_overlong_sequence(cp, len)) {
                    return {error::invalid_encoding, "Invalid utf8 code point"};
                }
                ++it;
                return {};
            }

            template <typename I, typename S>
            SCN_CONSTEXPR14 expected<I> parse_code_point(I begin,
                                                         S end,
                                                         code_point& cp)
            {
                code_point c{};
                auto e = validate_next(begin, end, c);
                if (e) {
                    cp = c;
                    return {begin};
                }
                return e;
            }

            template <typename I>
            I append(code_point cp, I it)
            {
                SCN_EXPECT(is_code_point_valid(cp));

                if (cp < 0x80) {
                    *(it++) = static_cast<uint8_t>(cp);
                }
                else if (cp < 0x800) {
                    *(it++) = static_cast<uint8_t>(
                        (static_cast<uint32_t>(cp) >> 6u) | 0xc0u);
                    *(it++) = static_cast<uint8_t>(
                        (static_cast<uint32_t>(cp) & 0x3fu) | 0x80u);
                }
                else if (cp < 0x10000) {
                    *(it++) = static_cast<uint8_t>(
                        (static_cast<uint32_t>(cp) >> 12u) | 0xe0u);
                    *(it++) = static_cast<uint8_t>(
                        ((static_cast<uint32_t>(cp) >> 6u) & 0x3fu) | 0x80u);
                    *(it++) = static_cast<uint8_t>(
                        (static_cast<uint32_t>(cp) & 0x3fu) | 0x80u);
                }
                else {
                    *(it++) = static_cast<uint8_t>(
                        (static_cast<uint32_t>(cp) >> 18u) | 0xf0u);
                    *(it++) = static_cast<uint8_t>(
                        ((static_cast<uint32_t>(cp) >> 12u) & 0x3fu) | 0x80u);
                    *(it++) = static_cast<uint8_t>(
                        ((static_cast<uint32_t>(cp) >> 6u) & 0x3fu) | 0x80u);
                    *(it++) = static_cast<uint8_t>(
                        (static_cast<uint32_t>(cp) & 0x3fu) | 0x80u);
                }
                return it;
            }

            template <typename I, typename S>
            SCN_CONSTEXPR14 expected<I> encode_code_point(I begin,
                                                          S end,
                                                          code_point cp)
            {
                SCN_EXPECT(begin + 4 <= end);

                if (!is_code_point_valid(cp)) {
                    return error(error::invalid_encoding,
                                 "Invalid code point, cannot encode in UTF-8");
                }
                return {append(cp, begin)};
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

        }  // namespace utf8
    }      // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn

#endif
