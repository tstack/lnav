/**
 * Copyright (c) 2022, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lnav_itertools_hh
#define lnav_itertools_hh

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <type_traits>
#include <vector>

#include "func_util.hh"

namespace lnav::itertools {

struct empty {};

struct not_empty {};

struct full {
    size_t f_max_size;
};

namespace details {

template<typename T>
struct unwrap_or {
    T uo_value;
};

template<typename P>
struct find_if {
    P fi_predicate;
};

template<typename T>
struct find {
    T f_value;
};

struct first {};

struct second {};

template<typename F>
struct filter_in {
    F f_func;
};

template<typename F>
struct filter_out {
    F f_func;
};

template<typename C>
struct sort_by {
    C sb_cmp;
};

struct sorted {};

template<typename F>
struct mapper {
    F m_func;
};

template<typename F>
struct flat_mapper {
    F fm_func;
};

template<typename F>
struct for_eacher {
    F fe_func;
};

template<typename R, typename T>
struct folder {
    R f_func;
    T f_init;
};

template<typename T>
struct prepend {
    T p_value;
};

template<typename T>
struct append {
    T p_value;
};

struct nth {
    std::optional<size_t> a_index;
};

struct skip {
    size_t a_count;
};

struct unique {};

struct max_value {};

template<typename T>
struct max_with_init {
    T m_init;
};

struct sum {};

struct to_vector {};

template<typename T, typename = void>
struct HasEmplaceBack : std::false_type {};

template<typename Type>
struct HasEmplaceBack<Type,
                      std::enable_if_t<std::is_member_function_pointer<
                          decltype(&Type::foo)>::emplace_back>>
    : std::true_type {};

template<typename T>
void
extend(T& accum)
{
}

template<typename T, typename E, typename... Args>
void
extend(T& accum, E& arg, Args&... args)
{
    if constexpr (HasEmplaceBack<T>::value) {
        for (const auto& elem : arg) {
            accum.emplace_back(elem);
        }
    } else {
        for (const auto& elem : arg) {
            accum.emplace(elem);
        }
    }
    details::extend(accum, args...);
}

}  // namespace details

inline details::to_vector
to_vector()
{
    return details::to_vector{};
}

template<typename T>
inline details::unwrap_or<T>
unwrap_or(T value)
{
    return details::unwrap_or<T>{
        value,
    };
}

template<typename P>
inline details::find_if<P>
find_if(P predicate)
{
    return details::find_if<P>{
        predicate,
    };
}

template<typename T>
inline details::find<T>
find(T value)
{
    return details::find<T>{
        value,
    };
}

inline details::first
first()
{
    return details::first{};
}

inline details::second
second()
{
    return details::second{};
}

inline details::nth
nth(std::optional<size_t> index)
{
    return details::nth{
        index,
    };
}

inline details::skip
skip(size_t count)
{
    return details::skip{
        count,
    };
}

template<typename F>
inline details::filter_in<F>
filter_in(F func)
{
    return details::filter_in<F>{
        func,
    };
}

template<typename F>
inline details::filter_out<F>
filter_out(F func)
{
    return details::filter_out<F>{
        func,
    };
}

template<typename T>
inline details::prepend<T>
prepend(T value)
{
    return details::prepend<T>{
        std::move(value),
    };
}

template<typename T>
inline details::append<T>
append(T value)
{
    return details::append<T>{
        std::move(value),
    };
}

template<typename C>
inline details::sort_by<C>
sort_with(C cmp)
{
    return details::sort_by<C>{cmp};
}

template<typename C, typename T>
inline auto
sort_by(T C::* m)
{
    return sort_with(
        [m](const C& lhs, const C& rhs) { return lhs.*m < rhs.*m; });
}

template<typename F>
inline details::mapper<F>
map(F func)
{
    return details::mapper<F>{func};
}

template<typename F>
inline details::flat_mapper<F>
flat_map(F func)
{
    return details::flat_mapper<F>{func};
}

template<typename F>
inline details::for_eacher<F>
for_each(F func)
{
    return details::for_eacher<F>{func};
}

inline auto
deref()
{
    return map([](auto iter) { return *iter; });
}

template<typename R, typename T>
inline details::folder<R, T>
fold(R func, T init)
{
    return details::folder<R, T>{func, init};
}

inline details::unique
unique()
{
    return details::unique{};
}

inline details::sorted
sorted()
{
    return details::sorted{};
}

template<typename T, typename... Args>
T
chain(const T& value1, const Args&... args)
{
    auto retval = value1;

    details::extend(retval, args...);

    return retval;
}

inline details::max_value
max()
{
    return details::max_value{};
}

template<typename T>
inline details::max_with_init<T>
max(T init)
{
    return details::max_with_init<T>{init};
}

inline details::sum
sum()
{
    return details::sum{};
}

}  // namespace lnav::itertools

template<typename C, typename P>
std::optional<std::conditional_t<
    std::is_const<typename std::remove_reference_t<C>>::value,
    typename std::remove_reference_t<C>::const_iterator,
    typename std::remove_reference_t<C>::iterator>>
operator|(C&& in, const lnav::itertools::details::find_if<P>& finder)
{
    for (auto iter = in.begin(); iter != in.end(); ++iter) {
        if (lnav::func::invoke(finder.fi_predicate, *iter)) {
            return std::make_optional(iter);
        }
    }

    return std::nullopt;
}

template<typename C, typename T>
std::optional<size_t>
operator|(const C& in, const lnav::itertools::details::find<T>& finder)
{
    size_t retval = 0;
    for (const auto& elem : in) {
        if (elem == finder.f_value) {
            return std::make_optional(retval);
        }
        retval += 1;
    }

    return std::nullopt;
}

template<typename C>
std::optional<typename C::const_iterator>
operator|(const C& in, const lnav::itertools::details::nth indexer)
{
    if (!indexer.a_index.has_value()) {
        return std::nullopt;
    }

    if (indexer.a_index.value() < in.size()) {
        auto iter = in.begin();

        std::advance(iter, indexer.a_index.value());
        return std::make_optional(iter);
    }

    return std::nullopt;
}

template<typename C>
std::vector<typename C::key_type>
operator|(const C& in, const lnav::itertools::details::first indexer)
{
    std::vector<typename C::key_type> retval;

    for (const auto& pair : in) {
        retval.emplace_back(pair.first);
    }

    return retval;
}

template<typename C>
std::optional<typename C::value_type>
operator|(const C& in, const lnav::itertools::details::max_value maxer)
{
    std::optional<typename C::value_type> retval;

    for (const auto& elem : in) {
        if (!retval) {
            retval = elem;
            continue;
        }

        if (elem > retval.value()) {
            retval = elem;
        }
    }

    return retval;
}

template<typename C, typename T>
typename C::value_type
operator|(const C& in, const lnav::itertools::details::max_with_init<T> maxer)
{
    typename C::value_type retval = (typename C::value_type) maxer.m_init;

    for (const auto& elem : in) {
        if (elem > retval) {
            retval = elem;
        }
    }

    return retval;
}

template<typename C>
typename C::value_type
operator|(const C& in, const lnav::itertools::details::sum summer)
{
    typename C::value_type retval{0};

    for (const auto& elem : in) {
        retval += elem;
    }

    return retval;
}

template<typename C>
C
operator|(const C& in, const lnav::itertools::details::skip& skipper)
{
    C retval;

    if (skipper.a_count < in.size()) {
        auto iter = in.begin();
        std::advance(iter, skipper.a_count);
        for (; iter != in.end(); ++iter) {
            retval.emplace_back(*iter);
        }
    }

    return retval;
}

template<typename T, typename F>
std::vector<T*>
operator|(const std::vector<std::unique_ptr<T>>& in,
          const lnav::itertools::details::filter_in<F>& filterer)
{
    std::vector<T*> retval;

    for (const auto& elem : in) {
        if (lnav::func::invoke(filterer.f_func, elem)) {
            retval.emplace_back(elem.get());
        }
    }

    return retval;
}

template<typename C, typename F>
C
operator|(const C& in, const lnav::itertools::details::filter_in<F>& filterer)
{
    C retval;

    for (const auto& elem : in) {
        if (lnav::func::invoke(filterer.f_func, elem)) {
            retval.emplace_back(elem);
        }
    }

    return retval;
}

template<typename C, typename F>
C
operator|(const C& in, const lnav::itertools::details::filter_out<F>& filterer)
{
    C retval;

    for (const auto& elem : in) {
        if (!lnav::func::invoke(filterer.f_func, elem)) {
            retval.emplace_back(elem);
        }
    }

    return retval;
}

template<typename C, typename T>
C
operator|(C in, const lnav::itertools::details::prepend<T>& prepender)
{
    in.emplace(in.begin(), prepender.p_value);

    return in;
}

template<typename C, typename T>
C
operator|(C in, const lnav::itertools::details::append<T>& appender)
{
    in.emplace_back(appender.p_value);

    return in;
}

template<typename C, typename R, typename T>
T
operator|(const C& in, const lnav::itertools::details::folder<R, T>& folder)
{
    auto accum = folder.f_init;

    for (const auto& elem : in) {
        accum = folder.f_func(elem, accum);
    }

    return accum;
}

template<typename C>
std::set<typename C::value_type>
operator|(C&& in, const lnav::itertools::details::unique& sorter)
{
    return {in.begin(), in.end()};
}

template<typename T, typename C>
T
operator|(T in, const lnav::itertools::details::sort_by<C>& sorter)
{
    std::sort(in.begin(), in.end(), sorter.sb_cmp);

    return in;
}

template<typename T>
T
operator|(T in, const lnav::itertools::details::sorted& sorter)
{
    std::sort(in.begin(), in.end());

    return in;
}

template<typename T,
         typename F,
         std::enable_if_t<lnav::func::is_invocable<F, T>::value, int> = 0>
auto
operator|(std::optional<T> in,
          const lnav::itertools::details::flat_mapper<F>& mapper) ->
    typename std::remove_const_t<typename std::remove_reference_t<
        decltype(lnav::func::invoke(mapper.fm_func, in.value()))>>
{
    if (!in) {
        return std::nullopt;
    }

    return lnav::func::invoke(mapper.fm_func, in.value());
}

template<typename T,
         typename F,
         std::enable_if_t<lnav::func::is_invocable<F, T>::value, int> = 0>
void
operator|(std::optional<T> in,
          const lnav::itertools::details::for_eacher<F>& eacher)
{
    if (!in) {
        return;
    }

    lnav::func::invoke(eacher.fe_func, in.value());
}

template<typename T,
         typename F,
         std::enable_if_t<lnav::func::is_invocable<F, T>::value, int> = 0>
void
operator|(const std::vector<std::shared_ptr<T>>& in,
          const lnav::itertools::details::for_eacher<F>& eacher)
{
    for (auto& elem : in) {
        lnav::func::invoke(eacher.fe_func, *elem);
    }
}

template<typename T,
         typename F,
         std::enable_if_t<lnav::func::is_invocable<F, T>::value, int> = 0>
void
operator|(const std::vector<T>& in,
          const lnav::itertools::details::for_eacher<F>& eacher)
{
    for (auto& elem : in) {
        lnav::func::invoke(eacher.fe_func, elem);
    }
}

template<typename T,
         typename F,
         std::enable_if_t<lnav::func::is_invocable<F, T>::value, int> = 0>
auto
operator|(std::optional<T> in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::optional<
        typename std::remove_const_t<typename std::remove_reference_t<
            decltype(lnav::func::invoke(mapper.m_func, in.value()))>>>
{
    if (!in) {
        return std::nullopt;
    }

    return std::make_optional(lnav::func::invoke(mapper.m_func, in.value()));
}

template<typename T, typename F>
auto
operator|(const std::set<T>& in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::set<std::remove_const_t<
        std::remove_reference_t<decltype(mapper.m_func(std::declval<T>()))>>>
{
    using return_type = std::set<std::remove_const_t<
        std::remove_reference_t<decltype(mapper.m_func(std::declval<T>()))>>>;
    return_type retval;

    std::transform(in.begin(),
                   in.end(),
                   std::inserter(retval, retval.begin()),
                   mapper.m_func);

    return retval;
}

template<typename T, typename F>
auto
operator|(const std::vector<T>& in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<std::remove_const_t<
        std::remove_reference_t<decltype(mapper.m_func(std::declval<T>()))>>>
{
    using return_type = std::vector<std::remove_const_t<
        std::remove_reference_t<decltype(mapper.m_func(std::declval<T>()))>>>;
    return_type retval;

    retval.reserve(in.size());
    std::transform(
        in.begin(), in.end(), std::back_inserter(retval), mapper.m_func);

    return retval;
}

template<typename T, typename F>
auto
operator|(const std::deque<T>& in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<std::remove_const_t<
        std::remove_reference_t<decltype(mapper.m_func(std::declval<T>()))>>>
{
    using return_type = std::vector<std::remove_const_t<
        std::remove_reference_t<decltype(mapper.m_func(std::declval<T>()))>>>;
    return_type retval;

    retval.reserve(in.size());
    std::transform(
        in.begin(), in.end(), std::back_inserter(retval), mapper.m_func);

    return retval;
}

template<typename K, typename V, typename F>
auto
operator|(const std::map<K, V>& in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<
        std::remove_const_t<std::remove_reference_t<decltype(mapper.m_func(
            std::declval<typename std::map<K, V>::value_type>()))>>>
{
    using return_type = std::vector<
        std::remove_const_t<std::remove_reference_t<decltype(mapper.m_func(
            std::declval<typename std::map<K, V>::value_type>()))>>>;
    return_type retval;

    retval.reserve(in.size());
    std::transform(
        in.begin(), in.end(), std::back_inserter(retval), mapper.m_func);

    return retval;
}

template<typename T, typename F>
auto
operator|(const T& in, const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<std::remove_const_t<
        std::remove_reference_t<decltype(((*in.begin()).*mapper.m_func)())>>>
{
    using return_type = std::vector<std::remove_const_t<
        std::remove_reference_t<decltype(((*in.begin()).*mapper.m_func)())>>>;
    return_type retval;

    retval.reserve(in.size());
    for (const auto& elem : in) {
        retval.emplace_back((elem.*mapper.m_func)());
    }

    return retval;
}

template<typename T, typename F>
auto
operator|(const std::vector<T>& in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<typename std::remove_const_t<std::remove_reference_t<
        decltype((*(std::declval<T>()).*mapper.m_func)())>>>
{
    using return_type
        = std::vector<typename std::remove_const_t<std::remove_reference_t<
            decltype((*(std::declval<T>()).*mapper.m_func)())>>>;
    return_type retval;

    retval.reserve(in.size());
    std::transform(
        in.begin(),
        in.end(),
        std::back_inserter(retval),
        [&mapper](const auto& elem) { return ((*elem).*mapper.m_func)(); });

    return retval;
}

template<typename T, typename F>
auto
operator|(const std::set<T>& in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<typename std::remove_const_t<std::remove_reference_t<
        decltype((*(std::declval<T>()).*mapper.m_func)())>>>
{
    using return_type
        = std::vector<typename std::remove_const_t<std::remove_reference_t<
            decltype((*(std::declval<T>()).*mapper.m_func)())>>>;
    return_type retval;

    retval.reserve(in.size());
    std::transform(
        in.begin(),
        in.end(),
        std::back_inserter(retval),
        [&mapper](const auto& elem) { return ((*elem).*mapper.m_func)(); });

    return retval;
}

template<typename T,
         typename F,
         std::enable_if_t<!lnav::func::is_invocable<F, T>::value, int> = 0>
auto
operator|(const std::vector<std::shared_ptr<T>>& in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<typename std::remove_reference_t<
        typename std::remove_const_t<decltype(((*in.front()).*mapper.m_func))>>>
{
    using return_type = std::vector<
        typename std::remove_const_t<typename std::remove_reference_t<decltype((
            (*in.front()).*mapper.m_func))>>>;
    return_type retval;

    retval.reserve(in.size());
    for (const auto& elem : in) {
        retval.emplace_back(((*elem).*mapper.m_func));
    }

    return retval;
}

template<typename T,
         typename F,
         std::enable_if_t<!lnav::func::is_invocable<F, T>::value, int> = 0>
auto
operator|(const std::vector<T>& in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<
        typename std::remove_const_t<typename std::remove_reference_t<
            decltype(((in.front()).*mapper.m_func))>>>
{
    using return_type = std::vector<
        typename std::remove_const_t<typename std::remove_reference_t<decltype((
            (in.front()).*mapper.m_func))>>>;
    return_type retval;

    retval.reserve(in.size());
    for (const auto& elem : in) {
        retval.emplace_back(elem.*mapper.m_func);
    }

    return retval;
}

template<typename T,
         typename F,
         std::enable_if_t<!lnav::func::is_invocable<F, T>::value, int> = 0>
auto
operator|(std::optional<T> in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::optional<typename std::remove_reference_t<
        typename std::remove_const_t<decltype(((in.value()).*mapper.m_func))>>>
{
    if (!in) {
        return std::nullopt;
    }

    return std::make_optional((in.value()).*mapper.m_func);
}

template<typename T,
         typename F,
         std::enable_if_t<!lnav::func::is_invocable<F, T>::value, int> = 0>
auto
operator|(std::optional<T> in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::optional<
        typename std::remove_const_t<typename std::remove_reference_t<
            decltype(((*in.value()).*mapper.m_func))>>>
{
    if (!in) {
        return std::nullopt;
    }

    return std::make_optional((*in.value()).*mapper.m_func);
}

template<typename T>
T
operator|(std::optional<T> in,
          const lnav::itertools::details::unwrap_or<T>& unwrapper)
{
    return in.value_or(unwrapper.uo_value);
}

template<typename T>
std::vector<T>
operator|(std::set<T>&& in, lnav::itertools::details::to_vector tv)
{
    std::vector<T> retval;

    retval.reserve(in.size());
    std::copy(in.begin(), in.end(), std::back_inserter(retval));

    return retval;
}

#endif
