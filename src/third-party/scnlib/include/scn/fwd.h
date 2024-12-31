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

#pragma once

#ifndef __cplusplus
#error "scnlib is a C++ library"
#endif

#define SCN_COMPILER(major, minor, patch) \
    ((major) * 10000000 + (minor) * 10000 + (patch))

#define SCN_VERSION SCN_COMPILER(4, 0, 1)

/////////////////////////////////////////////////////////////////
// Library configuration
/////////////////////////////////////////////////////////////////

// SCN_USE_EXCEPTIONS
// If 0, removes all `noexcept` annotations,
// and exception handling around stdlib facilities.
#ifndef SCN_USE_EXCEPTIONS
#define SCN_USE_EXCEPTIONS 1
#endif

// SCN_USE_TRIVIAL_ABI
// If 1, uses [[clang::trivial_abi]] in some classes, if available.
#ifndef SCN_USE_TRIVIAL_ABI
#define SCN_USE_TRIVIAL_ABI 1
#endif

// SCN_DISABLE_REGEX
// If 1, disabled regular expression support
#ifndef SCN_DISABLE_REGEX
#define SCN_DISABLE_REGEX 0
#endif

// SCN_REGEX_BOOST_USE_ICU
// If 1, use ICU for full Unicode support with the regex backend
// Only effective when SCN_REGEX_BACKEND is Boost
#ifndef SCN_REGEX_BOOST_USE_ICU
#define SCN_REGEX_BOOST_USE_ICU 0
#endif

// std::regex
#define SCN_REGEX_BACKEND_STD   0
// Boost.Regex
#define SCN_REGEX_BACKEND_BOOST 1
// Google RE2
#define SCN_REGEX_BACKEND_RE2   2

// Default to std::regex
#ifndef SCN_REGEX_BACKEND
#define SCN_REGEX_BACKEND SCN_REGEX_BACKEND_STD
#endif

#if SCN_REGEX_BACKEND < SCN_REGEX_BACKEND_STD || \
    SCN_REGEX_BACKEND > SCN_REGEX_BACKEND_RE2
#error "Invalid regex backend"
#endif

#if SCN_REGEX_BOOST_USE_ICU && SCN_REGEX_BACKEND != SCN_REGEX_BACKEND_BOOST
#error "SCN_REGEX_BOOST_USE_ICU requires the Boost SCN_REGEX_BACKEND"
#endif

#if SCN_REGEX_BACKEND == SCN_REGEX_BACKEND_STD
#define SCN_REGEX_SUPPORTS_NAMED_CAPTURES 0
#else
#define SCN_REGEX_SUPPORTS_NAMED_CAPTURES 1
#endif

#if SCN_REGEX_BACKEND == SCN_REGEX_BACKEND_RE2
#define SCN_REGEX_SUPPORTS_WIDE_STRINGS 0
#else
#define SCN_REGEX_SUPPORTS_WIDE_STRINGS 1
#endif

#if SCN_REGEX_BACKEND == SCN_REGEX_BACKEND_RE2 || SCN_REGEX_BOOST_USE_ICU
#define SCN_REGEX_SUPPORTS_UTF8_CLASSIFICATION 1
#else
#define SCN_REGEX_SUPPORTS_UTF8_CLASSIFICATION 0
#endif

// SCN_DISABLE_IOSTREAM
// If 1, removes all references and functionality related to standard streams.
#ifndef SCN_DISABLE_IOSTREAM
#define SCN_DISABLE_IOSTREAM 0
#endif

// SCN_DISABLE_LOCALE
// If 1, removes all references to std::locale, and C locale
#ifndef SCN_DISABLE_LOCALE
#define SCN_DISABLE_LOCALE 0
#endif

// SCN_DISABLE_FROM_CHARS
// If 1, disallows the float scanner from falling back on std::from_chars,
// even if it were available
#ifndef SCN_DISABLE_FROM_CHARS
#define SCN_DISABLE_FROM_CHARS 0
#endif

// SCN_DISABLE_STRTOD
// If 1, disallows the float scanner from falling back on std::strtod,
// even if it were available
#ifndef SCN_DISABLE_STRTOD
#define SCN_DISABLE_STRTOD 0
#endif

// SCN_DISABLE_CHRONO
// If 1, disables all <chrono> and <ctime> scanners
#ifndef SCN_DISABLE_CHRONO
#define SCN_DISABLE_CHRONO 0
#endif

