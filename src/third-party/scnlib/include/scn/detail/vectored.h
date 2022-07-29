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

#ifndef SCN_DETAIL_VECTORED_H
#define SCN_DETAIL_VECTORED_H

#include "../ranges/util.h"
#include "../util/math.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        namespace _get_buffer {
            struct fn {
            private:
                template <typename It>
                static It _get_end(It begin, It end, size_t max_size)
                {
                    return begin +
                           min(max_size, static_cast<std::size_t>(
                                             ranges::distance(begin, end)));
                }

                template <typename CharT>
                static SCN_CONSTEXPR14
                    span<typename std::add_const<CharT>::type>
                    impl(span<span<CharT>> s,
                         typename span<CharT>::iterator begin,
                         size_t max_size,
                         priority_tag<3>) noexcept
                {
                    auto buf_it = s.begin();
                    for (; buf_it != s.end(); ++buf_it) {
                        if (begin >= buf_it->begin() && begin < buf_it->end()) {
                            break;
                        }
                        if (begin == buf_it->end()) {
                            ++buf_it;
                            begin = buf_it->begin();
                            break;
                        }
                    }
                    if (buf_it == s.end()) {
                        return {};
                    }
                    return {begin, _get_end(begin, buf_it->end(), max_size)};
                }

                template <
                    typename Range,
                    typename std::enable_if<SCN_CHECK_CONCEPT(
                        ranges::contiguous_range<Range>)>::type* = nullptr>
                static SCN_CONSTEXPR14 span<typename std::add_const<
                    ranges::range_value_t<const Range>>::type>
                impl(const Range& s,
                     ranges::iterator_t<const Range> begin,
                     size_t max_size,
                     priority_tag<2>) noexcept
                {
                    auto b = ranges::begin(s);
                    auto e = ranges::end(s);
                    return {to_address(begin),
                            _get_end(to_address(begin),
                                     to_address_safe(e, b, e), max_size)};
                }

                template <typename Range, typename It>
                static auto impl(
                    const Range& r,
                    It begin,
                    size_t max_size,
                    priority_tag<1>) noexcept(noexcept(r.get_buffer(begin,
                                                                    max_size)))
                    -> decltype(r.get_buffer(begin, max_size))
                {
                    return r.get_buffer(begin, max_size);
                }

                template <typename Range, typename It>
                static auto impl(
                    const Range& r,
                    It begin,
                    size_t max_size,
                    priority_tag<0>) noexcept(noexcept(get_buffer(r,
                                                                  begin,
                                                                  max_size)))
                    -> decltype(get_buffer(r, begin, max_size))
                {
                    return get_buffer(r, begin, max_size);
                }

            public:
                template <typename Range, typename It>
                SCN_CONSTEXPR14 auto operator()(const Range& r,
                                                It begin,
                                                size_t max_size) const
                    noexcept(noexcept(
                        fn::impl(r, begin, max_size, priority_tag<3>{})))
                        -> decltype(fn::impl(r,
                                             begin,
                                             max_size,
                                             priority_tag<3>{}))
                {
                    return fn::impl(r, begin, max_size, priority_tag<3>{});
                }

                template <typename Range, typename It>
                SCN_CONSTEXPR14 auto operator()(const Range& r, It begin) const
                    noexcept(
                        noexcept(fn::impl(r,
                                          begin,
                                          std::numeric_limits<size_t>::max(),
                                          priority_tag<3>{})))
                        -> decltype(fn::impl(r,
                                             begin,
                                             std::numeric_limits<size_t>::max(),
                                             priority_tag<3>{}))
                {
                    return fn::impl(r, begin,
                                    std::numeric_limits<size_t>::max(),
                                    priority_tag<3>{});
                }
            };
        }  // namespace _get_buffer

        namespace {
            static constexpr auto& get_buffer =
                detail::static_const<_get_buffer::fn>::value;
        }  // namespace

        struct provides_buffer_access_concept {
            template <typename Range, typename Iterator>
            auto _test_requires(const Range& r, Iterator begin)
                -> decltype(scn::detail::valid_expr(
                    ::scn::detail::get_buffer(r, begin)));
        };
        template <typename Range, typename = void>
        struct provides_buffer_access_impl
            : std::integral_constant<
                  bool,
                  ::scn::custom_ranges::detail::_requires<
                      provides_buffer_access_concept,
                      Range,
                      ::scn::ranges::iterator_t<const Range>>::value> {
        };
    }  // namespace detail

    SCN_END_NAMESPACE
}  // namespace scn

#endif
