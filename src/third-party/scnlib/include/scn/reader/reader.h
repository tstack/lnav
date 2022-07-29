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

#ifndef SCN_READER_READER_H
#define SCN_READER_READER_H

#include "common.h"
#include "float.h"
#include "int.h"
#include "string.h"
#include "types.h"

#include "../detail/args.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    template <>
    struct scanner<code_point> : public detail::code_point_scanner {
    };
    template <>
    struct scanner<bool> : public detail::bool_scanner {
    };
    template <>
    struct scanner<char> : public detail::integer_scanner<char> {
    };
    template <>
    struct scanner<wchar_t> : public detail::integer_scanner<wchar_t> {
    };
    template <>
    struct scanner<signed char> : public detail::integer_scanner<signed char> {
    };
    template <>
    struct scanner<short> : public detail::integer_scanner<short> {
    };
    template <>
    struct scanner<int> : public detail::integer_scanner<int> {
    };
    template <>
    struct scanner<long> : public detail::integer_scanner<long> {
    };
    template <>
    struct scanner<long long> : public detail::integer_scanner<long long> {
    };
    template <>
    struct scanner<unsigned char>
        : public detail::integer_scanner<unsigned char> {
    };
    template <>
    struct scanner<unsigned short>
        : public detail::integer_scanner<unsigned short> {
    };
    template <>
    struct scanner<unsigned int>
        : public detail::integer_scanner<unsigned int> {
    };
    template <>
    struct scanner<unsigned long>
        : public detail::integer_scanner<unsigned long> {
    };
    template <>
    struct scanner<unsigned long long>
        : public detail::integer_scanner<unsigned long long> {
    };
    template <>
    struct scanner<float> : public detail::float_scanner<float> {
    };
    template <>
    struct scanner<double> : public detail::float_scanner<double> {
    };
    template <>
    struct scanner<long double> : public detail::float_scanner<long double> {
    };
    template <typename CharT, typename Allocator>
    struct scanner<std::basic_string<CharT, std::char_traits<CharT>, Allocator>>
        : public detail::string_scanner {
    };
    template <typename CharT>
    struct scanner<span<CharT>> : public detail::span_scanner {
    };
    template <typename CharT>
    struct scanner<basic_string_view<CharT>>
        : public detail::string_view_scanner {
    };
#if SCN_HAS_STRING_VIEW
    template <typename CharT>
    struct scanner<std::basic_string_view<CharT>>
        : public detail::std_string_view_scanner {
    };
#endif
    template <>
    struct scanner<detail::monostate>;

    SCN_END_NAMESPACE
}  // namespace scn

#endif