// SCN_DISABLE_TYPE_*
// If 1, removes ability to scan type
#ifndef SCN_DISABLE_TYPE_SCHAR
#define SCN_DISABLE_TYPE_SCHAR 0
#endif
#ifndef SCN_DISABLE_TYPE_SHORT
#define SCN_DISABLE_TYPE_SHORT 0
#endif
#ifndef SCN_DISABLE_TYPE_INT
#define SCN_DISABLE_TYPE_INT 0
#endif
#ifndef SCN_DISABLE_TYPE_LONG
#define SCN_DISABLE_TYPE_LONG 0
#endif
#ifndef SCN_DISABLE_TYPE_LONG_LONG
#define SCN_DISABLE_TYPE_LONG_LONG 0
#endif
#ifndef SCN_DISABLE_TYPE_UCHAR
#define SCN_DISABLE_TYPE_UCHAR 0
#endif
#ifndef SCN_DISABLE_TYPE_USHORT
#define SCN_DISABLE_TYPE_USHORT 0
#endif
#ifndef SCN_DISABLE_TYPE_UINT
#define SCN_DISABLE_TYPE_UINT 0
#endif
#ifndef SCN_DISABLE_TYPE_ULONG
#define SCN_DISABLE_TYPE_ULONG 0
#endif
#ifndef SCN_DISABLE_TYPE_ULONG_LONG
#define SCN_DISABLE_TYPE_ULONG_LONG 0
#endif
#ifndef SCN_DISABLE_TYPE_POINTER
#define SCN_DISABLE_TYPE_POINTER 0
#endif
#ifndef SCN_DISABLE_TYPE_BOOL
#define SCN_DISABLE_TYPE_BOOL 0
#endif
#ifndef SCN_DISABLE_TYPE_CHAR
#define SCN_DISABLE_TYPE_CHAR 0
#endif
#ifndef SCN_DISABLE_TYPE_CHAR32
#define SCN_DISABLE_TYPE_CHAR32 0
#endif
#ifndef SCN_DISABLE_TYPE_FLOAT
#define SCN_DISABLE_TYPE_FLOAT 0
#endif
#ifndef SCN_DISABLE_TYPE_DOUBLE
#define SCN_DISABLE_TYPE_DOUBLE 0
#endif
#ifndef SCN_DISABLE_TYPE_LONG_DOUBLE
#define SCN_DISABLE_TYPE_LONG_DOUBLE 0
#endif
#ifndef SCN_DISABLE_TYPE_STRING
#define SCN_DISABLE_TYPE_STRING 0
#endif
#ifndef SCN_DISABLE_TYPE_STRING_VIEW
#define SCN_DISABLE_TYPE_STRING_VIEW 0
#endif
#ifndef SCN_DISABLE_TYPE_CUSTOM
#define SCN_DISABLE_TYPE_CUSTOM 0
#endif

/////////////////////////////////////////////////////////////////
// Detect compiler
/////////////////////////////////////////////////////////////////

#ifdef __INTEL_COMPILER
// Intel
#define SCN_INTEL                                                      \
    SCN_COMPILER(__INTEL_COMPILER / 100, (__INTEL_COMPILER / 10) % 10, \
                 __INTEL_COMPILER % 10)
#elif defined(__clang__) && defined(__clang_minor__) && \
    defined(__clang_patchlevel__)
// Clang
#define SCN_CLANG \
    SCN_COMPILER(__clang_major__, __clang_minor__, __clang_patchlevel__)
#elif defined(__GNUC__) && defined(__GNUC_MINOR__) && \
    defined(__GNUC_PATCHLEVEL__)
// GCC
#define SCN_GCC SCN_COMPILER(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)
#endif

#if defined(_MSC_VER) && defined(_MSC_FULL_VER)
// MSVC
#if _MSC_VER == _MSC_FULL_VER / 10000
#define SCN_MSVC \
    SCN_COMPILER(_MSC_VER / 100, _MSC_VER % 100, _MSC_FULL_VER % 10000)
#else
#define SCN_MSVC                                                \
    SCN_COMPILER(_MSC_VER / 100, (_MSC_FULL_VER / 100000) % 10, \
                 _MSC_FULL_VER % 100000)
#endif  // _MSC_VER == _MSC_FULL_VER / 10000
#endif  // _MSC_VER

#ifndef SCN_INTEL
#define SCN_INTEL 0
#endif
#ifndef SCN_MSVC
#define SCN_MSVC 0
#endif
#ifndef SCN_CLANG
#define SCN_CLANG 0
#endif
#ifndef SCN_GCC
#define SCN_GCC 0
#endif

#if SCN_CLANG && SCN_MSVC
#define SCN_MSVC_CLANG 1
#else
#define SCN_MSVC_CLANG 0
#endif

// Pretending to be gcc (clang, icc, etc.)
#ifdef __GNUC__

#ifdef __GNUC_MINOR__
#define SCN_GCC_COMPAT_MINOR __GNUC_MINOR__
#else
#define SCN_GCC_COMPAT_MINOR 0
#endif

#ifdef __GNUC_PATCHLEVEL__
#define SCN_GCC_COMPAT_PATCHLEVEL __GNUC_PATCHLEVEL__
#else
#define SCN_GCC_COMPAT_PATCHLEVEL 0
#endif

#define SCN_GCC_COMPAT \
    SCN_COMPILER(__GNUC__, SCN_GCC_COMPAT_MINOR, SCN_GCC_COMPAT_PATCHLEVEL)
#else
#define SCN_GCC_COMPAT 0
#endif  // #ifdef __GNUC__

/////////////////////////////////////////////////////////////////
// Warning control
/////////////////////////////////////////////////////////////////

#if SCN_GCC
#define SCN_PRAGMA_APPLY(x) _Pragma(#x)

#define SCN_GCC_PUSH        _Pragma("GCC diagnostic push")
#define SCN_GCC_POP         _Pragma("GCC diagnostic pop")

