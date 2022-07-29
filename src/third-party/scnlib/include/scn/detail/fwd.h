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

#ifndef SCN_DETAIL_FWD_H
#define SCN_DETAIL_FWD_H

#include "config.h"

#include <cstddef>

namespace scn {
    SCN_BEGIN_NAMESPACE

    // args.h

    template <typename CharT>
    class basic_arg;
    template <typename CharT>
    class basic_args;
    template <typename CharT, typename... Args>
    class arg_store;

    template <typename T>
    struct temporary;

    // error.h

    class error;

    // locale.h

    template <typename CharT>
    class basic_locale_ref;

    // context.h

    template <typename WrappedRange>
    class basic_context;

    // parse_context.h

    template <typename CharT>
    class basic_parse_context;
    template <typename CharT>
    class basic_empty_parse_context;

    namespace detail {
        template <typename T>
        struct parse_context_template_for_format;
    }

    // reader/common.h

    template <typename T, typename Enable = void>
    struct scanner;

    // defined here to avoid including <scn.h> if the user wants to create a
    // scanner for their own type
    /**
     * Base class for all scanners.
     * User-defined scanner must derive from this type.
     */
    struct parser_base {
        /**
         * Returns `true` if `skip_range_whitespace()` is to be called before
         * scanning this value.
         *
         * Defaults to `true`. Is `false` for chars, code points and strings
         * when using set scanning.
         */
        static constexpr bool skip_preceding_whitespace()
        {
            return true;
        }
        /**
         * Returns `true` if this scanner supports parsing align and fill
         * specifiers from the format string, and then scanning them.
         *
         * Defaults to `false`, `true` for all scnlib-defined scanners.
         */
        static constexpr bool support_align_and_fill()
        {
            return false;
        }

        static SCN_CONSTEXPR14 void make_localized() {}
    };

    struct empty_parser;
    struct common_parser;
    struct common_parser_default;

    namespace detail {
        template <typename T>
        struct simple_integer_scanner;
    }

    // visitor.h

    template <typename Context, typename ParseCtx>
    class basic_visitor;

    // file.h

    template <typename CharT>
    class basic_mapped_file;
    template <typename CharT>
    class basic_file;
    template <typename CharT>
    class basic_owning_file;

    // scan.h

    template <typename T>
    struct span_list_wrapper;
    template <typename T>
    struct discard_type;

    // util/array.h

    namespace detail {
        template <typename T, std::size_t N>
        struct array;
    }

    // util/expected.h

    template <typename T, typename Error = ::scn::error, typename Enable = void>
    class expected;

    // util/memory.h

    namespace detail {
        template <typename T>
        struct pointer_traits;

        template <typename T>
        class erased_storage;

    }  // namespace detail

    // util/optional.h

    template <typename T>
    class optional;

    // util/small_vector.h

    namespace detail {
        template <typename T, size_t StackN>
        class small_vector;
    }

    // util/span.h

    template <typename T>
    class span;

    // util/string_view.h

    template <typename CharT>
    class basic_string_view;

    // util/unique_ptr.h

    namespace detail {
        template <typename T>
        class unique_ptr;
    }

    // for SCN_MOVE
    namespace detail {
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
    }  // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_DETAIL_FWD_H
