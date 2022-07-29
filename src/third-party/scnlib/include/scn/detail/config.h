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

#ifndef SCN_DETAIL_CONFIG_H
#define SCN_DETAIL_CONFIG_H

#include <cassert>

#define SCN_STD_11 201103L
#define SCN_STD_14 201402L
#define SCN_STD_17 201703L

#define SCN_COMPILER(major, minor, patch) \
    ((major)*10000000 /* 10,000,000 */ + (minor)*10000 /* 10,000 */ + (patch))
#define SCN_VERSION SCN_COMPILER(1, 1, 2)

#ifdef __INTEL_COMPILER
// Intel
#define SCN_INTEL                                                      \
    SCN_COMPILER(__INTEL_COMPILER / 100, (__INTEL_COMPILER / 10) % 10, \
                 __INTEL_COMPILER % 10)
#elif defined(_MSC_VER) && defined(_MSC_FULL_VER)
// MSVC
#if _MSC_VER == _MSC_FULL_VER / 10000
#define SCN_MSVC \
    SCN_COMPILER(_MSC_VER / 100, _MSC_VER % 100, _MSC_FULL_VER % 10000)
#else
#define SCN_MSVC                                                \
    SCN_COMPILER(_MSC_VER / 100, (_MSC_FULL_VER / 100000) % 10, \
                 _MSC_FULL_VER % 100000)
#endif  // _MSC_VER == _MSC_FULL_VER / 10000
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

#define SCN_STRINGIFY_APPLY(x) #x
#define SCN_STRINGIFY(x)       SCN_STRINGIFY_APPLY(x)

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
#if (defined(WIN32) || defined(_WIN32) || defined(__WIN32)) && \
    !defined(__CYGWIN__)
#define SCN_WINDOWS 1
#else
#define SCN_WINDOWS 0
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

// Warning control
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

#else
#define SCN_CLANG_PUSH
#define SCN_CLANG_POP
#define SCN_CLANG_IGNORE(x)
#define SCN_CLANG_PUSH_IGNORE_UNDEFINED_TEMPLATE
#define SCN_CLANG_POP_IGNORE_UNDEFINED_TEMPLATE
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

#ifndef SCN_PREDEFINE_VSCAN_OVERLOADS
#define SCN_PREDEFINE_VSCAN_OVERLOADS 0
#endif

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

#if SCN_HAS_EXCEPTIONS
#define SCN_TRY      try
#define SCN_CATCH(x) catch (x)
#define SCN_THROW(x) throw x
#define SCN_RETHROW  throw
#else
#define SCN_TRY      if (true)
#define SCN_CATCH(x) if (false)
#define SCN_THROW(x) ::std::abort()
#define SCN_RETHROW  ::std::abort()
#endif

#ifdef __has_include
#define SCN_HAS_INCLUDE(x) __has_include(x)
#else
#define SCN_HAS_INCLUDE(x) 0
#endif

#ifdef __has_cpp_attribute
#define SCN_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define SCN_HAS_CPP_ATTRIBUTE(x) 0
#endif

#ifdef __has_feature
#define SCN_HAS_FEATURE(x) __has_feature(x)
#else
#define SCN_HAS_FEATURE(x) 0
#endif

#ifdef __has_builtin
#define SCN_HAS_BUILTIN(x) __has_builtin(x)
#else
#define SCN_HAS_BUILTIN(x) 0
#endif

#if SCN_HAS_INCLUDE(<version>)
#include <version>
#endif

#if defined(_SCN_DOXYGEN) && _SCN_DOXYGEN
#define SCN_DOXYGEN 1
#else
#define SCN_DOXYGEN 0
#endif

// Detect constexpr
#if defined(__cpp_constexpr)
#if __cpp_constexpr >= 201304
#define SCN_HAS_RELAXED_CONSTEXPR 1
#else
#define SCN_HAS_RELAXED_CONSTEXPR 0
#endif
#endif