#define SCN_GCC_IGNORE(x)   SCN_PRAGMA_APPLY(GCC diagnostic ignored x)
#else
#define SCN_GCC_PUSH
#define SCN_GCC_POP
#define SCN_GCC_IGNORE(x)
#endif

#if SCN_CLANG
#define SCN_PRAGMA_APPLY(x) _Pragma(#x)

#define SCN_CLANG_PUSH      _Pragma("clang diagnostic push")
#define SCN_CLANG_POP       _Pragma("clang diagnostic pop")

#define SCN_CLANG_IGNORE(x) SCN_PRAGMA_APPLY(clang diagnostic ignored x)

#if SCN_CLANG >= SCN_COMPILER(3, 9, 0)
#define SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE \
    SCN_CLANG_PUSH SCN_CLANG_IGNORE("-Wundefined-func-template")
#define SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE SCN_CLANG_POP
#else
#define SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
#define SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
#endif

#if SCN_CLANG >= SCN_COMPILER(16, 0, 0)
#define SCN_CLANG_PUSH_IGNORE_UNSAFE_BUFFER_USAGE \
    SCN_CLANG_PUSH SCN_CLANG_IGNORE("-Wunsafe-buffer-usage")
#define SCN_CLANG_POP_IGNORE_UNSAFE_BUFFER_USAGE SCN_CLANG_POP
#else
#define SCN_CLANG_PUSH_IGNORE_UNSAFE_BUFFER_USAGE
#define SCN_CLANG_POP_IGNORE_UNSAFE_BUFFER_USAGE
#endif

#else
#define SCN_CLANG_PUSH
#define SCN_CLANG_POP
#define SCN_CLANG_IGNORE(x)
#define SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
#define SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
#define SCN_CLANG_PUSH_IGNORE_UNSAFE_BUFFER_USAGE
#define SCN_CLANG_POP_IGNORE_UNSAFE_BUFFER_USAGE
#endif

#if SCN_GCC_COMPAT && defined(SCN_PRAGMA_APPLY)
#define SCN_GCC_COMPAT_PUSH      SCN_PRAGMA_APPLY(GCC diagnostic push)
#define SCN_GCC_COMPAT_POP       SCN_PRAGMA_APPLY(GCC diagnostic pop)
#define SCN_GCC_COMPAT_IGNORE(x) SCN_PRAGMA_APPLY(GCC diagnostic ignored x)
#else
#define SCN_GCC_COMPAT_PUSH
#define SCN_GCC_COMPAT_POP
#define SCN_GCC_COMPAT_IGNORE(x)
#endif

#if SCN_MSVC
#define SCN_MSVC_PUSH      __pragma(warning(push))
#define SCN_MSVC_POP       __pragma(warning(pop))

#define SCN_MSVC_IGNORE(x) __pragma(warning(disable : x))
#else
#define SCN_MSVC_PUSH
#define SCN_MSVC_POP
#define SCN_MSVC_IGNORE(x)
#endif

#ifdef __has_include
#define SCN_HAS_INCLUDE(x) __has_include(x)
#else
#define SCN_HAS_INCLUDE(x) 0
#endif

/////////////////////////////////////////////////////////////////
// Standard library includes
/////////////////////////////////////////////////////////////////

SCN_GCC_PUSH
SCN_GCC_IGNORE("-Wnoexcept")
SCN_GCC_IGNORE("-Wrestrict")

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#if SCN_MSVC && SCN_HAS_INCLUDE(<yvals.h>)
// The above headers don't define _ITERATOR_DEBUG_LEVEL,
// so include <yvals.h> directly
#include <yvals.h>
#endif

SCN_GCC_POP

/////////////////////////////////////////////////////////////////
// Environment detection (preprocessor only)
/////////////////////////////////////////////////////////////////

#define SCN_STD_17 201703L
#define SCN_STD_20 202002L
#define SCN_STD_23 202302L

// Stdlib detect: libstdc++
#ifdef _GLIBCXX_RELEASE
#define SCN_STDLIB_GLIBCXX _GLIBCXX_RELEASE
#elif defined(__GLIBCXX__)
#define SCN_STDLIB_GLIBCXX 1
#else
#define SCN_STDLIB_GLIBCXX 0
#endif

// libc++
#ifdef _LIBCPP_VERSION
#define SCN_STDLIB_LIBCPP _LIBCPP_VERSION
#else
#define SCN_STDLIB_LIBCPP 0
#endif

// MSVC STL
#ifdef _MSVC_STL_VERSION
#define SCN_STDLIB_MS_STL _MSVC_STL_VERSION
#else
#define SCN_STDLIB_MS_STL 0
#endif

// MSVC debug iterators
#if SCN_STDLIB_MS_STL && defined(_ITERATOR_DEBUG_LEVEL) && \
    _ITERATOR_DEBUG_LEVEL != 0
#define SCN_MSVC_DEBUG_ITERATORS 1
#else
#define SCN_MSVC_DEBUG_ITERATORS 0
#endif

// POSIX
#if defined(__unix__) || defined(__APPLE__)
#define SCN_POSIX 1
#else
#define SCN_POSIX 0
#endif

