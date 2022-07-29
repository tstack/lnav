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

#ifndef SCN_RANGES_UTIL_H
#define SCN_RANGES_UTIL_H

#include "../util/meta.h"

namespace scn {
    SCN_BEGIN_NAMESPACE

    namespace custom_ranges {
        namespace detail {
            template <size_t N>
            using priority_tag = ::scn::detail::priority_tag<N>;

            template <typename... Ts>
            using void_t = ::scn::detail::void_t<Ts...>;

            template <typename T>
            using static_const = ::scn::detail::static_const<T>;

            template <typename T>
            using remove_cvref_t = ::scn::detail::remove_cvref_t<T>;

            template <typename T>
            constexpr typename std::decay<T>::type decay_copy(T&& t) noexcept(
                noexcept(static_cast<typename std::decay<T>::type>(SCN_FWD(t))))
            {
                return SCN_FWD(t);
            }

            struct nonesuch {
                nonesuch() = delete;
                nonesuch(nonesuch const&) = delete;
                nonesuch& operator=(const nonesuch&) = delete;
                ~nonesuch() = delete;
            };

            template <typename Void,
                      template <class...>
                      class Trait,
                      typename... Args>
            struct test {
                using type = nonesuch;
            };

            template <template <class...> class Trait, typename... Args>
            struct test<void_t<Trait<Args...>>, Trait, Args...> {
                using type = Trait<Args...>;
            };

            template <template <class...> class Trait, typename... Args>
            using test_t = typename test<void, Trait, Args...>::type;

            template <typename Void,
                      template <class...>
                      class AliasT,
                      typename... Args>
            struct exists_helper : std::false_type {
            };

            template <template <class...> class AliasT, typename... Args>
            struct exists_helper<void_t<AliasT<Args...>>, AliasT, Args...>
                : std::true_type {
            };

            template <template <class...> class AliasT, typename... Args>
            struct exists : exists_helper<void, AliasT, Args...> {
            };

            template <typename R,
                      typename... Args,
                      typename = decltype(&R::template _test_requires<Args...>)>
            auto test_requires(R&) -> void;

            template <typename R, typename... Args>
            using test_requires_t =
                decltype(test_requires<R, Args...>(SCN_DECLVAL(R&)));

            template <typename R, typename... Args>
            struct _requires : exists<test_requires_t, R, Args...> {
            };

            template <bool Expr>
            using requires_expr = typename std::enable_if<Expr, int>::type;

            template <typename...>
            struct get_common_type;

            template <typename T, typename U>
            struct copy_cv {
                using type = U;
            };
            template <typename T, typename U>
            struct copy_cv<const T, U> {
                using type = typename std::add_const<U>::type;
            };
            template <typename T, typename U>
            struct copy_cv<volatile T, U> {
                using type = typename std::add_volatile<U>::type;
            };
            template <typename T, typename U>
            struct copy_cv<const volatile T, U> {
                using type = typename std::add_cv<U>::type;
            };
            template <typename T, typename U>
            using copy_cv_t = typename copy_cv<T, U>::type;

            template <typename T>
            using cref_t = typename std::add_lvalue_reference<
                const typename std::remove_reference<T>::type>::type;

            template <typename T>
            struct rref_res {
                using type = T;
            };
            template <typename T>
            struct rref_res<T&> {
                using type = typename std::remove_reference<T>::type&&;
            };
            template <typename T>
            using rref_res_t = typename rref_res<T>::type;

            template <typename T, typename U>
            using cond_res_t =
                decltype(SCN_DECLVAL(bool) ? std::declval<T (&)()>()()
                                           : std::declval<U (&)()>()());

            template <typename T, typename U>
            struct simple_common_reference {
            };

            template <
                typename T,
                typename U,
                typename C =
                    test_t<cond_res_t, copy_cv_t<T, U>&, copy_cv_t<U, T>&>>
            struct lvalue_simple_common_reference
                : std::enable_if<std::is_reference<C>::value, C> {
            };
            template <typename T, typename U>
            using lvalue_scr_t =
                typename lvalue_simple_common_reference<T, U>::type;
            template <typename T, typename U>
            struct simple_common_reference<T&, U&>
                : lvalue_simple_common_reference<T, U> {
            };

            template <typename T,
                      typename U,
                      typename LCR = test_t<lvalue_scr_t, T, U>,
                      typename C = rref_res_t<LCR>>
            struct rvalue_simple_common_reference
                : std::enable_if<std::is_convertible<T&&, C>::value &&
                                 std::is_convertible<U&&, C>::value>::type {
            };
            template <typename T, typename U>
            struct simple_common_reference<T&&, U&&>
                : rvalue_simple_common_reference<T, U> {
            };

            template <typename A,
                      typename B,
                      typename C = test_t<lvalue_scr_t, A, const B>>
            struct mixed_simple_common_reference
                : std::enable_if<std::is_convertible<B&&, C>::value, C>::type {
            };

            template <typename A, typename B>
            struct simple_common_reference<A&, B&&>
                : mixed_simple_common_reference<A, B> {
            };
            template <typename A, typename B>
            struct simple_common_reference<A&&, B&>
                : simple_common_reference<B&&, A&> {
            };
            template <typename T, typename U>
            using simple_common_reference_t =
                typename simple_common_reference<T, U>::type;

            template <typename>
            struct xref {
                template <typename U>
                using type = U;
            };

            template <typename A>
            struct xref<A&> {
                template <typename U>
                using type = typename std::add_lvalue_reference<
                    typename xref<A>::template type<U>>::type;
            };

            template <typename A>
            struct xref<A&&> {
                template <typename U>
                using type = typename std::add_rvalue_reference<
                    typename xref<A>::template type<U>>::type;
            };

