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

#include <scn/scan.h>

// experimental

namespace scn {
SCN_BEGIN_NAMESPACE

namespace detail {
SCN_GCC_PUSH
SCN_GCC_IGNORE("-Wctor-dtor-privacy")
template <typename T>
class is_std_string_like {
    template <typename U>
    static auto check(U* p)
        -> decltype((void)p->find('a'), p->length(), (void)p->data(), int());
    template <typename>
    static void check(...);

public:
    static constexpr bool value = std::is_convertible_v<T, std::string_view> ||
                                  !std::is_void_v<decltype(check<T>(nullptr))>;
};
SCN_GCC_POP

template <typename T, typename = void>
struct is_map : std::false_type {};
template <typename T>
struct is_map<T, std::void_t<typename T::mapped_type>> : std::true_type {};

template <typename T, typename = void>
struct is_set : std::false_type {};
template <typename T>
struct is_set<
    T,
    std::void_t<std::enable_if_t<!is_map<T>::value>, typename T::key_type>>
    : std::true_type {};

template <typename T, typename = void>
struct is_tuple_like_impl : std::false_type {};
template <typename T>
struct is_tuple_like_impl<T, std::void_t<decltype(std::tuple_size<T>::value)>>
    : std::true_type {};

// TODO
template <typename T, typename CharT, bool = is_tuple_like_impl<T>::value>
struct is_tuple_scannable_impl : std::false_type {};

SCN_GCC_PUSH
SCN_GCC_IGNORE("-Wctor-dtor-privacy")
template <typename T, typename CharT>
struct is_tuple_scannable_impl<T, CharT, true> {
private:
    template <std::size_t... Is>
    static std::true_type check_impl(
        std::index_sequence<Is...>,
        std::integer_sequence<bool, (Is == Is)...>);
    static std::false_type check_impl(...);

    template <std::size_t... Is>
    static auto check(std::index_sequence<Is...>) -> decltype(check_impl(
        std::index_sequence<Is...>{},
        std::integer_sequence<
            bool,
            (is_scannable<std::tuple_element_t<Is, T>, CharT>::value)...>{}));

public:
    static constexpr bool value = decltype(check(
        std::make_index_sequence<std::tuple_size_v<T>>{}))::value;
};
SCN_GCC_POP

template <typename CharT>
struct range_mapper {
    using mapper = arg_mapper<CharT>;

    template <typename T,
              std::enable_if_t<is_scannable<T, CharT>::value>* = nullptr>
    static auto map(T& value) -> T&
    {
        return value;
    }
    template <typename T,
              std::enable_if_t<!is_scannable<T, CharT>::value>* = nullptr>
    static auto map(T& value) -> decltype(mapper().map(value))
    {
        return mapper().map(value);
    }
};

template <typename CharT, typename ElementT>
using range_scanner_type = scanner<
    remove_cvref_t<decltype(range_mapper<CharT>().map(SCN_DECLVAL(ElementT&)))>,
    CharT>;

template <typename T>
struct is_tuple_like
    : std::integral_constant<bool,
                             detail::is_tuple_like_impl<T>::value &&
                                 !ranges::range<T>> {};

template <typename T, typename CharT>
struct is_tuple_scannable : detail::is_tuple_scannable_impl<T, CharT> {};

template <typename Tuple, typename F, std::size_t... Is>
void tuple_for_each(std::index_sequence<Is...>, Tuple&& tuple, F&& f)
{
    using std::get;
    SCN_CLANG_PUSH
    SCN_CLANG_IGNORE("-Wcomma")
    const int ignored[] = {0, (static_cast<void>(f(get<Is>(tuple))), 0)...};
    SCN_CLANG_POP
    SCN_UNUSED(ignored);
}
template <typename Tuple, typename F>
void tuple_for_each(Tuple&& tuple, F&& f)
{
    tuple_for_each(
        std::make_index_sequence<std::tuple_size_v<remove_cvref_t<Tuple>>>{},
        SCN_FWD(tuple), SCN_FWD(f));
}

template <typename T, typename CharT>
struct is_range
    : std::integral_constant<
          bool,
          ranges::range<T> && !detail::is_std_string_like<T>::value &&
              !std::is_convertible_v<T, std::basic_string<CharT>> &&
              !std::is_convertible_v<T, std::basic_string_view<CharT>>> {};

template <typename Source, typename CharT>
scan_expected<ranges::iterator_t<Source>> scan_str(
    Source source,
    std::basic_string_view<CharT> str_to_read)
{
    SCN_TRY(it, internal_skip_classic_whitespace(source, false));
    for (auto ch_to_read : str_to_read) {
        if (SCN_UNLIKELY(ch_to_read != *it)) {
            return unexpected_scan_error(scan_error::invalid_scanned_value,
                                         "Invalid range character");
        }
        ++it;
    }
    return it;
}

template <typename Range, typename Element, typename Enable = void>
struct has_push_back : std::false_type {};
template <typename Range, typename Element>
struct has_push_back<Range,
                     Element,
                     decltype(SCN_DECLVAL(Range&).push_back(
                         SCN_DECLVAL(Element&&)))> : std::true_type {};

template <typename Range, typename Element, typename Enable = void>
struct has_push : std::false_type {};
template <typename Range, typename Element>
struct has_push<Range,
                Element,
                decltype(SCN_DECLVAL(Range&).push(SCN_DECLVAL(Element&&)))>
    : std::true_type {};

template <typename Range, typename Element, typename Enable = void>
struct has_element_insert : std::false_type {};
template <typename Range, typename Element>
struct has_element_insert<
    Range,
    Element,
    std::void_t<decltype(SCN_DECLVAL(Range&).insert(SCN_DECLVAL(Element&&)))>>
    : std::true_type {};

template <typename Range,
          typename Element,
          typename = std::enable_if_t<!std::is_reference_v<Element>>>
void add_element_to_range(Range& r, Element&& elem)
{
    using elem_type = std::remove_const_t<Element>;
    if constexpr (has_push_back<Range, elem_type>::value) {
        r.push_back(SCN_MOVE(elem));
    }
    else if constexpr (has_push<Range, elem_type>::value) {
        r.push(SCN_MOVE(elem));
    }
    else if constexpr (has_element_insert<Range, elem_type>::value) {
        r.insert(SCN_MOVE(elem));
    }
    else {
        static_assert(dependent_false<Range>::value, "Invalid range type");
    }
}

template <typename Range, typename Enable = void>
struct has_max_size : std::false_type {};
template <typename Range>
struct has_max_size<Range, decltype(SCN_DECLVAL(const Range&).max_size())>
    : std::true_type {};

template <typename Range, typename DiffT = ranges::range_difference_t<Range>>
DiffT range_max_size(const Range& r)
{
    if constexpr (has_max_size<Range>::value) {
        return static_cast<DiffT>(r.max_size());
    }
    else {
        return std::numeric_limits<DiffT>::max();
    }
}

template <typename CharT>
class range_scanner_base {
public:
    constexpr void set_separator(std::basic_string_view<CharT> sep)
    {
        m_separator = sep;
    }
    constexpr void set_brackets(std::basic_string_view<CharT> open,
                                std::basic_string_view<CharT> close)
    {
        m_opening_bracket = open;
        m_closing_bracket = close;
    }

protected:
    constexpr range_scanner_base() = default;