#if defined(__APPLE__)
#define SCN_APPLE 1
#else
#define SCN_APPLE 0
#endif

// Windows
#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32) || \
     defined(_WIN64)) &&                                      \
    !defined(__CYGWIN__)
#define SCN_WINDOWS 1
#else
#define SCN_WINDOWS 0
#endif

#if SCN_WINDOWS && defined(_WIN64)
#define SCN_WINDOWS_64BIT 1
#else
#define SCN_WINDOWS_64BIT 0
#endif

// MinGW
#if defined(__MINGW32__) || defined(__MINGW64__)
#define SCN_MINGW 1
#else
#define SCN_MINGW 0
#endif

#ifdef _MSVC_LANG
#define SCN_MSVC_LANG _MSVC_LANG
#else
#define SCN_MSVC_LANG 0
#endif

// Standard version
#if SCN_MSVC
#define SCN_STD SCN_MSVC_LANG
#else
#define SCN_STD __cplusplus
#endif

#ifdef __has_cpp_attribute
#define SCN_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define SCN_HAS_CPP_ATTRIBUTE(x) 0
#endif

#ifdef __has_builtin
#define SCN_HAS_BUILTIN(x) __has_builtin(x)
#else
#define SCN_HAS_BUILTIN(x) 0
#endif

#if defined(_SCN_DOXYGEN) && _SCN_DOXYGEN
#define SCN_DOXYGEN 1
#else
#define SCN_DOXYGEN 0
#endif

// Detect exceptions
#ifdef __cpp_exceptions
#define SCN_HAS_EXCEPTIONS 1
#endif

#if !defined(SCN_HAS_EXCEPTIONS) && defined(__EXCEPTIONS)
#define SCN_HAS_EXCEPTIONS 1
#endif

#if !defined(SCN_HAS_EXCEPTIONS) && defined(_HAS_EXCEPTIONS)
#if _HAS_EXCEPTIONS
#define SCN_HAS_EXCEPTIONS 1
#else
#define SCN_HAS_EXCEPTIONS 0
#endif
#endif

#if !defined(SCN_HAS_EXCEPTIONS) && !defined(_CPPUNWIND)
#define SCN_HAS_EXCEPTIONS 0
#endif

#ifndef SCN_HAS_EXCEPTIONS
#define SCN_HAS_EXCEPTIONS 0
#endif

#if SCN_GCC >= SCN_COMPILER(7, 0, 0)
#define SCN_HAS_CPP17_ATTRIBUTES 1
#elif SCN_CLANG >= SCN_COMPILER(3, 9, 0)
#define SCN_HAS_CPP17_ATTRIBUTES 1
#elif SCN_MSVC >= SCN_COMPILER(19, 11, 0)
#define SCN_HAS_CPP17_ATTRIBUTES 1
#elif SCN_INTEL >= SCN_COMPILER(18, 0, 0)
#define SCN_HAS_CPP17_ATTRIBUTES 1
#else
#define SCN_HAS_CPP17_ATTRIBUTES 0
#endif

// Detect [[nodiscard]]
#if SCN_HAS_CPP_ATTRIBUTE(nodiscard) >= 201603L
#define SCN_HAS_NODISCARD 1
#elif SCN_STD >= SCN_STD_17 && SCN_HAS_CPP17_ATTRIBUTES
#define SCN_HAS_NODISCARD 1
#else
#define SCN_HAS_NODISCARD 0
#endif

// Detect [[maybe_unused]]
#if SCN_HAS_CPP_ATTRIBUTE(maybe_unused) >= 201603L
#define SCN_HAS_MAYBE_UNUSED 1
#elif SCN_STD >= SCN_STD_17 && SCN_HAS_CPP17_ATTRIBUTES
#define SCN_HAS_MAYBE_UNUSED 1
#else
#define SCN_HAS_MAYBE_UNUSED 0
#endif

// Detect [[no_unique_address]]
#if SCN_MSVC >= SCN_COMPILER(19, 29, 0) && SCN_STD >= SCN_STD_20
#define SCN_HAS_NO_UNIQUE_ADDRESS_MSVC 1
#define SCN_HAS_NO_UNIQUE_ADDRESS_STD  0
#elif SCN_HAS_CPP_ATTRIBUTE(no_unique_address) >= 201803L && \
    SCN_STD >= SCN_STD_20
#define SCN_HAS_NO_UNIQUE_ADDRESS_MSVC 0
#define SCN_HAS_NO_UNIQUE_ADDRESS_STD  1
#else
#define SCN_HAS_NO_UNIQUE_ADDRESS_MSVC 0
#define SCN_HAS_NO_UNIQUE_ADDRESS_STD  0
#endif

// Detect [[fallthrough]]
#if SCN_HAS_CPP_ATTRIBUTE(fallthrough) >= 201603L
#define SCN_HAS_FALLTHROUGH_CPPATTRIBUTE 1
#elif SCN_STD >= SCN_STD_17 && \
    (SCN_HAS_CPP17_ATTRIBUTES || SCN_MSVC >= SCN_COMPILER(19, 10, 0))
#define SCN_HAS_FALLTHROUGH_CPPATTRIBUTE 1
#endif

