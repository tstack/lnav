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

#ifndef SCN_DETAIL_RANGE_H
#define SCN_DETAIL_RANGE_H

#include "../ranges/ranges.h"
#include "../util/algorithm.h"
#include "../util/memory.h"
#include "error.h"
#include "vectored.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace detail {
        namespace _reset_begin_iterator {
            struct fn {
            private:
                template <typename Iterator>
                static auto impl(Iterator& it, priority_tag<1>) noexcept(
                    noexcept(it.reset_begin_iterator()))
                    -> decltype(it.reset_begin_iterator())
                {
                    return it.reset_begin_iterator();
                }

                template <typename Iterator>
                static void impl(Iterator&, size_t, priority_tag<0>) noexcept
                {
                }

            public:
                template <typename Iterator>
                auto operator()(Iterator& it) const
                    noexcept(noexcept(fn::impl(it, priority_tag<1>{})))
                        -> decltype(fn::impl(it, priority_tag<1>{}))
                {
                    return fn::impl(it, priority_tag<1>{});
                }
            };
        }  // namespace _reset_begin_iterator
        namespace {
            static constexpr auto& reset_begin_iterator =
                static_const<detail::_reset_begin_iterator::fn>::value;
        }

        template <typename Iterator, typename = void>
        struct extract_char_type;
        template <typename Iterator>
        struct extract_char_type<
            Iterator,
            typename std::enable_if<std::is_integral<
                polyfill_2a::iter_value_t<Iterator>>::value>::type> {
            using type = polyfill_2a::iter_value_t<Iterator>;
        };
        template <typename Iterator>
        struct extract_char_type<
            Iterator,
            void_t<
                typename std::enable_if<!std::is_integral<
                    polyfill_2a::iter_value_t<Iterator>>::value>::type,
                typename polyfill_2a::iter_value_t<Iterator>::success_type>> {
            using type =
                typename polyfill_2a::iter_value_t<Iterator>::success_type;
        };

        template <typename Range, typename = void>
        struct is_direct_impl
            : std::is_integral<ranges::range_value_t<const Range>> {
        };

        template <typename Range>
        struct reconstruct_tag {
        };

        template <
            typename Range,
            typename Iterator,
            typename Sentinel,
            typename = typename std::enable_if<
                std::is_constructible<Range, Iterator, Sentinel>::value>::type>
        Range reconstruct(reconstruct_tag<Range>, Iterator begin, Sentinel end)
        {
            return {begin, end};
        }
#if SCN_HAS_STRING_VIEW
        // std::string_view is not reconstructible pre-C++20
        template <typename CharT,
                  typename Traits,
                  typename Iterator,
                  typename Sentinel>
        std::basic_string_view<CharT, Traits> reconstruct(
            reconstruct_tag<std::basic_string_view<CharT, Traits>>,
            Iterator begin,
            Sentinel end)
        {
            // On MSVC, string_view can't even be constructed from its
            // iterators!
            return {::scn::detail::to_address(begin),
                    static_cast<size_t>(ranges::distance(begin, end))};
        }
