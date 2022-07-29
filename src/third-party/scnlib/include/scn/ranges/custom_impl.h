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
// The contents of this file are adapted from NanoRange.
//     https://github.com/tcbrindle/NanoRange
//     Copyright (c) 2018 Tristan Brindle
//     Distributed under the Boost Software License, Version 1.0

#ifndef SCN_RANGES_CUSTOM_IMPL_H
#define SCN_RANGES_CUSTOM_IMPL_H

#include "util.h"

#include "../util/string_view.h"

#include <iterator>
#include <utility>

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace custom_ranges {
        // iterator_category is span.h

        template <typename T>
        struct iterator_category;

        namespace detail {
            template <typename T, typename = void>
            struct iterator_category {
            };
            template <typename T>
            struct iterator_category<T*>
                : std::enable_if<std::is_object<T>::value,
                                 contiguous_iterator_tag> {
            };
            template <typename T>
            struct iterator_category<const T> : iterator_category<T> {
            };
            template <typename T>
            struct iterator_category<
                T,
                detail::void_t<typename T::iterator_category>> {
                using type = typename T::iterator_category;
            };
        }  // namespace detail

        template <typename T>
        struct iterator_category : detail::iterator_category<T> {
        };
        template <typename T>
        using iterator_category_t = typename iterator_category<T>::type;

        template <typename T>
        using iter_reference_t = decltype(*SCN_DECLVAL(T&));

        // iter_difference_t
        template <typename>
        struct incrementable_traits;

        namespace detail {
            struct empty {
            };

            template <typename T>
            struct with_difference_type {
                using difference_type = T;
            };
            template <typename, typename = void>
            struct incrementable_traits_helper {
            };

            template <>
            struct incrementable_traits_helper<void*> {
            };
            template <typename T>
            struct incrementable_traits_helper<T*>
                : std::conditional<std::is_object<T>::value,
                                   with_difference_type<std::ptrdiff_t>,
                                   empty>::type {
            };
            template <typename I>
            struct incrementable_traits_helper<const I>
                : incrementable_traits<typename std::decay<I>::type> {
            };

            template <typename, typename = void>
            struct has_member_difference_type : std::false_type {
            };
            template <typename T>
            struct has_member_difference_type<
                T,
                detail::void_t<typename T::difference_type>> : std::true_type {
            };

            template <typename T>
            struct incrementable_traits_helper<
                T,
                typename std::enable_if<
                    has_member_difference_type<T>::value>::type> {
                using difference_type = typename T::difference_type;
            };
            template <typename T>
            struct incrementable_traits_helper<
                T,
                typename std::enable_if<
                    !std::is_pointer<T>::value &&
                    !has_member_difference_type<T>::value &&
                    std::is_integral<decltype(SCN_DECLVAL(const T&) -
                                              SCN_DECLVAL(const T&))>::value>::
                    type>
                : with_difference_type<typename std::make_signed<
                      decltype(SCN_DECLVAL(T) - SCN_DECLVAL(T))>::type> {
            };
        }  // namespace detail

        template <typename T>
        struct incrementable_traits : detail::incrementable_traits_helper<T> {
        };

        template <typename T>
        using iter_difference_t =
            typename incrementable_traits<T>::difference_type;

        // iter_value_t
        template <typename>
        struct readable_traits;

        namespace detail {
            template <typename T>
            struct with_value_type {
                using value_type = T;
            };
            template <typename, typename = void>
            struct readable_traits_helper {
            };

            template <typename T>
            struct readable_traits_helper<T*>
                : std::conditional<
                      std::is_object<T>::value,
                      with_value_type<typename std::remove_cv<T>::type>,
                      empty>::type {
            };

            template <typename I>
            struct readable_traits_helper<
                I,
                typename std::enable_if<std::is_array<I>::value>::type>
                : readable_traits<typename std::decay<I>::type> {
            };

            template <typename I>
            struct readable_traits_helper<
                const I,
                typename std::enable_if<!std::is_array<I>::value>::type>
                : readable_traits<typename std::decay<I>::type> {
            };

            template <typename T, typename V = typename T::value_type>
            struct member_value_type
                : std::conditional<std::is_object<V>::value,
                                   with_value_type<V>,
                                   empty>::type {
            };

            template <typename T, typename E = typename T::element_type>
            struct _member_element_type
                : std::conditional<
                      std::is_object<E>::value,
                      with_value_type<typename std::remove_cv<E>::type>,
                      empty>::type {
            };

            template <typename T>
            using member_value_type_t = typename T::value_type;

            template <typename T>
            struct has_member_value_type : exists<member_value_type_t, T> {
            };

            template <typename T>
            using member_element_type_t = typename T::element_type;

            template <typename T>
            struct has_member_element_type : exists<member_element_type_t, T> {
            };

            template <typename T>
            struct readable_traits_helper<
                T,
                typename std::enable_if<
                    has_member_value_type<T>::value &&
                    !has_member_element_type<T>::value>::type>
                : member_value_type<T> {
            };

            template <typename T>
            struct readable_traits_helper<
                T,
                typename std::enable_if<has_member_element_type<T>::value &&
                                        !has_member_value_type<T>::value>::type>
                : _member_element_type<T> {
            };

            template <typename T>
            struct readable_traits_helper<
                T,
                typename std::enable_if<
                    has_member_element_type<T>::value &&
                    has_member_value_type<T>::value>::type> {
            };
        }  // namespace detail

        template <typename T>
        struct readable_traits : detail::readable_traits_helper<T> {
        };

        template <typename T>
        using iter_value_t = typename readable_traits<T>::value_type;

        // sentinel_for
        namespace detail {
            struct sentinel_for_concept {
                template <typename S, typename I>
                auto _test_requires(S s, I i)
                    -> decltype(scn::detail::valid_expr(*i, i == s, i != s));
            };
        }  // namespace detail
        template <typename S, typename I>
        struct sentinel_for
            : std::integral_constant<
                  bool,
                  std::is_default_constructible<S>::value &&
                      std::is_copy_constructible<S>::value &&
                      detail::_requires<detail::sentinel_for_concept, S, I>::
                          value> {
        };

        // sized_sentinel_for
        namespace detail {
            struct sized_sentinel_for_concept {
                template <typename S, typename I>
                auto _test_requires(const S& s, const I& i) -> decltype(
                    detail::requires_expr<
                        std::is_same<decltype(s - i),
                                     iter_difference_t<I>>::value>{},
                    detail::requires_expr<
                        std::is_same<decltype(i - s),
                                     iter_difference_t<I>>::value>{});
            };
        }  // namespace detail
        template <typename S, typename I>
        struct sized_sentinel_for
            : std::integral_constant<
                  bool,
                  detail::_requires<detail::sized_sentinel_for_concept, S, I>::
                          value &&
                      sentinel_for<S, I>::value> {
        };
        template <typename S>
        struct sized_sentinel_for<S, void*> : std::false_type {
        };
        template <typename I>
        struct sized_sentinel_for<void*, I> : std::false_type {
        };
        template <>
        struct sized_sentinel_for<void*, void*> : std::false_type {
        };

        // begin
        namespace _begin {
            template <typename T>
            void begin(T&&) = delete;
            template <typename T>
            void begin(std::initializer_list<T>&&) = delete;

            struct fn {
            private:
                template <typename T, std::size_t N>
                static SCN_CONSTEXPR14 void impl(T(&&)[N],
                                                 detail::priority_tag<3>) =
                    delete;

                template <typename T, std::size_t N>
                static SCN_CONSTEXPR14 auto impl(
                    T (&t)[N],
                    detail::priority_tag<3>) noexcept -> decltype((t) + 0)
                {
                    return (t) + 0;
                }

                template <typename C>
                static SCN_CONSTEXPR14 auto impl(
                    basic_string_view<C> sv,
                    detail::priority_tag<2>) noexcept -> decltype(sv.begin())
                {
                    return sv.begin();
                }

                template <typename T>
                static SCN_CONSTEXPR14 auto
                impl(T& t, detail::priority_tag<1>) noexcept(noexcept(
                    ::scn::custom_ranges::detail::decay_copy(t.begin())))
                    -> decltype(::scn::custom_ranges::detail::decay_copy(
                        t.begin()))
                {
                    return ::scn::custom_ranges::detail::decay_copy(t.begin());
                }

                template <typename T>
                static SCN_CONSTEXPR14 auto
                impl(T&& t, detail::priority_tag<0>) noexcept(
                    noexcept(::scn::custom_ranges::detail::decay_copy(
                        begin(SCN_FWD(t)))))
                    -> decltype(::scn::custom_ranges::detail::decay_copy(
                        begin(SCN_FWD(t))))
                {
                    return ::scn::custom_ranges::detail::decay_copy(
                        begin(SCN_FWD(t)));
                }

            public:
                template <typename T>
                SCN_CONSTEXPR14 auto operator()(T&& t) const noexcept(
                    noexcept(fn::impl(SCN_FWD(t), detail::priority_tag<3>{})))
                    -> decltype(fn::impl(SCN_FWD(t), detail::priority_tag<3>{}))
                {
                    return fn::impl(SCN_FWD(t), detail::priority_tag<3>{});
                }
            };
        }  // namespace _begin
        namespace {
            constexpr auto& begin = detail::static_const<_begin::fn>::value;
        }

        // end
        namespace _end {
            template <typename T>
            void end(T&&) = delete;
            template <typename T>
            void end(std::initializer_list<T>&&) = delete;

            struct fn {
            private:
                template <typename T, std::size_t N>
                static constexpr void impl(T(&&)[N],
                                           detail::priority_tag<2>) = delete;

                template <typename T, std::size_t N>
                static constexpr auto impl(T (&t)[N],
                                           detail::priority_tag<2>) noexcept
                    -> decltype((t) + N)
                {
                    return (t) + N;
                }

                template <typename C>
                static constexpr auto impl(basic_string_view<C> sv,
                                           detail::priority_tag<2>) noexcept
                    -> decltype(sv.end())
                {
                    return sv.end();
                }

                SCN_GCC_PUSH
                SCN_GCC_IGNORE("-Wnoexcept")
                template <typename T,
                          typename S =
                              decltype(::scn::custom_ranges::detail::decay_copy(
                                  SCN_DECLVAL(T&).end())),
                          typename I = decltype(::scn::custom_ranges::begin(
                              SCN_DECLVAL(T&)))>
                static constexpr auto
                impl(T& t, detail::priority_tag<1>) noexcept(
                    noexcept(::scn::custom_ranges::detail::decay_copy(t.end())))
                    -> decltype(::scn::custom_ranges::detail::decay_copy(
                        t.end()))
                {
                    return ::scn::custom_ranges::detail::decay_copy(t.end());
                }

                template <typename T,
                          typename S =
                              decltype(::scn::custom_ranges::detail::decay_copy(
                                  end(SCN_DECLVAL(T)))),
                          typename I = decltype(::scn::custom_ranges::begin(
                              SCN_DECLVAL(T)))>
                static constexpr auto
                impl(T& t, detail::priority_tag<0>) noexcept(noexcept(
                    ::scn::custom_ranges::detail::decay_copy(end(SCN_FWD(t)))))
                    -> S
                {
                    return ::scn::custom_ranges::detail::decay_copy(
                        end(SCN_FWD(t)));
                }

            public:
                template <typename T>
                constexpr auto operator()(T&& t) const noexcept(
                    noexcept(fn::impl(SCN_FWD(t), detail::priority_tag<2>{})))
                    -> decltype(fn::impl(SCN_FWD(t), detail::priority_tag<2>{}))
                {
                    return fn::impl(SCN_FWD(t), detail::priority_tag<2>{});
                }
                SCN_GCC_POP
            };
        }  // namespace _end
        namespace {
            constexpr auto& end = detail::static_const<_end::fn>::value;
        }

        // cbegin
        namespace _cbegin {
            struct fn {
                template <typename T>
                constexpr auto operator()(const T& t) const
                    noexcept(noexcept(::scn::custom_ranges::begin(t)))
                        -> decltype(::scn::custom_ranges::begin(t))
                {
                    return ::scn::custom_ranges::begin(t);
                }

                template <typename T>
                constexpr auto operator()(const T&& t) const noexcept(noexcept(
                    ::scn::custom_ranges::begin(static_cast<const T&&>(t))))
                    -> decltype(::scn::custom_ranges::begin(
                        static_cast<const T&&>(t)))
                {
                    return ::scn::custom_ranges::begin(
                        static_cast<const T&&>(t));
                }
            };
        }  // namespace _cbegin
        namespace {
            constexpr auto& cbegin = detail::static_const<_cbegin::fn>::value;
        }

        // cend
        namespace _cend {
            struct fn {
                template <typename T>
                constexpr auto operator()(const T& t) const
                    noexcept(noexcept(::scn::custom_ranges::end(t)))
                        -> decltype(::scn::custom_ranges::end(t))
                {
                    return ::scn::custom_ranges::end(t);
                }

                template <typename T>
                constexpr auto operator()(const T&& t) const noexcept(noexcept(
                    ::scn::custom_ranges::end(static_cast<const T&&>(t))))
                    -> decltype(::scn::custom_ranges::end(
                        static_cast<const T&&>(t)))
                {
                    return ::scn::custom_ranges::end(static_cast<const T&&>(t));
                }
            };
        }  // namespace _cend
        namespace {
            constexpr auto& cend = detail::static_const<_cend::fn>::value;
        }

        // range
        namespace detail {
            struct range_impl_concept {
                template <typename T>
                auto _test_requires(T&& t)
                    -> decltype(::scn::custom_ranges::begin(SCN_FWD(t)),
                                ::scn::custom_ranges::end(SCN_FWD(t)));
            };
            template <typename T>
            struct range_impl : _requires<range_impl_concept, T> {
            };
            struct range_concept {
                template <typename>
                static auto test(long) -> std::false_type;
                template <typename T>
                static auto test(int) ->
                    typename std::enable_if<range_impl<T&>::value,
                                            std::true_type>::type;
            };
        }  // namespace detail
        template <typename T>
        struct range : decltype(detail::range_concept::test<T>(0)) {
        };

        template <typename T>
        struct forwarding_range
            : std::integral_constant<bool,
                                     range<T>::value &&
                                         detail::range_impl<T>::value> {
        };

        // typedefs
        template <typename R>
        using iterator_t =
            typename std::enable_if<range<R>::value,
                                    decltype(::scn::custom_ranges::begin(
                                        SCN_DECLVAL(R&)))>::type;
        template <typename R>
        using sentinel_t =
            typename std::enable_if<range<R>::value,
                                    decltype(::scn::custom_ranges::end(
                                        SCN_DECLVAL(R&)))>::type;
        template <typename R>
        using range_difference_t =
            typename std::enable_if<range<R>::value,
                                    iter_difference_t<iterator_t<R>>>::type;
        template <typename R>
        using range_value_t =
            typename std::enable_if<range<R>::value,
                                    iter_value_t<iterator_t<R>>>::type;
        template <typename R>
        using range_reference_t =
            typename std::enable_if<range<R>::value,
                                    iter_reference_t<iterator_t<R>>>::type;

        // view
        struct view_base {
        };

        namespace detail {
            template <typename>
            struct is_std_non_view : std::false_type {
            };
            template <typename T>
            struct is_std_non_view<std::initializer_list<T>> : std::true_type {
            };
            template <typename T>
            struct enable_view_helper
                : std::conditional<
                      std::is_base_of<view_base, T>::value,
                      std::true_type,
                      typename std::conditional<
                          is_std_non_view<T>::value,
                          std::false_type,
                          typename std::conditional<
                              range<T>::value && range<const T>::value,
                              std::is_same<range_reference_t<T>,
                                           range_reference_t<const T>>,
                              std::true_type>::type>::type>::type {
            };
            template <typename T>
            struct view_impl
                : std::integral_constant<
                      bool,
                      std::is_copy_constructible<T>::value &&
                          std::is_default_constructible<T>::value &&
                          detail::enable_view_helper<T>::value> {
            };
        }  // namespace detail
        template <typename T>
        struct view : std::conditional<range<T>::value,
                                       detail::view_impl<T>,
                                       std::false_type>::type {
        };

        // data
        template <typename P>
        struct _is_object_pointer
            : std::integral_constant<
                  bool,
                  std::is_pointer<P>::value &&
                      std::is_object<detail::test_t<iter_value_t, P>>::value> {
        };

        namespace _data {
            struct fn {
            private:
                template <typename CharT, typename Traits, typename Allocator>
                static constexpr auto impl(
                    std::basic_string<CharT, Traits, Allocator>& str,
                    detail::priority_tag<2>) noexcept -> typename std::
                    basic_string<CharT, Traits, Allocator>::pointer
                {
                    return std::addressof(*str.begin());
                }
                template <typename CharT, typename Traits, typename Allocator>
                static constexpr auto impl(
                    const std::basic_string<CharT, Traits, Allocator>& str,
                    detail::priority_tag<2>) noexcept -> typename std::
                    basic_string<CharT, Traits, Allocator>::const_pointer
                {
                    return std::addressof(*str.begin());
                }
                template <typename CharT, typename Traits, typename Allocator>
                static constexpr auto impl(
                    std::basic_string<CharT, Traits, Allocator>&& str,
                    detail::priority_tag<2>) noexcept -> typename std::
                    basic_string<CharT, Traits, Allocator>::pointer
                {
                    return std::addressof(*str.begin());
                }

                template <typename T,
                          typename D =
                              decltype(::scn::custom_ranges::detail::decay_copy(
                                  SCN_DECLVAL(T&).data()))>
                static constexpr auto
                impl(T& t, detail::priority_tag<1>) noexcept(noexcept(
                    ::scn::custom_ranges::detail::decay_copy(t.data()))) ->
                    typename std::enable_if<_is_object_pointer<D>::value,
                                            D>::type
                {
                    return ::scn::custom_ranges::detail::decay_copy(t.data());
                }

                template <typename T>
                static constexpr auto
                impl(T&& t, detail::priority_tag<0>) noexcept(
                    noexcept(::scn::custom_ranges::begin(SCN_FWD(t)))) ->
                    typename std::enable_if<
                        _is_object_pointer<decltype(::scn::custom_ranges::begin(
                            SCN_FWD(t)))>::value,
                        decltype(::scn::custom_ranges::begin(SCN_FWD(t)))>::type
                {
                    return ::scn::custom_ranges::begin(SCN_FWD(t));
                }

            public:
                template <typename T>
                constexpr auto operator()(T&& t) const noexcept(
                    noexcept(fn::impl(SCN_FWD(t), detail::priority_tag<2>{})))
                    -> decltype(fn::impl(SCN_FWD(t), detail::priority_tag<2>{}))
                {
                    return fn::impl(SCN_FWD(t), detail::priority_tag<2>{});
                }
            };
        }  // namespace _data
        namespace {
            constexpr auto& data = detail::static_const<_data::fn>::value;
        }

        // size
        template <typename>
        struct disable_sized_range : std::false_type {
        };

        namespace _size {
            template <typename T>
            void size(T&&) = delete;
            template <typename T>
            void size(T&) = delete;

            struct fn {
            private:
                template <typename T, std::size_t N>
                static constexpr std::size_t impl(
                    const T(&&)[N],
                    detail::priority_tag<3>) noexcept
                {
                    return N;
                }

                template <typename T, std::size_t N>
                static constexpr std::size_t impl(
                    const T (&)[N],
                    detail::priority_tag<3>) noexcept
                {
                    return N;
                }

                template <typename T,
                          typename I =
                              decltype(::scn::custom_ranges::detail::decay_copy(
                                  SCN_DECLVAL(T).size()))>
                static constexpr auto
                impl(T&& t, detail::priority_tag<2>) noexcept(
                    noexcept(::scn::custom_ranges::detail::decay_copy(
                        SCN_FWD(t).size()))) ->
                    typename std::enable_if<
                        std::is_integral<I>::value &&
                            !disable_sized_range<
                                detail::remove_cvref_t<T>>::value,
                        I>::type
                {
                    return ::scn::custom_ranges::detail::decay_copy(
                        SCN_FWD(t).size());
                }

                template <typename T,
                          typename I =
                              decltype(::scn::custom_ranges::detail::decay_copy(
                                  size(SCN_DECLVAL(T))))>
                static constexpr auto
                impl(T&& t, detail::priority_tag<1>) noexcept(noexcept(
                    ::scn::custom_ranges::detail::decay_copy(size(SCN_FWD(t)))))
                    -> typename std::enable_if<
                        std::is_integral<I>::value &&
                            !disable_sized_range<
                                detail::remove_cvref_t<T>>::value,
                        I>::type
                {
                    return ::scn::custom_ranges::detail::decay_copy(
                        size(SCN_FWD(t)));
                }

                template <typename T,
                          typename I = decltype(::scn::custom_ranges::begin(
                              SCN_DECLVAL(T))),
                          typename S = decltype(::scn::custom_ranges::end(
                              SCN_DECLVAL(T))),
                          typename D =
                              decltype(::scn::custom_ranges::detail::decay_copy(
                                  SCN_DECLVAL(S) - SCN_DECLVAL(I)))>
                static constexpr auto
                impl(T&& t, detail::priority_tag<0>) noexcept(
                    noexcept(::scn::custom_ranges::detail::decay_copy(
                        ::scn::custom_ranges::end(t) -
                        ::scn::custom_ranges::begin(t)))) ->
                    typename std::enable_if<
                        !std::is_array<detail::remove_cvref_t<T>>::value,
                        D>::type
                {
                    return ::scn::custom_ranges::detail::decay_copy(
                        ::scn::custom_ranges::end(t) -
                        ::scn::custom_ranges::begin(t));
                }

            public:
                template <typename T>
                constexpr auto operator()(T&& t) const noexcept(
                    noexcept(fn::impl(SCN_FWD(t), detail::priority_tag<3>{})))
                    -> decltype(fn::impl(SCN_FWD(t), detail::priority_tag<3>{}))
                {
                    return fn::impl(SCN_FWD(t), detail::priority_tag<3>{});
                }
            };
        }  // namespace _size
        namespace {
            constexpr auto& size = detail::static_const<_size::fn>::value;
        }

        // empty
        namespace _empty_ns {
            struct fn {
            private:
                template <typename T>
                static constexpr auto
                impl(T&& t, detail::priority_tag<2>) noexcept(
                    noexcept((bool(SCN_FWD(t).empty()))))
                    -> decltype((bool(SCN_FWD(t).empty())))
                {
                    return bool((SCN_FWD(t).empty()));
                }
                template <typename T>
                static constexpr auto
                impl(T&& t, detail::priority_tag<1>) noexcept(
                    noexcept(::scn::custom_ranges::size(SCN_FWD(t)) == 0))
                    -> decltype(::scn::custom_ranges::size(SCN_FWD(t)) == 0)
                {
                    return ::scn::custom_ranges::size(SCN_FWD(t)) == 0;
                }

                template <typename T,
                          typename I = decltype(::scn::custom_ranges::begin(
                              SCN_DECLVAL(T)))>
                static constexpr auto
                impl(T&& t, detail::priority_tag<0>) noexcept(
                    noexcept(::scn::custom_ranges::begin(t) ==
                             ::scn::custom_ranges::end(t)))
                    -> decltype(::scn::custom_ranges::begin(t) ==
                                ::scn::custom_ranges::end(t))
                {
                    return ::scn::custom_ranges::begin(t) ==
                           ::scn::custom_ranges::end(t);
                }

            public:
                template <typename T>
                constexpr auto operator()(T&& t) const noexcept(
                    noexcept(fn::impl(SCN_FWD(t), detail::priority_tag<2>{})))
                    -> decltype(fn::impl(SCN_FWD(t), detail::priority_tag<2>{}))
                {
                    return fn::impl(SCN_FWD(t), detail::priority_tag<2>{});
                }
            };
        }  // namespace _empty_ns
        namespace {
            constexpr auto& empty = detail::static_const<_empty_ns::fn>::value;
        }

        // sized_range
        namespace detail {
            struct sized_range_concept {
                template <typename T>
                auto _test_requires(T& t)
                    -> decltype(::scn::custom_ranges::size(t));
            };
        }  // namespace detail
        template <typename T>
        struct sized_range
            : std::integral_constant<
                  bool,
                  range<T>::value &&
                      !disable_sized_range<detail::remove_cvref_t<T>>::value &&
                      detail::_requires<detail::sized_range_concept,
                                        T>::value> {
        };

        // contiguous_range
        namespace detail {
            struct contiguous_range_concept {
                template <typename>
                static auto test(long) -> std::false_type;
                template <typename T>
                static auto test(int) -> typename std::enable_if<
                    _requires<contiguous_range_concept, T>::value,
                    std::true_type>::type;

                template <typename T>
                auto _test_requires(T& t)
                    -> decltype(requires_expr<std::is_same<
                                    decltype(::scn::custom_ranges::data(t)),
                                    typename std::add_pointer<
                                        range_reference_t<T>>::type>::value>{});
            };
        }  // namespace detail
        template <typename T>
        struct contiguous_range
            : decltype(detail::contiguous_range_concept::test<T>(0)) {
        };

        // subrange
        template <typename D>
        class view_interface : public view_base {
            static_assert(std::is_class<D>::value, "");
            static_assert(
                std::is_same<D, typename std::remove_cv<D>::type>::value,
                "");

        private:
            SCN_CONSTEXPR14 D& derived() noexcept
            {
                return static_cast<D&>(*this);
            }
            constexpr D& derived() const noexcept
            {
                return static_cast<const D&>(*this);
            }

        public:
            SCN_NODISCARD SCN_CONSTEXPR14 bool empty()
            {
                return ::scn::custom_ranges::begin(derived()) ==
                       ::scn::custom_ranges::end(derived());
            }
            SCN_NODISCARD constexpr bool empty() const
            {
                return ::scn::custom_ranges::begin(derived()) ==
                       ::scn::custom_ranges::end(derived());
            }

            template <typename R = D,
                      typename = decltype(::scn::custom_ranges::empty(
                          SCN_DECLVAL(R&)))>
            SCN_CONSTEXPR14 explicit operator bool()
            {
                return !::scn::custom_ranges::empty(derived());
            }
            template <typename R = D,
                      typename = decltype(::scn::custom_ranges::empty(
                          SCN_DECLVAL(const R&)))>
            constexpr explicit operator bool() const
            {
                return !::scn::custom_ranges::empty(derived());
            }

            template <typename R = D,
                      typename std::enable_if<
                          contiguous_range<R>::value>::type* = nullptr>
            auto data() -> decltype(std::addressof(
                *::scn::custom_ranges::begin(static_cast<R&>(*this))))
            {
                return ::scn::custom_ranges::empty(derived())
                           ? nullptr
                           : std::addressof(
                                 *::scn::custom_ranges::begin(derived()));
            }
            template <typename R = D,
                      typename std::enable_if<
                          contiguous_range<const R>::value>::type* = nullptr>
            auto data() const -> decltype(std::addressof(
                *::scn::custom_ranges::begin(static_cast<const R&>(*this))))
            {
                return ::scn::custom_ranges::empty(derived())
                           ? nullptr
                           : std::addressof(
                                 *::scn::custom_ranges::begin(derived()));
            }

            template <typename R = D,
                      typename std::enable_if<
                          range<R>::value &&
                          sized_sentinel_for<sentinel_t<R>, iterator_t<R>>::
                              value>::type* = nullptr>
            SCN_CONSTEXPR14 auto size()
                -> decltype(::scn::custom_ranges::end(static_cast<R&>(*this)) -
                            ::scn::custom_ranges::begin(static_cast<R&>(*this)))
            {
                return ::scn::custom_ranges::end(derived()) -
                       ::scn::custom_ranges::begin(derived());
            }

            template <
                typename R = D,
                typename std::enable_if<
                    range<const R>::value &&
                    sized_sentinel_for<sentinel_t<const R>,
                                       iterator_t<const R>>::value>::type* =
                    nullptr>
            constexpr auto size() const
                -> decltype(::scn::custom_ranges::end(
                                static_cast<const R&>(*this)) -
                            ::scn::custom_ranges::begin(
                                static_cast<const R&>(*this)))
            {
                return ::scn::custom_ranges::end(derived()) -
                       ::scn::custom_ranges::begin(derived());
            }
        };

        enum class subrange_kind : bool { unsized, sized };

        namespace detail {
            template <typename I, typename S>
            struct default_subrange_kind
                : std::integral_constant<subrange_kind,
                                         sized_sentinel_for<S, I>::value
                                             ? subrange_kind::sized
                                             : subrange_kind::unsized> {
            };
        }  // namespace detail

        namespace _subrange {
            template <typename I,
                      typename S = I,
                      subrange_kind = scn::custom_ranges::detail::
                          default_subrange_kind<I, S>::value>
            class subrange;
        }  // namespace _subrange

        using _subrange::subrange;

        namespace detail {
            struct pair_like_concept {
                template <typename>
                static auto test(long) -> std::false_type;
                template <typename T,
                          typename = typename std::tuple_size<T>::type>
                static auto test(int) -> typename std::enable_if<
                    _requires<pair_like_concept, T>::value,
                    std::true_type>::type;

                template <typename T>
                auto _test_requires(T t) -> decltype(
                    requires_expr<
                        std::is_base_of<std::integral_constant<std::size_t, 2>,
                                        std::tuple_size<T>>::value>{},
                    std::declval<std::tuple_element<
                        0,
                        typename std::remove_const<T>::type>>(),
                    std::declval<std::tuple_element<
                        1,
                        typename std::remove_const<T>::type>>(),
                    requires_expr<std::is_convertible<
                        decltype(std::get<0>(t)),
                        const std::tuple_element<0, T>&>::value>{},
                    requires_expr<std::is_convertible<
                        decltype(std::get<1>(t)),
                        const std::tuple_element<1, T>&>::value>{});
            };
            template <typename T>
            struct pair_like
                : std::integral_constant<
                      bool,
                      !std::is_reference<T>::value &&
                          decltype(pair_like_concept::test<T>(0))::value> {
            };

            struct pair_like_convertible_to_concept {
                template <typename T, typename U, typename V>
                auto _test_requires(T&& t) -> decltype(
                    requires_expr<
                        std::is_convertible<decltype(std::get<0>(SCN_FWD(t))),
                                            U>::value>{},
                    requires_expr<
                        std::is_convertible<decltype(std::get<1>(SCN_FWD(t))),
                                            V>::value>{});
            };
            template <typename T, typename U, typename V>
            struct pair_like_convertible_to
                : std::integral_constant<
                      bool,
                      !range<T>::value &&
                          pair_like<
                              typename std::remove_reference<T>::type>::value &&
                          _requires<pair_like_convertible_to_concept, T, U, V>::
                              value> {
            };
            template <typename T, typename U, typename V>
            struct pair_like_convertible_from
                : std::integral_constant<
                      bool,
                      !range<T>::value &&
                          pair_like<
                              typename std::remove_reference<T>::type>::value &&
                          std::is_constructible<T, U, V>::value> {
            };

            struct iterator_sentinel_pair_concept {
                template <typename>
                static auto test(long) -> std::false_type;
                template <typename T>
                static auto test(int) -> typename std::enable_if<
                    !range<T>::value && pair_like<T>::value &&
                        sentinel_for<
                            typename std::tuple_element<1, T>::type,
                            typename std::tuple_element<0, T>::type>::value,
                    std::true_type>::type;
            };
            template <typename T>
            struct iterator_sentinel_pair
                : decltype(iterator_sentinel_pair_concept::test<T>(0)) {
            };

            template <typename I, typename S, bool StoreSize = false>
            struct subrange_data {
                constexpr subrange_data() = default;
                constexpr subrange_data(I&& b, S&& e)
                    : begin(SCN_MOVE(b)), end(SCN_MOVE(e))
                {
                }
                template <bool Dependent = true>
                constexpr subrange_data(
                    I&& b,
                    S&& e,
                    typename std::enable_if<Dependent,
                                            iter_difference_t<I>>::type)
                    : begin(SCN_MOVE(b)), end(SCN_MOVE(e))
                {
                }

                constexpr iter_difference_t<I> get_size() const
                {
                    return distance(begin, end);
                }

                I begin{};
                S end{};
            };

            template <typename I, typename S>
            struct subrange_data<I, S, true> {
                constexpr subrange_data() = default;
                constexpr subrange_data(I&& b, S&& e, iter_difference_t<I> s)
                    : begin(SCN_MOVE(b)), end(SCN_MOVE(e)), size(s)
                {
                }

                constexpr iter_difference_t<I> get_size() const
                {
                    return size;
                }

                I begin{};
                S end{};
                iter_difference_t<I> size{0};
            };

            template <typename R, typename I, typename S, subrange_kind K>
            auto subrange_range_constructor_constraint_helper_fn(long)
                -> std::false_type;

            template <typename R, typename I, typename S, subrange_kind K>
            auto subrange_range_constructor_constraint_helper_fn(int) ->
                typename std::enable_if<
                    forwarding_range<R>::value &&
                        std::is_convertible<iterator_t<R>, I>::value &&
                        std::is_convertible<sentinel_t<R>, S>::value,
                    std::true_type>::type;

            template <typename R, typename I, typename S, subrange_kind K>
            struct subrange_range_constructor_constraint_helper
                : decltype(subrange_range_constructor_constraint_helper_fn<R,
                                                                           I,
                                                                           S,
                                                                           K>(
                      0)) {
            };

            template <typename R>
            constexpr subrange_kind subrange_deduction_guide_helper()
            {
                return (sized_range<R>::value ||
                        sized_sentinel_for<sentinel_t<R>, iterator_t<R>>::value)
                           ? subrange_kind::sized
                           : subrange_kind::unsized;
            }

            template <typename T, typename U>
            struct not_same_as : std::integral_constant<
                                     bool,
                                     !std::is_same<remove_cvref_t<T>,
                                                   remove_cvref_t<U>>::value> {
            };
        }  // namespace detail

        namespace _subrange {
            template <typename I, typename S, subrange_kind K>
            class subrange : public view_interface<subrange<I, S, K>> {
                static_assert(sentinel_for<S, I>::value, "");
                static_assert(K == subrange_kind::sized ||
                                  !sized_sentinel_for<S, I>::value,
                              "");

                static constexpr bool _store_size =
                    K == subrange_kind::sized &&
                    !sized_sentinel_for<S, I>::value;

            public:
                using iterator = I;
                using sentinel = S;

                subrange() = default;

                template <bool SS = _store_size,
                          typename std::enable_if<!SS>::type* = nullptr>
                SCN_CONSTEXPR14 subrange(I i, S s)
                    : m_data{SCN_MOVE(i), SCN_MOVE(s)}
                {
                }
                template <bool Dependent = true,
                          subrange_kind KK = K,
                          typename std::enable_if<
                              KK == subrange_kind::sized>::type* = nullptr>
                SCN_CONSTEXPR14 subrange(
                    I i,
                    S s,
                    typename std::enable_if<Dependent,
                                            iter_difference_t<I>>::type n)
                    : m_data{SCN_MOVE(i), SCN_MOVE(s), n}
                {
                }

                constexpr I begin() const noexcept
                {
                    return m_data.begin;
                }

                constexpr S end() const noexcept
                {
                    return m_data.end;
                }

                SCN_NODISCARD constexpr bool empty() const noexcept
                {
                    return m_data.begin == m_data.end;
                }

                template <subrange_kind KK = K,
                          typename std::enable_if<
                              KK == subrange_kind::sized>::type* = nullptr>
                constexpr iter_difference_t<I> size() const noexcept
                {
                    return m_data.get_size();
                }

            private:
                detail::subrange_data<I, S, _store_size> m_data{};
            };

            template <typename I, typename S, subrange_kind K>
            I begin(subrange<I, S, K>&& r) noexcept
            {
                return r.begin();
            }
            template <typename I, typename S, subrange_kind K>
            S end(subrange<I, S, K>&& r) noexcept
            {
                return r.end();
            }
        }  // namespace _subrange

        namespace detail {
            template <std::size_t N>
            struct subrange_get_impl;
            template <>
            struct subrange_get_impl<0> {
                template <typename I, typename S, subrange_kind K>
                static auto get(const subrange<I, S, K>& s)
                    -> decltype(s.begin())
                {
                    return s.begin();
                }
            };
            template <>
            struct subrange_get_impl<1> {
                template <typename I, typename S, subrange_kind K>
                static auto get(const subrange<I, S, K>& s) -> decltype(s.end())
                {
                    return s.end();
                }
            };
        }  // namespace detail

        template <std::size_t N,
                  typename I,
                  typename S,
                  subrange_kind K,
                  typename std::enable_if<(N < 2)>::type* = nullptr>
        auto get(const subrange<I, S, K>& s)
            -> decltype(detail::subrange_get_impl<N>::get(s))
        {
            return detail::subrange_get_impl<N>::get(s);
        }

        // reconstructible_range
        template <typename R>
        struct pair_reconstructible_range
            : std::integral_constant<
                  bool,
                  range<R>::value &&
                      forwarding_range<
                          typename std::remove_reference<R>::type>::value &&
                      std::is_constructible<R, iterator_t<R>, sentinel_t<R>>::
                          value> {
        };
        template <typename R>
        struct reconstructible_range
            : std::integral_constant<
                  bool,
                  range<R>::value &&
                      forwarding_range<
                          typename std::remove_reference<R>::type>::value &&
                      std::is_constructible<
                          R,
                          subrange<iterator_t<R>, sentinel_t<R>>>::value> {
        };
    }  // namespace custom_ranges

    namespace polyfill_2a {
        // bidir iterator
        namespace detail {
            struct bidirectional_iterator_concept {
                template <typename I>
                auto _test_requires(I i)
                    -> decltype(custom_ranges::detail::requires_expr<
                                std::is_same<decltype(i--), I>::value>{});
                template <typename>
                static auto test(long) -> std::false_type;
                template <typename I>
                static auto test(int) -> typename std::enable_if<
                    std::is_base_of<
                        custom_ranges::bidirectional_iterator_tag,
                        custom_ranges::iterator_category_t<I>>::value &&
                        custom_ranges::detail::
                            _requires<bidirectional_iterator_concept, I>::value,
                    std::true_type>::type;
            };
        }  // namespace detail
        template <typename I>
        struct bidirectional_iterator
            : decltype(detail::bidirectional_iterator_concept::test<I>(0)) {
        };

        // random access iterator
        namespace detail {
            struct random_access_iterator_concept {
                template <typename I>
                auto _test_requires(I i,
                                    const I j,
                                    const custom_ranges::iter_difference_t<I> n)
                    -> decltype(valid_expr(
                        j + n,
                        custom_ranges::detail::requires_expr<
                            std::is_same<decltype(j + n), I>::value>{},
                        n + j,
#ifndef _MSC_VER
                        custom_ranges::detail::requires_expr<
                            std::is_same<decltype(n + j), I>::value>{},
#endif
                        j - n,
                        custom_ranges::detail::requires_expr<
                            std::is_same<decltype(j - n), I>::value>{},
                        j[n],
                        custom_ranges::detail::requires_expr<std::is_same<
                            decltype(j[n]),
                            custom_ranges::iter_reference_t<I>>::value>{},
                        custom_ranges::detail::requires_expr<
                            std::is_convertible<decltype(i < j),
                                                bool>::value>{}));
                template <typename>
                static auto test(long) -> std::false_type;
                template <typename I>
                static auto test(int) -> typename std::enable_if<
                    bidirectional_iterator<I>::value &&
                        std::is_base_of<
                            custom_ranges::random_access_iterator_tag,
                            custom_ranges::iterator_category_t<I>>::value &&
                        custom_ranges::sized_sentinel_for<I, I>::value &&
                        custom_ranges::detail::
                            _requires<random_access_iterator_concept, I>::value,
                    std::true_type>::type;
            };
        }  // namespace detail
        template <typename I>
        struct random_access_iterator
            : decltype(detail::random_access_iterator_concept::test<I>(0)) {
        };
    }  // namespace polyfill_2a

    namespace custom_ranges {
        // advance
        namespace _advance {
            struct fn {
            private:
                template <typename T>
                static constexpr T abs(T t)
                {
                    return t < T{0} ? -t : t;
                }

                template <
                    typename R,
                    typename std::enable_if<polyfill_2a::random_access_iterator<
                        R>::value>::type* = nullptr>
                static SCN_CONSTEXPR14 void impl(R& r, iter_difference_t<R> n)
                {
                    r += n;
                }

                template <
                    typename I,
                    typename std::enable_if<
                        polyfill_2a::bidirectional_iterator<I>::value &&
                        !polyfill_2a::random_access_iterator<I>::value>::type* =
                        nullptr>
                static SCN_CONSTEXPR14 void impl(I& i, iter_difference_t<I> n)
                {
                    constexpr auto zero = iter_difference_t<I>{0};

                    if (n > zero) {
                        while (n-- > zero) {
                            ++i;
                        }
                    }
                    else {
                        while (n++ < zero) {
                            --i;
                        }
                    }
                }

                template <
                    typename I,
                    typename std::enable_if<
                        !polyfill_2a::bidirectional_iterator<I>::value>::type* =
                        nullptr>
                static SCN_CONSTEXPR14 void impl(I& i, iter_difference_t<I> n)
                {
                    while (n-- > iter_difference_t<I>{0}) {
                        ++i;
                    }
                }

                template <
                    typename I,
                    typename S,
                    typename std::enable_if<
                        std::is_assignable<I&, S>::value>::type* = nullptr>
                static SCN_CONSTEXPR14 void impl(I& i,
                                                 S bound,
                                                 detail::priority_tag<2>)
                {
                    i = SCN_MOVE(bound);
                }

                template <typename I,
                          typename S,
                          typename std::enable_if<
                              sized_sentinel_for<S, I>::value>::type* = nullptr>
                static SCN_CONSTEXPR14 void impl(I& i,
                                                 S bound,
                                                 detail::priority_tag<1>)
                {
                    fn::impl(i, bound - i);
                }

                template <typename I, typename S>
                static SCN_CONSTEXPR14 void impl(I& i,
                                                 S bound,
                                                 detail::priority_tag<0>)
                {
                    while (i != bound) {
                        ++i;
                    }
                }

                template <typename I,
                          typename S,
                          typename std::enable_if<
                              sized_sentinel_for<S, I>::value>::type* = nullptr>
                static SCN_CONSTEXPR14 auto impl(I& i,
                                                 iter_difference_t<I> n,
                                                 S bound)
                    -> iter_difference_t<I>
                {
                    if (fn::abs(n) >= fn::abs(bound - i)) {
                        auto dist = bound - i;
                        fn::impl(i, bound, detail::priority_tag<2>{});
                        return dist;
                    }
                    else {
                        fn::impl(i, n);
                        return n;
                    }
                }

                template <
                    typename I,
                    typename S,
                    typename std::enable_if<
                        polyfill_2a::bidirectional_iterator<I>::value &&
                        !sized_sentinel_for<S, I>::value>::type* = nullptr>
                static SCN_CONSTEXPR14 auto impl(I& i,
                                                 iter_difference_t<I> n,
                                                 S bound)
                    -> iter_difference_t<I>
                {
                    constexpr iter_difference_t<I> zero{0};
                    iter_difference_t<I> counter{0};

                    if (n < zero) {
                        do {
                            --i;
                            --counter;
                        } while (++n < zero && i != bound);
                    }
                    else {
                        while (n-- > zero && i != bound) {
                            ++i;
                            ++counter;
                        }
                    }

                    return counter;
                }

                template <
                    typename I,
                    typename S,
                    typename std::enable_if<
                        !polyfill_2a::bidirectional_iterator<I>::value &&
                        !sized_sentinel_for<S, I>::value>::type* = nullptr>
                static SCN_CONSTEXPR14 auto impl(I& i,
                                                 iter_difference_t<I> n,
                                                 S bound)
                    -> iter_difference_t<I>
                {
                    constexpr iter_difference_t<I> zero{0};
                    iter_difference_t<I> counter{0};

                    while (n-- > zero && i != bound) {
                        ++i;
                        ++counter;
                    }

                    return counter;
                }

            public:
                template <typename I>
                SCN_CONSTEXPR14 void operator()(I& i,
                                                iter_difference_t<I> n) const
                {
                    fn::impl(i, n);
                }

                template <typename I,
                          typename S,
                          typename std::enable_if<
                              sentinel_for<S, I>::value>::type* = nullptr>
                SCN_CONSTEXPR14 void operator()(I& i, S bound) const
                {
                    fn::impl(i, bound, detail::priority_tag<2>{});
                }

                template <typename I,
                          typename S,
                          typename std::enable_if<
                              sentinel_for<S, I>::value>::type* = nullptr>
                SCN_CONSTEXPR14 iter_difference_t<I>
                operator()(I& i, iter_difference_t<I> n, S bound) const
                {
                    return n - fn::impl(i, n, bound);
                }
            };
        }  // namespace _advance
        namespace {
            constexpr auto& advance = detail::static_const<_advance::fn>::value;
        }

        // distance
        SCN_GCC_PUSH
        SCN_GCC_IGNORE("-Wnoexcept")
        namespace _distance {
            struct fn {
            private:
                template <typename I, typename S>
                static SCN_CONSTEXPR14 auto impl(I i,
                                                 S s) noexcept(noexcept(s - i))
                    -> typename std::enable_if<sized_sentinel_for<S, I>::value,
                                               iter_difference_t<I>>::type
                {
                    return s - i;
                }

                template <typename I, typename S>
                static SCN_CONSTEXPR14 auto impl(I i, S s) noexcept(
                    noexcept(i != s, ++i, ++SCN_DECLVAL(iter_difference_t<I>&)))
                    -> typename std::enable_if<!sized_sentinel_for<S, I>::value,
                                               iter_difference_t<I>>::type
                {
                    iter_difference_t<I> counter{0};
                    while (i != s) {
                        ++i;
                        ++counter;
                    }
                    return counter;
                }

                template <typename R>
                static SCN_CONSTEXPR14 auto impl(R&& r) noexcept(
                    noexcept(::scn::custom_ranges::size(r))) ->
                    typename std::enable_if<
                        sized_range<R>::value,
                        iter_difference_t<iterator_t<R>>>::type
                {
                    return static_cast<iter_difference_t<iterator_t<R>>>(
                        ::scn::custom_ranges::size(r));
                }

                template <typename R>
                static SCN_CONSTEXPR14 auto impl(R&& r) noexcept(
                    noexcept(fn::impl(::scn::custom_ranges::begin(r),
                                      ::scn::custom_ranges::end(r)))) ->
                    typename std::enable_if<
                        !sized_range<R>::value,
                        iter_difference_t<iterator_t<R>>>::type
                {
                    return fn::impl(::scn::custom_ranges::begin(r),
                                    ::scn::custom_ranges::end(r));
                }

            public:
                template <typename I, typename S>
                SCN_CONSTEXPR14 auto operator()(I first, S last) const
                    noexcept(noexcept(fn::impl(SCN_MOVE(first),
                                               SCN_MOVE(last)))) ->
                    typename std::enable_if<sentinel_for<S, I>::value,
                                            iter_difference_t<I>>::type
                {
                    return fn::impl(SCN_MOVE(first), SCN_MOVE(last));
                }

                template <typename R>
                SCN_CONSTEXPR14 auto operator()(R&& r) const
                    noexcept(noexcept(fn::impl(SCN_FWD(r)))) ->
                    typename std::enable_if<
                        range<R>::value,
                        iter_difference_t<iterator_t<R>>>::type
                {
                    return fn::impl(SCN_FWD(r));
                }
            };
        }  // namespace _distance
        namespace {
            constexpr auto& distance =
                detail::static_const<_distance::fn>::value;
        }
        SCN_GCC_POP  // -Wnoexcept
    }                // namespace custom_ranges

    namespace polyfill_2a {
        template <typename T>
        using iter_value_t = ::scn::custom_ranges::iter_value_t<T>;
        template <typename T>
        using iter_reference_t = ::scn::custom_ranges::iter_reference_t<T>;
        template <typename T>
        using iter_difference_t = ::scn::custom_ranges::iter_difference_t<T>;
    }  // namespace polyfill_2a

    SCN_END_NAMESPACE
}  // namespace scn

namespace std {
    template <typename I, typename S, ::scn::custom_ranges::subrange_kind K>
    struct tuple_size<::scn::custom_ranges::subrange<I, S, K>>
        : public integral_constant<size_t, 2> {
    };

    template <typename I, typename S, ::scn::custom_ranges::subrange_kind K>
    struct tuple_element<0, ::scn::custom_ranges::subrange<I, S, K>> {
        using type = I;
    };
    template <typename I, typename S, ::scn::custom_ranges::subrange_kind K>
    struct tuple_element<1, ::scn::custom_ranges::subrange<I, S, K>> {
        using type = S;
    };

    using ::scn::custom_ranges::get;
}  // namespace std

#define SCN_CHECK_CONCEPT(C) C::value

#endif  // SCN_RANGES_CUSTOM_IMPL_H