            template <typename A>
            struct xref<const A> {
                template <typename U>
                using type = typename std::add_const<
                    typename xref<A>::template type<U>>::type;
            };

            template <typename A>
            struct xref<volatile A> {
                template <typename U>
                using type = typename std::add_volatile<
                    typename xref<A>::template type<U>>::type;
            };

            template <typename A>
            struct xref<const volatile A> {
                template <typename U>
                using type = typename std::add_cv<
                    typename xref<A>::template type<U>>::type;
            };

            template <typename T,
                      typename U,
                      template <class>
                      class TQual,
                      template <class>
                      class UQual>
            struct basic_common_reference {
            };

            template <typename...>
            struct get_common_reference;
            template <typename... Ts>
            using get_common_reference_t =
                typename get_common_reference<Ts...>::type;

            template <>
            struct get_common_reference<> {
            };
            template <typename T0>
            struct get_common_reference<T0> {
                using type = T0;
            };

            template <typename T, typename U>
            struct has_simple_common_ref
                : exists<simple_common_reference_t, T, U> {
            };
            template <typename T, typename U>
            using basic_common_ref_t = typename basic_common_reference<
                ::scn::detail::remove_cvref_t<T>,
                ::scn::detail::remove_cvref_t<U>,
                xref<T>::template type,
                xref<U>::template type>::type;

            template <typename T, typename U>
            struct has_basic_common_ref : exists<basic_common_ref_t, T, U> {
            };
            template <typename T, typename U>
            struct has_cond_res : exists<cond_res_t, T, U> {
            };

            template <typename T, typename U, typename = void>
            struct binary_common_ref : get_common_type<T, U> {
            };
            template <typename T, typename U>
            struct binary_common_ref<
                T,
                U,
                typename std::enable_if<
                    has_simple_common_ref<T, U>::value>::type>
                : simple_common_reference<T, U> {
            };
            template <typename T, typename U>
            struct binary_common_ref<
                T,
                U,
                typename std::enable_if<
                    has_basic_common_ref<T, U>::value &&
                    !has_simple_common_ref<T, U>::value>::type> {
                using type = basic_common_ref_t<T, U>;
            };
            template <typename T, typename U>
            struct binary_common_ref<
                T,
                U,
                typename std::enable_if<
                    has_cond_res<T, U>::value &&
                    !has_basic_common_ref<T, U>::value &&
                    !has_simple_common_ref<T, U>::value>::type> {
                using type = cond_res_t<T, U>;
            };
            template <typename T1, typename T2>
            struct get_common_reference<T1, T2> : binary_common_ref<T1, T2> {
            };

            template <typename Void, typename T1, typename T2, typename... Rest>
            struct multiple_common_reference {
            };
            template <typename T1, typename T2, typename... Rest>
            struct multiple_common_reference<
                void_t<get_common_reference_t<T1, T2>>,
                T1,
                T2,
                Rest...> : get_common_reference<get_common_reference_t<T1, T2>,
                                                Rest...> {
            };
            template <typename T1, typename T2, typename... Rest>
            struct get_common_reference<T1, T2, Rest...>
                : multiple_common_reference<void, T1, T2, Rest...> {
            };

            template <typename... Ts>
            using get_common_type_t = typename get_common_type<Ts...>::type;

            template <typename T, typename U>
            struct _same_decayed
                : std::integral_constant<
                      bool,
                      std::is_same<T, typename std::decay<T>::type>::value &&
                          std::is_same<U,
                                       typename std::decay<U>::type>::value> {
            };

            template <typename T, typename U>
            using ternary_return_t =
                typename std::decay<decltype(false ? SCN_DECLVAL(T)
                                                   : SCN_DECLVAL(U))>::type;

            template <typename, typename, typename = void>
            struct binary_common_type {
            };

            template <typename T, typename U>
            struct binary_common_type<
                T,
                U,
                typename std::enable_if<!_same_decayed<T, U>::value>::type>
                : get_common_type<typename std::decay<T>::type,
                                  typename std::decay<U>::type> {
            };

            template <typename T, typename U>
            struct binary_common_type<
                T,
                U,
                typename std::enable_if<
                    _same_decayed<T, U>::value &&
                    exists<ternary_return_t, T, U>::value>::type> {
                using type = ternary_return_t<T, U>;
            };

            template <typename T, typename U>
            struct binary_common_type<
                T,
                U,
                typename std::enable_if<
                    _same_decayed<T, U>::value &&
                    !exists<ternary_return_t, T, U>::value &&
                    exists<cond_res_t, cref_t<T>, cref_t<U>>::value>::type> {
                using type =
                    typename std::decay<cond_res_t<cref_t<T>, cref_t<U>>>::type;
            };

            template <>
            struct get_common_type<> {
            };

            template <typename T>
            struct get_common_type<T> : get_common_type<T, T> {
            };

            template <typename T, typename U>
            struct get_common_type<T, U> : binary_common_type<T, U> {
            };

            template <typename Void, typename...>
            struct multiple_common_type {
            };

            template <typename T1, typename T2, typename... R>
            struct multiple_common_type<void_t<get_common_type_t<T1, T2>>,
                                        T1,
                                        T2,
                                        R...>
                : get_common_type<get_common_type_t<T1, T2>, R...> {
            };

            template <typename T1, typename T2, typename... R>
            struct get_common_type<T1, T2, R...>
                : multiple_common_type<void, T1, T2, R...> {
            };
        }  // namespace detail
    }      // namespace custom_ranges

    SCN_END_NAMESPACE
}  // namespace scn

#endif  // SCN_RANGES_UTIL_H