#if SCN_HAS_CPP_ATTRIBUTE(gnu::fallthrough)
#define SCN_HAS_FALLTHROUGH_CPPGNUATTRIBUTE 1
#endif
#if SCN_HAS_CPP_ATTRIBUTE(clang::fallthrough)
#define SCN_HAS_FALLTHROUGH_CPPCLANGATTRIBUTE 1
#endif

#if SCN_GCC >= SCN_COMPILER(7, 0, 0)
#define SCN_HAS_FALLTHROUGH_GCCATTRIBUTE 1
#endif

#ifndef SCN_HAS_FALLTHROUGH_CPPATTRIBUTE
#define SCN_HAS_FALLTHROUGH_CPPATTRIBUTE 0
#endif
#ifndef SCN_HAS_FALLTHROUGH_CPPGNUATTRIBUTE
#define SCN_HAS_FALLTHROUGH_CPPGNUATTRIBUTE 0
#endif
#ifndef SCN_HAS_FALLTHROUGH_CPPCLANGATTRIBUTE
#define SCN_HAS_FALLTHROUGH_CPPCLANGATTRIBUTE 0
#endif
#ifndef SCN_HAS_FALLTHROUGH_GCCATTRIBUTE
#define SCN_HAS_FALLTHROUGH_GCCATTRIBUTE 0
#endif

// Detect [[likely]] and [[unlikely]]
#if SCN_STD >= SCN_STD_20

#if SCN_HAS_CPP_ATTRIBUTE(likely) >= 201803 && \
    SCN_HAS_CPP_ATTRIBUTE(unlikely) >= 201803
#define SCN_HAS_LIKELY_ATTR 1
#elif SCN_GCC >= SCN_COMPILER(9, 0, 0)
#define SCN_HAS_LIKELY_ATTR 1
#elif SCN_CLANG >= SCN_COMPILER(12, 0, 0)
#define SCN_HAS_LIKELY_ATTR 1
#elif SCN_MSVC >= SCN_COMPILER(19, 26, 0)
#define SCN_HAS_LIKELY_ATTR 1
#else
#define SCN_HAS_LIKELY_ATTR 0
#endif  // has_attr(likely && unlikely)

#else
#define SCN_HAS_LIKELY_ATTR 0
#endif  // SCN_STD >= 20

// Detect __attribute__((cold))
#if SCN_GCC || SCN_CLANG
#define SCN_COLD __attribute__((cold, noinline))
#else
#define SCN_COLD /* cold */
#endif

// Detect [[clang::trivial_abi]]
#if SCN_HAS_CPP_ATTRIBUTE(clang::trivial_abi)
#define SCN_HAS_TRIVIAL_ABI 1
#else
#define SCN_HAS_TRIVIAL_ABI 0
#endif

// Detect explicit(bool)
#if defined(__cpp_conditional_explicit) && \
    __cpp_conditional_explicit >= 201806L && SCN_STD >= SCN_STD_20
#define SCN_HAS_CONDITIONAL_EXPLICIT 1
#else
#define SCN_HAS_CONDITIONAL_EXPLICIT 0
#endif

// Detect <charconv>

#if SCN_STD >= SCN_STD_17

// libstdc++
#if SCN_STDLIB_GLIBCXX

#if SCN_STDLIB_GLIBCXX >= 9
#define SCN_HAS_INTEGER_CHARCONV 1
#else
#define SCN_HAS_INTEGER_CHARCONV 0
#endif

#if SCN_STDLIB_GLIBCXX >= 11
#define SCN_HAS_FLOAT_CHARCONV 1
#else
#define SCN_HAS_FLOAT_CHARCONV 0
#endif

// MSVC
#elif SCN_MSVC >= SCN_COMPILER(19, 14, 0)

#define SCN_HAS_INTEGER_CHARCONV 1

#if SCN_MSVC >= SCN_COMPILER(19, 21, 0)
#define SCN_HAS_FLOAT_CHARCONV 1
#else
#define SCN_HAS_FLOAT_CHARCONV 0
#endif

// libc++
#elif SCN_STDLIB_LIBCPP

#define SCN_HAS_FLOAT_CHARCONV 0

#if SCN_STDLIB_LIBCPP >= 7000
#define SCN_HAS_INTEGER_CHARCONV 1
#else
#define SCN_HAS_INTEGER_CHARCONV 0
#endif

// other
#elif defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201606L
#define SCN_HAS_INTEGER_CHARCONV 1
#define SCN_HAS_FLOAT_CHARCONV   1
#endif  // _GLIBCXX_RELEASE

#endif  // std >= 17

#ifndef SCN_HAS_INTEGER_CHARCONV
#define SCN_HAS_INTEGER_CHARCONV 0
#define SCN_HAS_FLOAT_CHARCONV   0
#endif

// Detect <bit> operations
#if defined(__cpp_lib_bitops) && __cpp_lib_bitops >= 201907L && \
    SCN_STD >= SCN_STD_20
#define SCN_HAS_BITOPS 1
#else
#define SCN_HAS_BITOPS 0
#endif

// Detect __assume
#if SCN_INTEL || SCN_MSVC
#define SCN_HAS_ASSUME 1
#else
#define SCN_HAS_ASSUME 0
#endif

