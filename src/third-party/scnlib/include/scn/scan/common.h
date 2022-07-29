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

#ifndef SCN_SCAN_COMMON_H
#define SCN_SCAN_COMMON_H

#include "../detail/locale.h"
#include "../detail/result.h"
#include "../unicode/common.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        template <typename CharT>
        constexpr int to_format(int i)
        {
            return i;
        }
        template <typename T>
        constexpr auto to_format(T&& f) -> decltype(string_view{SCN_FWD(f)})
        {
            return {SCN_FWD(f)};
        }
        template <typename T>
        constexpr auto to_format(T&& f) -> decltype(wstring_view{SCN_FWD(f)})
        {
            return {SCN_FWD(f)};
        }
        template <typename CharT>
        basic_string_view<CharT> to_format(const std::basic_string<CharT>& str)
        {
            return {str.data(), str.size()};
        }

        template <typename CharT>
        struct until_pred {
            array<CharT, 4> until;
            size_t size;

            constexpr until_pred(CharT ch) : until({{ch}}), size(1) {}
            until_pred(code_point cp)
            {
                auto ret = encode_code_point(until.begin(), until.end(), cp);
                SCN_ENSURE(ret);
                size = ret.value() - until.begin();
            }

            SCN_CONSTEXPR14 bool operator()(span<const CharT> ch) const
            {
                if (ch.size() != size) {
                    return false;
                }
                for (size_t i = 0; i < ch.size(); ++i) {
                    if (ch[i] != until[i]) {
                        return false;
                    }
                }
                return true;
            }
            static constexpr bool is_localized()
            {
                return false;
            }
            constexpr bool is_multibyte() const
            {
                return size != 1;
            }
        };

        template <typename Error, typename Range>
        using generic_scan_result_for_range = decltype(detail::wrap_result(
            SCN_DECLVAL(Error),
            SCN_DECLVAL(detail::range_tag<Range>),
            SCN_DECLVAL(range_wrapper_for_t<Range>)));
        template <typename Range>
        using scan_result_for_range =
            generic_scan_result_for_range<wrapped_error, Range>;
    }  // namespace detail

    template <typename T>
    struct discard_type {
        discard_type() = default;
    };

    /**
     * Scans an instance of `T`, but doesn't store it anywhere.
     * Uses `scn::temp` internally, so the user doesn't have to bother.
     *
     * \code{.cpp}
     * int i{};
     * // 123 is discarded, 456 is read into `i`
     * auto result = scn::scan("123 456", "{} {}", scn::discard<T>(), i);
     * // result == true
     * // i == 456
     * \endcode
     */
    template <typename T>
    discard_type<T>& discard()
    {
        return temp(discard_type<T>{})();
    }

    template <typename T>
    struct scanner<discard_type<T>> : public scanner<T> {
        template <typename Context>
        error scan(discard_type<T>&, Context& ctx)
        {
            T tmp;
            return scanner<T>::scan(tmp, ctx);
        }
    };

    SCN_END_NAMESPACE
}  // namespace scn

#endif