#endif  // SCN_HAS_STRING_VIEW

        template <typename T, bool>
        struct range_wrapper_storage;
        template <typename T>
        struct range_wrapper_storage<T, true> {
            using type = remove_cvref_t<T>;
            using range_type = const type&;

            const type* value{nullptr};

            range_wrapper_storage() = default;
            range_wrapper_storage(const type& v, dummy_type)
                : value(std::addressof(v))
            {
            }

            const type& get() const& noexcept
            {
                return *value;
            }
            type&& get() && noexcept
            {
                return *value;
            }
        };
        template <typename T>
        struct range_wrapper_storage<T, false> {
            using range_type = T;

            T value{};

            range_wrapper_storage() = default;
            template <typename U>
            range_wrapper_storage(U&& v, dummy_type) : value(SCN_FWD(v))
            {
            }

            const T& get() const& noexcept
            {
                return value;
            }
            T&& get() && noexcept
            {
                return value;
            }
        };

        template <typename T>
        using _range_wrapper_marker = typename T::range_wrapper_marker;

        template <typename T>
        struct _has_range_wrapper_marker
            : custom_ranges::detail::exists<_range_wrapper_marker, T> {
        };

        /**
         * Wraps a source range for more consistent behavior
         */
        template <typename Range>
        class range_wrapper {
        public:
            using range_type = Range;
            using range_nocvref_type = remove_cvref_t<Range>;
            using iterator = ranges::iterator_t<const range_nocvref_type>;
            using sentinel = ranges::sentinel_t<const range_nocvref_type>;
            using char_type = typename extract_char_type<iterator>::type;
            using difference_type =
                ranges::range_difference_t<const range_nocvref_type>;
            using storage_type =
                range_wrapper_storage<Range, std::is_reference<Range>::value>;
            using storage_range_type = typename storage_type::range_type;

            using range_wrapper_marker = void;

            template <
                typename R,
                typename = typename std::enable_if<
                    !_has_range_wrapper_marker<remove_cvref_t<R>>::value>::type>
            range_wrapper(R&& r)
                : m_range(SCN_FWD(r), dummy_type{}),
                  m_begin(ranges::cbegin(m_range.get()))
            {
            }

            range_wrapper(const range_wrapper& o) : m_range(o.m_range)
            {
                const auto n =
                    ranges::distance(o.begin_underlying(), o.m_begin);
                m_begin = ranges::cbegin(m_range.get());
                ranges::advance(m_begin, n);
                m_read = o.m_read;
            }
            range_wrapper& operator=(const range_wrapper& o)
            {
                const auto n =
                    ranges::distance(o.begin_underlying(), o.m_begin);
                m_range = o.m_range;
                m_begin = ranges::cbegin(m_range.get());
                ranges::advance(m_begin, n);
                m_read = o.m_read;
                return *this;
            }

            range_wrapper(range_wrapper&& o) noexcept
            {
                const auto n =
                    ranges::distance(o.begin_underlying(), o.m_begin);
                m_range = SCN_MOVE(o.m_range);
                m_begin = ranges::cbegin(m_range.get());
                ranges::advance(m_begin, n);
                m_read = exchange(o.m_read, 0);
            }
            range_wrapper& operator=(range_wrapper&& o) noexcept
            {
                reset_to_rollback_point();

                const auto n =
                    ranges::distance(o.begin_underlying(), o.m_begin);
                m_range = SCN_MOVE(o.m_range);
                m_begin = ranges::cbegin(m_range.get());
                ranges::advance(m_begin, n);
                m_read = exchange(o.m_read, 0);
                return *this;
            }

            ~range_wrapper() = default;

            iterator begin() const noexcept
            {
                return m_begin;
            }
            SCN_GCC_PUSH
            SCN_GCC_IGNORE("-Wnoexcept")
            sentinel end() const noexcept(
                noexcept(ranges::end(SCN_DECLVAL(const storage_type&).get())))
            {
                return ranges::end(m_range.get());
            }
            SCN_GCC_POP

            struct dummy {
            };

            /**
             * Returns `true` if `begin() == end()`.
             */
            bool empty() const
            {
                return begin() == end();
            }

            /**
             * Advance the begin iterator by `n` characters.
             */
            iterator advance(difference_type n = 1) noexcept
            {
                SCN_EXPECT(_advance_check(
                    n, std::integral_constant<bool, is_contiguous>{}));
                m_read += n;
                ranges::advance(m_begin, n);
                return m_begin;
            }

            /// @{
            /**
             * Advance the begin iterator, until it's equal to `it`.
             * Assumes that `it` is reachable by repeatedly incrementing begin,
             * will hang otherwise.
             */
            template <typename R = range_nocvref_type,
                      typename std::enable_if<SCN_CHECK_CONCEPT(
                          ranges::sized_range<R>)>::type* = nullptr>
            void advance_to(iterator it) noexcept
            {
                const auto diff = ranges::distance(m_begin, it);
                m_read += diff;
                m_begin = it;
            }
            template <typename R = range_nocvref_type,
                      typename std::enable_if<SCN_CHECK_CONCEPT(
                          !ranges::sized_range<R>)>::type* = nullptr>
            void advance_to(iterator it) noexcept
            {
                while (m_begin != it) {
                    ++m_read;
                    ++m_begin;
                }
            }
            /// @}

            /**
             * Returns the begin iterator of the underlying source range, is not
             * necessarily equal to `begin()`.
             */
            iterator begin_underlying() const noexcept(noexcept(
                ranges::cbegin(SCN_DECLVAL(const range_nocvref_type&))))
            {
                return ranges::cbegin(m_range.get());
            }

            /**
             * Returns the underlying source range.
             * Note that `range_underlying().begin()` may not be equal to
             * `begin()`.
             */
            const range_type& range_underlying() const noexcept
            {
                return m_range.get();
            }

            /**
             * Returns a pointer to the beginning of the range.
             * `*this` must be contiguous.
             */
            template <typename R = range_nocvref_type,
                      typename std::enable_if<SCN_CHECK_CONCEPT(
                          ranges::contiguous_range<R>)>::type* = nullptr>
            auto data() const
                noexcept(noexcept(*SCN_DECLVAL(ranges::iterator_t<const R>)))
                    -> decltype(std::addressof(
                        *SCN_DECLVAL(ranges::iterator_t<const R>)))
            {
                return std::addressof(*m_begin);
            }
            SCN_GCC_PUSH
            SCN_GCC_IGNORE("-Wnoexcept")
            /**
             * Returns `end() - begin()`.
             * `*this` must be sized.
             */
            template <typename R = range_nocvref_type,
                      typename std::enable_if<SCN_CHECK_CONCEPT(
                          ranges::sized_range<R>)>::type* = nullptr>
            auto size() const noexcept(noexcept(
                ranges::distance(SCN_DECLVAL(ranges::iterator_t<const R>),
                                 SCN_DECLVAL(ranges::sentinel_t<const R>))))
                -> decltype(ranges::distance(
                    SCN_DECLVAL(ranges::iterator_t<const R>),
                    SCN_DECLVAL(ranges::sentinel_t<const R>)))
            {
                return ranges::distance(m_begin, end());
            }
            SCN_GCC_POP
            struct dummy2 {
            };

            template <typename R = range_nocvref_type,
                      typename std::enable_if<provides_buffer_access_impl<
                          R>::value>::type* = nullptr>
            span<const char_type> get_buffer_and_advance(
                size_t max_size = std::numeric_limits<size_t>::max())
            {
                auto buf = get_buffer(m_range.get(), begin(), max_size);
                if (buf.size() == 0) {
                    return buf;
                }
                advance(buf.ssize());
                return buf;
            }

            /**
             * Reset `begin()` to the rollback point, as if by repeatedly
             * calling `operator--()` on the begin iterator.
             *
             * Returns `error::unrecoverable_source_error` on failure.
             *
             * \see set_rollback_point()
             */
            error reset_to_rollback_point()
            {
                for (; m_read != 0; --m_read) {
                    --m_begin;
                    if (m_begin == end()) {
                        return {error::unrecoverable_source_error,
                                "Putback failed"};
                    }
                }
                return {};
            }
            /**
             * Sets the rollback point equal to the current `begin()` iterator.
             *
             * \see reset_to_rollback_point()
             */
            void set_rollback_point()
            {
                m_read = 0;
            }

            void reset_begin_iterator()
            {
                detail::reset_begin_iterator(m_begin);
            }

            /**
             * Construct a new source range from `begin()` and `end()`, and wrap
             * it in a new `range_wrapper`.
             */
            template <typename R>
            auto reconstruct_and_rewrap() && -> range_wrapper<R>
            {
                auto reconstructed =
                    reconstruct(reconstruct_tag<R>{}, begin(), end());
                return {SCN_MOVE(reconstructed)};
            }

            /**
             * `true` if `value_type` is a character type (`char` or `wchar_t`)
             * `false` if it's an `expected` containing a character
             */
            static constexpr bool is_direct =
                is_direct_impl<range_nocvref_type>::value;
            // can call .data() and memcpy
            /**
             * `true` if `this->data()` can be called, and `memcpy` can be
             * performed on it.
             */
            static constexpr bool is_contiguous =
                SCN_CHECK_CONCEPT(ranges::contiguous_range<range_nocvref_type>);
            /**
             * `true` if the range provides a way to access a contiguous buffer
             * on it (`detail::get_buffer()`), which may not provide the entire
             * source data, e.g. a `span` of `span`s (vectored I/O).
             */
            static constexpr bool provides_buffer_access =
                provides_buffer_access_impl<range_nocvref_type>::value;

        private:
            template <typename R = Range>
            bool _advance_check(std::ptrdiff_t n, std::true_type)
            {
                SCN_CLANG_PUSH
                SCN_CLANG_IGNORE("-Wzero-as-null-pointer-constant")
                return m_begin + n <= end();
                SCN_CLANG_POP
            }
            template <typename R = Range>
            bool _advance_check(std::ptrdiff_t, std::false_type)
            {
                return true;
            }

            storage_type m_range;
            iterator m_begin;
            mutable difference_type m_read{0};
        };

        namespace _wrap {
            struct fn {
            private:
                template <typename Range>
                static range_wrapper<Range> impl(const range_wrapper<Range>& r,
                                                 priority_tag<4>) noexcept
                {
                    return r;
                }
                template <typename Range>
                static range_wrapper<Range> impl(range_wrapper<Range>&& r,
                                                 priority_tag<4>) noexcept
                {
                    return SCN_MOVE(r);
                }

                template <typename Range>
                static auto impl(Range&& r, priority_tag<3>) noexcept(
                    noexcept(SCN_FWD(r).wrap())) -> decltype(SCN_FWD(r).wrap())
                {
                    return SCN_FWD(r).wrap();
                }

                template <typename CharT, std::size_t N>
                static auto impl(CharT (&str)[N], priority_tag<2>) noexcept
                    -> range_wrapper<
                        basic_string_view<typename std::remove_cv<CharT>::type>>
                {
                    return {
                        basic_string_view<typename std::remove_cv<CharT>::type>(
                            str, str + N - 1)};
                }

                template <typename CharT, typename Allocator>
                static auto impl(
                    const std::basic_string<CharT,
                                            std::char_traits<CharT>,
                                            Allocator>& str,
                    priority_tag<2>) noexcept
                    -> range_wrapper<basic_string_view<CharT>>
                {
                    return {basic_string_view<CharT>{str.data(), str.size()}};
                }
                template <typename CharT, typename Allocator>
                static auto impl(
                    std::basic_string<CharT,
                                      std::char_traits<CharT>,
                                      Allocator>&& str,
                    priority_tag<2>) noexcept(std::
                                                  is_nothrow_move_constructible<
                                                      decltype(str)>::value)
                    -> range_wrapper<std::basic_string<CharT,
                                                       std::char_traits<CharT>,
                                                       Allocator>>
                {
                    return {SCN_MOVE(str)};
                }

#if SCN_HAS_STRING_VIEW
                template <typename CharT>
                static auto impl(const std::basic_string_view<CharT>& str,
                                 priority_tag<1>) noexcept
                    -> range_wrapper<basic_string_view<CharT>>
                {
                    return {basic_string_view<CharT>{str.data(), str.size()}};
                }
#endif
                template <typename T,
                          typename CharT = typename std::remove_const<T>::type>
                static auto impl(span<T> s, priority_tag<2>) noexcept
                    -> range_wrapper<basic_string_view<CharT>>
                {
                    return {basic_string_view<CharT>{s.data(), s.size()}};
                }

                template <typename Range,
                          typename = typename std::enable_if<
                              SCN_CHECK_CONCEPT(ranges::view<Range>)>::type>
                static auto impl(Range r, priority_tag<1>) noexcept
                    -> range_wrapper<Range>
                {
                    return {r};
                }

                template <typename Range>
                static auto impl(const Range& r, priority_tag<0>) noexcept
                    -> range_wrapper<Range&>
                {
                    static_assert(SCN_CHECK_CONCEPT(ranges::range<Range>),
                                  "Input needs to be a Range");
                    return {r};
                }
                template <typename Range,
                          typename = typename std::enable_if<
                              !std::is_reference<Range>::value>::type>
                static auto impl(Range&& r, priority_tag<0>) noexcept
                    -> range_wrapper<Range>
                {
                    static_assert(SCN_CHECK_CONCEPT(ranges::range<Range>),
                                  "Input needs to be a Range");
                    return {SCN_MOVE(r)};
                }

            public:
                template <typename Range>
                auto operator()(Range&& r) const
                    noexcept(noexcept(fn::impl(SCN_FWD(r), priority_tag<4>{})))
                        -> decltype(fn::impl(SCN_FWD(r), priority_tag<4>{}))
                {
                    return fn::impl(SCN_FWD(r), priority_tag<4>{});
                }
            };
        }  // namespace _wrap
    }      // namespace detail

    namespace {
        /**
         * Create a `range_wrapper` for any supported source range.
         */
        static constexpr auto& wrap =
            detail::static_const<detail::_wrap::fn>::value;
    }  // namespace

    template <typename Range>
    struct range_wrapper_for {
        using type = decltype(wrap(SCN_DECLVAL(Range)));
    };
    template <typename Range>
    using range_wrapper_for_t = typename range_wrapper_for<Range>::type;

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_DETAIL_RANGE_H