    std::basic_string_view<CharT> m_separator{","};
    std::basic_string_view<CharT> m_opening_bracket{"["};
    std::basic_string_view<CharT> m_closing_bracket{"]"};
};

template <typename CharT>
class range_scanner_base_for_ranges : public range_scanner_base<CharT> {
protected:
    template <typename T, typename Scan, typename Range, typename Context>
    scan_expected<typename Context::iterator> scan_impl(Scan scan_cb,
                                                        Range& range,
                                                        Context& ctx) const
    {
        SCN_TRY(it, detail::scan_str(ctx.range(), this->m_opening_bracket));
        ctx.advance_to(it);

        using diff_type = ranges::range_difference_t<Range>;
        for (diff_type i = 0; i < detail::range_max_size(range); ++i) {
            if (auto e = detail::scan_str(ctx.range(), this->m_closing_bracket);
                e) {
                break;
            }

            T elem{};
            if (auto e = scan_inner_loop(scan_cb, ctx, elem, i == 0);
                SCN_LIKELY(e)) {
                detail::add_element_to_range(range, SCN_MOVE(elem));
                ctx.advance_to(*e);
            }
            else {
                return e;
            }
        }

        return detail::scan_str(ctx.range(), this->m_closing_bracket);
    }

private:
    template <typename Scan, typename Context, typename Elem>
    scan_expected<typename Context::iterator>
    scan_inner_loop(Scan scan_cb, Context& ctx, Elem& elem, bool is_first) const
    {
        auto skip_separator =
            [](auto src, auto sep,
               bool f) -> scan_expected<typename Context::iterator> {
            if (f) {
                return src.begin();
            }
            return detail::scan_str(src, sep);
        };

        SCN_TRY(it, skip_separator(ctx.range(), this->m_separator, is_first));
        ctx.advance_to(it);
        return scan_cb(detail::range_mapper<CharT>().map(elem), ctx, is_first);
    }
};

template <typename T>
struct is_std_pair : std::false_type {};
template <typename First, typename Second>
struct is_std_pair<std::pair<First, Second>> : std::true_type {};
}  // namespace detail

template <typename Tuple, typename CharT>
struct scanner<
    Tuple,
    CharT,
    std::enable_if_t<detail::is_tuple_like<Tuple>::value &&
                     detail::is_tuple_scannable<Tuple, CharT>::value>>
    : public detail::range_scanner_base<CharT> {
public:
    constexpr scanner()
    {
        this->set_separator(",");
        this->set_brackets("(", ")");
    }

    template <typename ParseCtx>
    constexpr typename ParseCtx::iterator parse(ParseCtx& pctx)
    {
        return pctx.begin();
    }

    template <typename Context>
    scan_expected<typename Context::iterator> scan(Tuple& value,
                                                   Context& ctx) const
    {
        SCN_TRY(it, detail::scan_str(ctx.range(), this->m_opening_bracket));
        ctx.advance_to(it);
        SCN_TRY_DISCARD(scan_for_each(value, ctx));
        return detail::scan_str(ctx.range(), this->m_closing_bracket);
    }

private:
    template <typename Context>
    scan_expected<typename Context::iterator> scan_for_each(Tuple& value,
                                                            Context& ctx) const
    {
        scan_expected<void> err{};
        int index{};

        auto callback = [&](auto& val) {
            if (SCN_UNLIKELY(!err)) {
                return;
            }

            if (index > 0) {
                if (auto e = detail::scan_str(ctx.range(), this->m_separator);
                    SCN_LIKELY(e)) {
                    ctx.advance_to(e.value());
                }
                else {
                    err = unexpected(e.error());
                    return;
                }
            }

            scanner<detail::remove_cvref_t<decltype(val)>, CharT> s{};
            if (auto e = s.scan(val, ctx); SCN_LIKELY(e)) {
                ctx.advance_to(e.value());
                ++index;
            }
            else {
                err = unexpected(e.error());
            }
        };

        detail::tuple_for_each(value, callback);
        if (SCN_UNLIKELY(!err)) {
            return unexpected(err.error());
        }
        return ctx.range().begin();
    }
};

template <typename T, typename CharT, typename Enable = void>
class range_scanner;

template <typename T, typename CharT>
class range_scanner<T,
                    CharT,
                    std::enable_if_t<std::conjunction_v<
                        std::is_same<T, detail::remove_cvref_t<T>>,
                        detail::is_scannable<T, CharT>>>>
    : public detail::range_scanner_base_for_ranges<CharT> {
public:
    constexpr range_scanner() = default;

    constexpr detail::range_scanner_type<CharT, T>& base()
    {
        return m_underlying;
    }

    template <typename ParseCtx>
    constexpr typename ParseCtx::iterator parse(ParseCtx& pctx)
    {
        // TODO
        return m_underlying.parse(pctx);
    }

    template <typename Range, typename Context>
    scan_expected<typename Context::iterator> scan(Range& range,
                                                   Context& ctx) const
    {
        return this->template scan_impl<T>(
            [&](T& v, Context& c, bool) { return m_underlying.scan(v, c); },
            range, ctx);
    }

private:
    detail::range_scanner_type<CharT, T> m_underlying;
};

enum class range_format {
    disabled,
    map,
    set,
    sequence,
    string,
    // debug_string
};

namespace detail {
template <typename T>
struct default_range_format_kind
    : std::integral_constant<range_format,
                             std::is_same_v<ranges::range_reference_t<T>, T>
                                 ? range_format::disabled
                                 : (is_map<T>::value
                                        ? range_format::map
                                        : (is_set<T>::value
                                               ? range_format::set
                                               : range_format::sequence))> {};

template <typename T, typename Enable = void>
struct range_value_type_for_scanner_processor {
    using type = T;
};
template <typename First, typename Second>
struct range_value_type_for_scanner_processor<std::pair<First, Second>> {
    using type = std::pair<std::remove_cv_t<First>, std::remove_cv_t<Second>>;
};

template <typename T>
using range_value_type_for_scanner =
    typename range_value_type_for_scanner_processor<detail::char_t<T>>::type;

template <range_format Kind,
          typename Range,
          typename CharT,
          typename Enable = void>
class range_default_scanner;

template <range_format Kind, typename Range, typename CharT>
class range_default_scanner<Kind,
                            Range,
                            CharT,
                            std::enable_if_t<(Kind == range_format::sequence ||
                                              Kind == range_format::map ||
                                              Kind == range_format::set)>> {
public:
    constexpr range_default_scanner()
    {
        init();
    }

    template <typename ParseCtx>
    constexpr typename ParseCtx::iterator parse(ParseCtx& pctx)
    {
        return m_underlying.parse(pctx);
    }

    template <typename Context>
    scan_expected<typename Context::iterator> scan(Range& range,
                                                   Context& ctx) const
    {
        return m_underlying.scan(range, ctx);
    }

private:
    constexpr void init()
    {
        if constexpr (Kind == range_format::set) {
            m_underlying.set_brackets("{", "}");
        }
        else if constexpr (Kind == range_format::map) {
            m_underlying.set_brackets("{", "}");
            m_underlying.base().set_brackets({}, {});
            m_underlying.base().set_separator(":");
        }
    }

    range_scanner<range_value_type_for_scanner<Range>, CharT> m_underlying;
};
}  // namespace detail

template <typename T, typename CharT, typename Enable = void>
struct range_format_kind
    : std::conditional_t<
          detail::is_range<T, CharT>::value,
          detail::default_range_format_kind<T>,
          std::integral_constant<range_format, range_format::disabled>> {};

template <typename Range, typename CharT>
struct scanner<Range,
               CharT,
               std::enable_if_t<range_format_kind<Range, CharT>::value !=
                                range_format::disabled>>
    : detail::range_default_scanner<range_format_kind<Range, CharT>::value,
                                    Range,
                                    CharT> {};

SCN_END_NAMESPACE
}  // namespace scn
