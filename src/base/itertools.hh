
#ifndef lnav_itertools_hh
#define lnav_itertools_hh

#include <algorithm>
#include <type_traits>
#include <vector>

#include "func_util.hh"
#include "optional.hpp"

namespace lnav {
namespace itertools {

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

}  // namespace details

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
sort_by(T C::*m)
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

template<typename R, typename T>
inline details::folder<R, T>
fold(R func, T init)
{
    return details::folder<R, T>{func, init};
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
    T retval;

    for (const auto& arg : {value1, args...}) {
        for (const auto& elem : arg) {
            retval.emplace_back(elem);
        }
    }

    return retval;
}

}  // namespace itertools
}  // namespace lnav

template<typename C, typename P>
nonstd::optional<typename C::value_type>
operator|(const C& in, const lnav::itertools::details::find_if<P>& finder)
{
    for (const auto& elem : in) {
        if (lnav::func::invoke(finder.fi_predicate, elem)) {
            return nonstd::make_optional(elem);
        }
    }

    return nonstd::nullopt;
}

template<typename C, typename T>
nonstd::optional<size_t>
operator|(const C& in, const lnav::itertools::details::find<T>& finder)
{
    size_t retval = 0;
    for (const auto& elem : in) {
        if (elem == finder.f_value) {
            return nonstd::make_optional(retval);
        }
        retval += 1;
    }

    return nonstd::nullopt;
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

template<typename T, typename F>
auto
operator|(const T& in, const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<decltype(mapper.m_func(typename T::value_type{}))>
{
    using return_type
        = std::vector<decltype(mapper.m_func(typename T::value_type{}))>;
    return_type retval;

    retval.reserve(in.size());
    std::transform(
        in.begin(), in.end(), std::back_inserter(retval), mapper.m_func);

    return retval;
}

template<typename T, typename F>
auto
operator|(const std::vector<std::shared_ptr<T>>& in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<typename std::remove_const_t<decltype(((*in.front())
                                                          .*mapper.m_func)())>>
{
    using return_type = std::vector<typename std::remove_const_t<decltype((
        (*in.front()).*mapper.m_func)())>>;
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
operator|(const std::vector<std::shared_ptr<T>>& in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<typename std::remove_reference_t<
        typename std::remove_const_t<decltype(((*in.front()).*mapper.m_func))>>>
{
    using return_type = std::vector<
        typename std::remove_reference_t<typename std::remove_const_t<decltype((
            (*in.front()).*mapper.m_func))>>>;
    return_type retval;

    retval.reserve(in.size());
    for (const auto& elem : in) {
        retval.template emplace_back(((*elem).*mapper.m_func));
    }

    return retval;
}

template<typename T, typename F>
auto
operator|(nonstd::optional<T> in,
          const lnav::itertools::details::mapper<F>& mapper)
    -> nonstd::optional<typename std::remove_reference_t<
        typename std::remove_const_t<decltype(((in.value()).*mapper.m_func))>>>
{
    if (!in) {
        return nonstd::nullopt;
    }

    return nonstd::make_optional((in.value()).*mapper.m_func);
}

template<typename T>
T
operator|(nonstd::optional<T> in,
          const lnav::itertools::details::unwrap_or<T>& unwrapper)
{
    return in.value_or(unwrapper.uo_value);
}

template<typename T, typename F>
auto
operator|(const T& in, const lnav::itertools::details::mapper<F>& mapper)
    -> std::vector<std::remove_const_t<decltype(((typename T::value_type{})
                                                 .*mapper.m_func)())>>
{
    using return_type = std::vector<std::remove_const_t<decltype((
        (typename T::value_type{}).*mapper.m_func)())>>;
    return_type retval;

    retval.reserve(in.size());
    for (const auto& elem : in) {
        retval.template emplace_back((elem.*mapper.m_func)());
    }

    return retval;
}

#endif