// Detect __builtin_assume
#if SCN_HAS_BUILTIN(__builtin_assume)
#define SCN_HAS_BUILTIN_ASSUME 1
#else
#define SCN_HAS_BUILTIN_ASSUME 0
#endif

// Detect __builtin_assume_aligned
#if SCN_HAS_BUILTIN(__builtin_assume_aligned) || SCN_GCC
#define SCN_HAS_BUILTIN_ASSUME_ALIGNED 1
#else
#define SCN_HAS_BUILTIN_ASSUME_ALIGNED 0
#endif

// Detect __assume_aligned
#if SCN_HAS_BUILTIN(__assume_aligned) || SCN_INTEL
#define SCN_HAS_ASSUME_ALIGNED 1
#else
#define SCN_HAS_ASSUME_ALIGNED 0
#endif

// Detect __builtin_unreachable
#if SCN_HAS_BUILTIN(__builtin_unreachable) || SCN_GCC
#define SCN_HAS_BUILTIN_UNREACHABLE 1
#else
#define SCN_HAS_BUILTIN_UNREACHABLE 0
#endif

// Detect __builtin_expect
#if SCN_HAS_BUILTIN(__builtin_expect) || SCN_GCC
#define SCN_HAS_BUILTIN_EXPECT 1
#else
#define SCN_HAS_BUILTIN_EXPECT 0
#endif

// Detect __builtin_add_overflow etc.
#if SCN_HAS_BUILTIN(__builtin_add_overflow) || SCN_GCC
#define SCN_HAS_BUILTIN_OVERFLOW 1
#else
#define SCN_HAS_BUILTIN_OVERFLOW 0
#endif

// Detect char8_t
#if defined(__cpp_char8_t) && __cpp_char8_t >= 201811L
#define SCN_HAS_CHAR8 1
#else
#define SCN_HAS_CHAR8 0
#endif

// Detect consteval
#if defined(__cpp_consteval) && __cpp_consteval >= 201811L && \
    SCN_STD >= SCN_STD_20
#define SCN_HAS_CONSTEVAL 1
#else
#define SCN_HAS_CONSTEVAL 0
#endif

// Detect std::span
#if defined(__cpp_lib_span) && __cpp_lib_span >= 202002L && \
    SCN_STD >= SCN_STD_20
#define SCN_HAS_STD_SPAN 1
#else
#define SCN_HAS_STD_SPAN 0
#endif

// Detect std::regex_constants::multiline:
// libc++ 15 and later, or libstdc++ 11.4 or later
// (2023-05-28 is the date of the commit introducing `multiline`,
//  libstdc++ doesn't support checking for minor versions)
#if SCN_STDLIB_LIBCPP >= 15000 || SCN_STDLIB_GLIBCXX >= 12 || \
    (SCN_STDLIB_GLIBCXX == 11 && __GLIBCXX__ >= 20230528L)
#define SCN_HAS_STD_REGEX_MULTILINE 1
#else
#define SCN_HAS_STD_REGEX_MULTILINE 0
#endif

// Detect endianness
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SCN_IS_BIG_ENDIAN 1
#else
#define SCN_IS_BIG_ENDIAN 0
#endif

#elif SCN_WINDOWS
#define SCN_IS_BIG_ENDIAN 0
#else

#if SCN_APPLE
#include <machine/endian.h>
#elif defined(sun) || defined(__sun)
#include <sys/byteorder.h>
#elif SCN_HAS_INCLUDE(<endian.h>)
#include <endian.h>
#endif

#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__)
#define SCN_IS_BIG_ENDIAN 0
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SCN_IS_BIG_ENDIAN 0
#else
#define SCN_IS_BIG_ENDIAN 1
#endif

#endif  // defined __BYTE_ORDER__ && defined __ORDER_BIG_ENDIAN__

/////////////////////////////////////////////////////////////////
// Helper macros
/////////////////////////////////////////////////////////////////

#define SCN_STRINGIFY_APPLY(x) #x
#define SCN_STRINGIFY(x)       SCN_STRINGIFY_APPLY(x)
// SCN_CONSTEVAL
#if SCN_HAS_CONSTEVAL
#define SCN_CONSTEVAL consteval
#else
#define SCN_CONSTEVAL /*consteval*/ constexpr
#endif

// SCN_NODISCARD
#if SCN_HAS_NODISCARD
#define SCN_NODISCARD [[nodiscard]]
#else
#define SCN_NODISCARD /*nodiscard*/
#endif

// SCN_MAYBE_UNUSED
#if SCN_HAS_MAYBE_UNUSED
#define SCN_MAYBE_UNUSED [[maybe_unused]]
#else
#define SCN_MAYBE_UNUSED /*maybe_unused*/
#endif

// SCN_NO_UNIQUE_ADDRESS
#if SCN_HAS_NO_UNIQUE_ADDRESS_STD
#define SCN_NO_UNIQUE_ADDRESS [[no_unique_address]]
#elif SCN_HAS_NO_UNIQUE_ADDRESS_MSVC
#define SCN_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define SCN_NO_UNIQUE_ADDRESS /*no_unique_address*/
#endif