#ifndef SCN_HAS_RELAXED_CONSTEXPR
#if SCN_HAS_FEATURE(cxx_relaxed_constexpr) || \
    SCN_MSVC >= SCN_COMPILER(19, 10, 0) ||    \
    ((SCN_GCC >= SCN_COMPILER(6, 0, 0) ||     \
      SCN_INTEL >= SCN_COMPILER(17, 0, 0)) && \
     SCN_STD >= SCN_STD_14)
#define SCN_HAS_RELAXED_CONSTEXPR 1
#else
#define SCN_HAS_RELAXED_CONSTEXPR 0
#endif
#endif

#if SCN_HAS_RELAXED_CONSTEXPR || SCN_DOXYGEN
#define SCN_CONSTEXPR14 constexpr
#else
#define SCN_CONSTEXPR14 inline
#endif

// Detect string_view
#if defined(__cpp_lib_string_view) && __cpp_lib_string_view >= 201603 && \
    SCN_STD >= SCN_STD_17
#define SCN_HAS_STRING_VIEW 1
#else
#define SCN_HAS_STRING_VIEW 0
#endif

// Detect [[nodiscard]]
#if (SCN_HAS_CPP_ATTRIBUTE(nodiscard) && __cplusplus >= SCN_STD_17) ||      \
    (SCN_MSVC >= SCN_COMPILER(19, 11, 0) && SCN_MSVC_LANG >= SCN_STD_17) || \
    ((SCN_GCC >= SCN_COMPILER(7, 0, 0) ||                                   \
      SCN_INTEL >= SCN_COMPILER(18, 0, 0)) &&                               \
     __cplusplus >= SCN_STD_17) && !SCN_DOXYGEN
#define SCN_NODISCARD [[nodiscard]]
#else
#define SCN_NODISCARD /*nodiscard*/
#endif

// Detect [[clang::trivial_abi]]
#if SCN_HAS_CPP_ATTRIBUTE(clang::trivial_abi)
#define SCN_TRIVIAL_ABI [[clang::trivial_abi]]
#else
#define SCN_TRIVIAL_ABI /*trivial_abi*/
#endif

#if defined(SCN_HEADER_ONLY) && SCN_HEADER_ONLY
#define SCN_FUNC inline
#else
#define SCN_FUNC
#endif

// Detect <charconv>

#if defined(_GLIBCXX_RELEASE) && __cplusplus >= SCN_STD_17
#define SCN_HAS_INTEGER_CHARCONV (_GLIBCXX_RELEASE >= 9)
#define SCN_HAS_FLOAT_CHARCONV   (_GLIBCXX_RELEASE >= 11)
#elif SCN_MSVC >= SCN_COMPILER(19, 14, 0)
#define SCN_HAS_INTEGER_CHARCONV 1
#define SCN_HAS_FLOAT_CHARCONV   (SCN_MSVC >= SCN_COMPILER(19, 21, 0))
#elif defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201606
#define SCN_HAS_INTEGER_CHARCONV 1
#define SCN_HAS_FLOAT_CHARCONV   1
#endif  // _GLIBCXX_RELEASE

#ifndef SCN_HAS_INTEGER_CHARCONV
#define SCN_HAS_INTEGER_CHARCONV 0
#define SCN_HAS_FLOAT_CHARCONV   0
#endif

// Detect std::launder
#if defined(__cpp_lib_launder) && __cpp_lib_launder >= 201606
#define SCN_HAS_LAUNDER 1
#else
#define SCN_HAS_LAUNDER 0
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

// Detect __builtin_unreachable
#if SCN_HAS_BUILTIN(__builtin_unreachable) || SCN_GCC_COMPAT
#define SCN_HAS_BUILTIN_UNREACHABLE 1
#else
#define SCN_HAS_BUILTIN_UNREACHABLE 0
#endif