// SCN_FALLTHROUGH
#if SCN_HAS_FALLTHROUGH_CPPATTRIBUTE
#define SCN_FALLTHROUGH [[fallthrough]]
#elif SCN_HAS_FALLTHROUGH_CPPGNUATTRIBUTE
#define SCN_FALLTHROUGH [[gnu::fallthrough]]
#elif SCN_HAS_FALLTHROUGH_CPPCLANGATTRIBUTE
#define SCN_FALLTHROUGH [[clang::fallthrough]]
#elif SCN_HAS_FALLTHROUGH_GCCATTRIBUTE
#define SCN_FALLTHROUGH __attribute__((fallthrough))
#else
#define SCN_FALLTHROUGH    \
    do { /* fallthrough */ \
    } while (false)
#endif

// SCN_TRIVIAL_ABI
#if SCN_HAS_TRIVIAL_ABI && SCN_USE_TRIVIAL_ABI
#define SCN_TRIVIAL_ABI [[clang::trivial_abi]]
#else
#define SCN_TRIVIAL_ABI /*trivial_abi*/
#endif

// SCN_IMPLICIT
#if SCN_HAS_CONDITIONAL_EXPLICIT
#define SCN_IMPLICIT explicit(false)
#else
#define SCN_IMPLICIT /*implicit*/
#endif

#if SCN_GCC || SCN_CLANG
#define SCN_FORCE_INLINE __attribute__((always_inline))
#elif SCN_MSVC
#define SCN_FORCE_INLINE __forceinline
#else
#define SCN_FORCE_INLINE inline
#endif

// SCN_LIKELY & SCN_UNLIKELY
#if SCN_HAS_BUILTIN_EXPECT
#define SCN_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SCN_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define SCN_LIKELY(x)   (x)
#define SCN_UNLIKELY(x) (x)
#endif

// SCN_LIKELY_ATTR & SCN_UNLIKELY_ATTR
#if SCN_HAS_LIKELY_ATTR
#define SCN_LIKELY_ATTR   [[likely]]
#define SCN_UNLIKELY_ATTR [[unlikely]]
#else
#define SCN_LIKELY_ATTR   /* likely */
#define SCN_UNLIKELY_ATTR /* unlikely */
#endif

// SCN_ASSUME
#if SCN_HAS_ASSUME
#define SCN_ASSUME(x) __assume(x)
#elif SCN_HAS_BUILTIN_ASSUME
#define SCN_ASSUME(x) __builtin_assume(x)
#elif SCN_HAS_BUILTIN_UNREACHABLE
#define SCN_ASSUME(x) ((x) ? static_cast<void>(0) : __builtin_unreachable())
#else
#define SCN_ASSUME(x) static_cast<void>(!!(x))
#endif

// SCN_UNREACHABLE
#if SCN_HAS_BUILTIN_UNREACHABLE
#define SCN_UNREACHABLE __builtin_unreachable()
#else
#define SCN_UNREACHABLE SCN_ASSUME(0)
#endif

// SCN_ASSUME_ALIGNED
#if SCN_HAS_BUILTIN_ASSUME_ALIGNED
#define SCN_ASSUME_ALIGNED(x, n) __builtin_assume_aligned(x, n)
#elif SCN_HAS_ASSUME_ALIGNED
#define SCN_ASSUME_ALIGNED(x, n) __assume_aligned(x, n)
#else
#define SCN_ASSUME_ALIGNED(x, n)                                           \
    ([&](auto* p) noexcept -> decltype(p) {                                \
        if ((reinterpret_cast<std::uintptr_t>(p) & ((1 << n) - 1)) == 0) { \
            return p;                                                      \
        }                                                                  \
        else {                                                             \
            SCN_UNREACHABLE;                                               \
        }                                                                  \
    }(x))
#endif

#define SCN_UNUSED(x) static_cast<void>(sizeof(x))

// SCN_ASSERT
#ifdef NDEBUG
#define SCN_ASSERT(cond, msg)        \
    do {                             \
        SCN_CLANG_PUSH               \
        SCN_CLANG_IGNORE("-Wassume") \
        SCN_ASSUME(!!(cond));        \
        SCN_CLANG_POP                \
    } while (false)
#else
#define SCN_ASSERT(cond, msg) assert((cond) && msg)
#endif

#define SCN_EXPECT(cond) SCN_ASSERT(cond, "Precondition violation")
#define SCN_ENSURE(cond) SCN_ASSERT(cond, "Postcondition violation")

#define SCN_MOVE(x) \
    static_cast<    \
        typename ::scn::detail::remove_reference<decltype(x)>::type&&>(x)
#define SCN_FWD(x)          static_cast<decltype(x)&&>(x)
#define SCN_DECLVAL(...)    static_cast<__VA_ARGS__ (*)()>(nullptr)()

#define SCN_BEGIN_NAMESPACE inline namespace v4 {
#define SCN_END_NAMESPACE   }

/////////////////////////////////////////////////////////////////
// Forward declarations
/////////////////////////////////////////////////////////////////

/**
 * scnlib namespace, containing the library interface
 */
namespace scn {
SCN_BEGIN_NAMESPACE

/// Placeholder monostate type
struct monostate {};

template <typename Context>
class basic_scan_arg;
template <typename Context>
class basic_scan_args;

template <typename Range, typename CharT>
class basic_scan_context;

namespace detail {
struct buffer_range_tag {};

template <typename CharT>
using default_context = basic_scan_context<buffer_range_tag, CharT>;
}  // namespace detail

using scan_context = basic_scan_context<detail::buffer_range_tag, char>;
using wscan_context = basic_scan_context<detail::buffer_range_tag, wchar_t>;

using scan_args = basic_scan_args<scan_context>;
using wscan_args = basic_scan_args<wscan_context>;

class scan_error;

/**
 * A C++23-like `expected`.
 *
 * \ingroup result
 */
template <typename T, typename E>
class expected;

template <typename CharT>
struct basic_runtime_format_string;
template <typename CharT, typename Source, typename... Args>
class basic_scan_format_string;

namespace detail {
template <typename T>
struct type_identity {
    using type = T;
};
template <typename T>
using type_identity_t = typename type_identity<T>::type;
}  // namespace detail

template <typename Source, typename... Args>
using scan_format_string =
    basic_scan_format_string<char,
                             detail::type_identity_t<Source>,
                             detail::type_identity_t<Args>...>;
template <typename Source, typename... Args>
using wscan_format_string =
    basic_scan_format_string<wchar_t,
                             detail::type_identity_t<Source>,
                             detail::type_identity_t<Args>...>;

struct invalid_input_range;

#if !SCN_DISABLE_IOSTREAM

template <typename CharT>
struct basic_istream_scanner;

///
using istream_scanner = basic_istream_scanner<char>;
///
using wistream_scanner = basic_istream_scanner<wchar_t>;

#endif  // SCN_USE_IOSTREAMS

template <typename CharT>
class basic_scan_parse_context;

///
using scan_parse_context = basic_scan_parse_context<char>;
///
using wscan_parse_context = basic_scan_parse_context<wchar_t>;

namespace detail {
template <typename CharT>
class compile_parse_context;
}

template <typename Iterator, typename... Args>
class scan_result;

struct file_marker {
    constexpr file_marker() noexcept = default;
    template <typename... Args>
    constexpr file_marker(Args&&...) noexcept
    {
    }
};

namespace detail {
template <typename CharT>
class basic_scan_buffer;

using scan_buffer = basic_scan_buffer<char>;
using wscan_buffer = basic_scan_buffer<wchar_t>;
}  // namespace detail

/**
 * Scanner type, can be customized to enable scanning of user-defined types
 *
 * \ingroup ctx
 */
template <typename T, typename CharT = char, typename Enable = void>
struct scanner {
    /// Default fallback implementation, not constructible, always an error.
    scanner() = delete;

    /**
     * Parse the format string contained in `pctx`, and populate `*this` with
     * the parsed format specifier values, to be used later in `scan()`.
     *
     * Should be `constexpr` to allow for compile-time format string checking.
     *
     * A common pattern is to inherit a `scanner` implementation from another
     * `scanner`, while only overriding `scan()`, and keeping the same
     * `parse()`, or at least delegating to it.
     *
     * To report errors, an exception derived from `std::exception` can be
     * thrown, or `ParseContext::on_error` can be called.
     *
     * \return On success, an iterator pointing to the `}` character at the end
     * of the replacement field in the format string.
     * Will cause an error, if the returned iterator doesn't point to a `}`
     * character.
     */
    template <typename ParseContext>
    constexpr auto parse(ParseContext& pctx) ->
        typename ParseContext::iterator = delete;

    /**
     * Scan a value of type `T` from `ctx` into `value`,
     * using the format specs in `*this`, populated by `parse()`.
     *
     * `value` is guaranteed to only be default initialized.
     *
     * \return On success, an iterator pointing past the last character consumed
     * from `ctx`.
     */
    template <typename Context>
    auto scan(T& value, Context& ctx) const
        -> expected<typename Context::iterator, scan_error> = delete;
};

template <typename T>
struct discard;

namespace detail {
template <typename T, size_t N>
class basic_buffer;
}  // namespace detail

namespace detail {
template <typename T, typename = void>
struct pointer_traits;
}  // namespace detail

namespace detail {
struct dummy_type {};

template <typename T>
struct tag_type {
    using type = T;
};

template <typename>
struct dependent_false : std::false_type {};

template <typename T>
struct remove_reference {
    using type = T;
};
template <typename T>
struct remove_reference<T&> {
    using type = T;
};
template <typename T>
struct remove_reference<T&&> {
    using type = T;
};

template <std::size_t I>
struct priority_tag : priority_tag<I - 1> {};
template <>
struct priority_tag<0> {};

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T, typename Self>
inline constexpr bool is_not_self = !std::is_same_v<remove_cvref_t<T>, Self>;
}  // namespace detail

template <typename CharT>
class basic_regex_match;
template <typename CharT>
class basic_regex_matches;

using regex_match = basic_regex_match<char>;
using wregex_match = basic_regex_match<wchar_t>;

using regex_matches = basic_regex_matches<char>;
using wregex_matches = basic_regex_matches<wchar_t>;

SCN_END_NAMESPACE
}  // namespace scn