#if SCN_HAS_ASSUME
#define SCN_ASSUME(x) __assume(x)
#elif SCN_HAS_BUILTIN_ASSUME
#define SCN_ASSUME(x) __builtin_assume(x)
#elif SCN_HAS_BUILTIN_UNREACHABLE
#define SCN_ASSUME(x) ((x) ? static_cast<void>(0) : __builtin_unreachable())
#else
#define SCN_ASSUME(x) static_cast<void>((x) ? 0 : 0)
#endif

#if SCN_HAS_BUILTIN_UNREACHABLE
#define SCN_UNREACHABLE __builtin_unreachable()
#else
#define SCN_UNREACHABLE SCN_ASSUME(0)
#endif

// Detect __builtin_expect
#if SCN_HAS_BUILTIN(__builtin_expect) || SCN_GCC_COMPAT
#define SCN_HAS_BUILTIN_EXPECT 1
#else
#define SCN_HAS_BUILTIN_EXPECT 0
#endif

#if SCN_HAS_BUILTIN_EXPECT
#define SCN_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SCN_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define SCN_LIKELY(x)   (x)
#define SCN_UNLIKELY(x) (x)
#endif

#ifndef SCN_DEPRECATED

#if (SCN_HAS_CPP_ATTRIBUTE(deprecated) && SCN_STD >= 201402L) || \
    SCN_MSVC >= SCN_COMPILER(19, 0, 0) || SCN_DOXYGEN
#define SCN_DEPRECATED [[deprecated]]
#else

#if SCN_GCC_COMPAT
#define SCN_DEPRECATED __attribute__((deprecated))
#elif SCN_MSVC
#define SCN_DEPRECATED __declspec(deprecated)
#else
#define SCN_DEPRECATED /* deprecated */
#endif

#endif

#endif  // !defined(SCN_DEPRECATED)

// Detect concepts
#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
#define SCN_HAS_CONCEPTS 1
#else
#define SCN_HAS_CONCEPTS 0
#endif

// Detect ranges
#if defined(__cpp_lib_ranges) && __cpp_lib_ranges >= 201911L
#define SCN_HAS_RANGES 1
#else
#define SCN_HAS_RANGES 0
#endif

// Detect char8_t
#if defined(__cpp_char8_t) && __cpp_char8_t >= 201811L
#define SCN_HAS_CHAR8 1
#else
#define SCN_HAS_CHAR8 0
#endif

#define SCN_UNUSED(x) static_cast<void>(sizeof(x))

#if SCN_HAS_RELAXED_CONSTEXPR
#define SCN_ASSERT(cond, msg)                \
    do {                                     \
        static_cast<void>(SCN_LIKELY(cond)); \
        assert((cond) && msg);               \
    } while (false)
#define SCN_EXPECT(cond) SCN_ASSERT(cond, "Precondition violation")
#define SCN_ENSURE(cond) SCN_ASSERT(cond, "Postcondition violation")
#else
#define SCN_ASSERT(cond, msg) SCN_UNUSED(cond)
#define SCN_EXPECT(cond)      SCN_UNUSED(cond)
#define SCN_ENSURE(cond)      SCN_UNUSED(cond)
#endif

#define SCN_MOVE(x) \
    static_cast<    \
        typename ::scn::detail::remove_reference \
        <decltype(x)>::type&&>(x)
#define SCN_FWD(x)          static_cast<decltype(x)&&>(x)
#define SCN_DECLVAL(T)      static_cast<T (*)()>(nullptr)()

#define SCN_BEGIN_NAMESPACE inline namespace v1 {
#define SCN_END_NAMESPACE   }

#if defined(SCN_HEADER_ONLY)
#define SCN_INCLUDE_SOURCE_DEFINITIONS !SCN_HEADER_ONLY
#else
#define SCN_INCLUDE_SOURCE_DEFINITIONS 1
#endif

#endif  // SCN_DETAIL_CONFIG_H
