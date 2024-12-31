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

#include <chrono>
#include <ctime>

#if SCN_DISABLE_CHRONO
#error "scn/chrono.h included, but SCN_DISABLE_CHRONO is true"
#endif

namespace scn {
SCN_BEGIN_NAMESPACE

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L

using weekday = std::chrono::weekday;
using day = std::chrono::day;
using month = std::chrono::month;
using year = std::chrono::year;

using month_day = std::chrono::month_day;
using year_month = std::chrono::year_month;
using year_month_day = std::chrono::year_month_day;

using std::chrono::Friday;
using std::chrono::Monday;
using std::chrono::Saturday;
using std::chrono::Sunday;
using std::chrono::Thursday;
using std::chrono::Tuesday;
using std::chrono::Wednesday;

using std::chrono::April;
using std::chrono::August;
using std::chrono::December;
using std::chrono::February;
using std::chrono::January;
using std::chrono::July;
using std::chrono::June;
using std::chrono::March;
using std::chrono::May;
using std::chrono::November;
using std::chrono::October;
using std::chrono::September;

#else

// Fallbacks

struct weekday {
    weekday() = default;
    constexpr explicit weekday(unsigned wd) noexcept
        : m_value(static_cast<unsigned char>(wd != 7 ? wd : 0))
    {
    }
    constexpr unsigned c_encoding() const noexcept
    {
        return m_value;
    }

    friend constexpr bool operator==(const weekday& lhs, const weekday& rhs)
    {
        return lhs.m_value == rhs.m_value;
    }

private:
    unsigned char m_value;
};

inline constexpr weekday Sunday{0};
inline constexpr weekday Monday{1};
inline constexpr weekday Tuesday{2};
inline constexpr weekday Wednesday{3};
inline constexpr weekday Thursday{4};
inline constexpr weekday Friday{5};
inline constexpr weekday Saturday{6};

struct day {
    day() = default;
    constexpr explicit day(unsigned d) noexcept
        : m_value(static_cast<unsigned char>(d))
    {
    }
    constexpr explicit operator unsigned() const noexcept
    {
        return m_value;
    }

    friend constexpr bool operator==(const day& lhs, const day& rhs)
    {
        return lhs.m_value == rhs.m_value;
    }

private:
    unsigned char m_value;
};

struct month {
    month() = default;
    constexpr explicit month(unsigned d) noexcept
        : m_value(static_cast<unsigned char>(d))
    {
    }
    constexpr explicit operator unsigned() const noexcept
    {
        return m_value;
    }

    friend constexpr bool operator==(const month& lhs, const month& rhs)
    {
        return lhs.m_value == rhs.m_value;
    }

private:
    unsigned char m_value;
};

inline constexpr month January{1};
inline constexpr month February{2};
inline constexpr month March{3};
inline constexpr month April{4};
inline constexpr month May{5};
inline constexpr month June{6};
inline constexpr month July{7};
inline constexpr month August{8};
inline constexpr month September{9};
inline constexpr month October{10};
inline constexpr month November{11};
inline constexpr month December{12};

struct year {
    year() = default;
    constexpr explicit year(int d) noexcept : m_value(d) {}
    constexpr explicit operator int() const noexcept
    {
        return m_value;
    }

    friend constexpr bool operator==(const year& lhs, const year& rhs)
    {
        return lhs.m_value == rhs.m_value;
    }

private:
    int m_value;
};

struct month_day {
    month_day() = default;
    constexpr month_day(const scn::month& m, const scn::day& d) noexcept
        : m_month(m), m_day(d)
    {
    }
    constexpr scn::month month() const noexcept
    {
        return m_month;
    }
    constexpr scn::day day() const noexcept
    {
        return m_day;
    }

    friend constexpr bool operator==(const month_day& lhs, const month_day& rhs)
    {
        return lhs.m_month == rhs.m_month && lhs.m_day == rhs.m_day;
    }

private:
    scn::month m_month;
    scn::day m_day;
};

struct year_month {
    year_month() = default;
    constexpr year_month(const scn::year& y, const scn::month& m) noexcept
        : m_year(y), m_month(m)
    {
    }
    constexpr scn::year year() const noexcept
    {
        return m_year;
    }
    constexpr scn::month month() const noexcept
    {
        return m_month;
    }

    friend constexpr bool operator==(const year_month& lhs,
                                     const year_month& rhs)
    {
        return lhs.m_year == rhs.m_year && lhs.m_month == rhs.m_month;
    }

private:
    scn::year m_year;
    scn::month m_month;
};

struct year_month_day {
    year_month_day() = default;
    constexpr year_month_day(const scn::year& y,
                             const scn::month& m,
                             const scn::day& d) noexcept
        : m_year(y), m_month(m), m_day(d)
    {
    }
    constexpr scn::year year() const noexcept
    {
        return m_year;
    }
    constexpr scn::month month() const noexcept
    {
        return m_month;
    }
    constexpr scn::day day() const noexcept
    {
        return m_day;
    }

    friend constexpr bool operator==(const year_month_day& lhs,
                                     const year_month_day& rhs)
    {
        return lhs.m_year == rhs.m_year && lhs.m_month == rhs.m_month &&
               lhs.m_day == rhs.m_day;
    }

private:
    scn::year m_year;
    scn::month m_month;
    scn::day m_day;
};

#endif  // __cpp_lib_chrono >= 201907

namespace detail {
template <typename T>
using has_tm_gmtoff_predicate = decltype(T::tm_gmtoff);

template <typename T>
void assign_gmtoff(T& tm, std::chrono::seconds val)
{
    static_assert(std::is_same_v<T, std::tm>);
    if constexpr (mp_valid<has_tm_gmtoff_predicate, T>::value) {
        tm.tm_gmtoff = val.count();
    }
    else {
        SCN_UNUSED(tm);
        SCN_UNUSED(val);
        SCN_EXPECT(false);
        SCN_UNREACHABLE;
    }
}
}  // namespace detail

/**
 * An alternative to `std::tm`,
 * with support for subsecond precision, and
 * clear distinction between the value `0` and an unset field.
 */
struct datetime_components {
    /// Fractions of a second, [0.0-1.0)
    std::optional<double> subsec;
    /// Seconds, [0-60]
    std::optional<signed char> sec;
    /// Minutes, [0-59]
    std::optional<signed char> min;
    /// Hours, [0-23]
    std::optional<signed char> hour;
    /// Day (in a month), [1-31]
    std::optional<signed char> mday;
    /// Month (strongly typed)
    std::optional<month> mon;
    /// Year, without an offset
    /// Note, `std::tm` stores years since 1900
    std::optional<int> year;
    /// Day of week (strongly typed)
    std::optional<weekday> wday;
    /// Day of year (offset from Jan 1st), [0-365]
    std::optional<short> yday;
    /// Timezone offset from UTC
    std::optional<std::chrono::minutes> tz_offset;
    /// Timezone name
    std::optional<std::string> tz_name;

    /**
     * Returns a `std::tm` object corresponding to `*this`.
     * Unset fields have a value of `0`, with `tm_isdst` having a value of `-1`.
     * The fields `subsec` and `tz_name` are discarded.
     * `tz_offset` is set to `tm_gmtoff`, if it's available.
     */
    [[nodiscard]] std::tm to_tm() const
    {
        SCN_UNUSED(subsec);
        std::tm t{
            static_cast<int>(sec.value_or(0)),
            static_cast<int>(min.value_or(0)),
            static_cast<int>(hour.value_or(0)),
            static_cast<int>(mday.value_or(0)),
            static_cast<int>(static_cast<unsigned>(mon.value_or(January)) - 1),
            static_cast<int>(year.value_or(1900) - 1900),
            static_cast<int>(wday.value_or(Sunday).c_encoding()),
            static_cast<int>(yday.value_or(0)),
            -1,
        };
        if constexpr (detail::mp_valid<detail::has_tm_gmtoff_predicate,
                                       std::tm>::value) {
            detail::assign_gmtoff(
                t, std::chrono::duration_cast<std::chrono::seconds>(
                       tz_offset.value_or(std::chrono::minutes{0})));
        }
        return t;
    }
};

struct tm_with_tz : public std::tm {
    std::optional<std::chrono::minutes> tz_offset;
    std::optional<std::string> tz_name;
};

namespace detail {
enum class numeric_system {
    standard,
    alternative_e,  // 'E'
    alternative_o,  // 'O'
};

template <typename Duration>
std::optional<Duration> time_since_unix_epoch(const datetime_components& dt)
{
    static_assert(std::is_integral_v<typename Duration::rep>);
    static_assert(Duration::period::type::den <= std::nano::den);

    if (dt.tz_offset || dt.tz_name || dt.wday || dt.yday) {
        return std::nullopt;
    }

    auto tm = dt.to_tm();
    // TODO: overflow checks
    auto time = std::chrono::seconds{std::mktime(&tm)};
    if constexpr (Duration::period::type::den > std::intmax_t{1}) {
        // Duration more precise than seconds (seconds is std::ratio<1, 1>)
        if (dt.subsec) {
            auto subsec_in_ns =
                std::chrono::nanoseconds{static_cast<std::int64_t>(
                    *dt.subsec * static_cast<double>(std::nano::den))};
            return std::chrono::duration_cast<Duration>(time + subsec_in_ns);
        }
    }
    // Duration is seconds or larger, or subsec is not set
    // -> ignore subsec
    return std::chrono::duration_cast<Duration>(time);
}

template <typename CharT, typename Handler>
constexpr const CharT* parse_chrono_format_specs(const CharT* begin,
                                                 const CharT* end,
                                                 Handler&& handler)
{
    if (begin == end || *begin == CharT{'}'}) {
        handler.on_error("chrono format specs can't be empty");
        return begin;
    }

    auto p = begin;
    if (*p == CharT{'L'}) {
        handler.on_localized();
        begin = ++p;
        if (p == end) {
            handler.on_error("chrono format specs can't be empty");
            return p;
        }
    }

    if (p == end || *p != CharT{'%'}) {
        handler.on_error(
            "chrono format spec must start with a conversion specifier (%...)");
        return p;
    }

    while (p != end) {
        auto ch = *p;
        if (ch == CharT{'}'}) {
            break;
        }
        if (ch != CharT{'%'}) {
            if (ch == 0x20 || (ch >= 0x09 && ch <= 0x0d)) {
                // Simple ASCII space
                handler.on_text(begin, p);
                handler.on_whitespace();
                begin = p += 1;
                continue;
            }
            const auto cp_len =
                detail::code_point_length_by_starting_code_unit(ch);
            if (cp_len == 0) {
                handler.on_error("Invalid literal character");
                return p;
            }
            if (cp_len > 1) {
                // Multi code unit code point,
                // possible space
                auto cp_start_p = p;
                CharT buffer[4] = {};
                for (std::size_t i = 0; i < cp_len; ++i) {
                    if (p == end) {
                        handler.on_error("Invalid literal character");
                        return p;
                    }
                    buffer[i] = *p;
                    ++p;
                }
                if (is_cp_space(decode_code_point_exhaustive(
                        std::basic_string_view<CharT>{buffer, cp_len}))) {
                    handler.on_text(begin, cp_start_p);
                    handler.on_whitespace();
                    begin = p;
                    continue;
                }
            }
            ++p;
            continue;
        }
        if (begin != p) {
            handler.on_text(begin, p);
        }
        ++p;  // Consume '%'
        if (p == end) {
            handler.on_error("Unexpected end of chrono format string");
            return p;
        }

        ch = *p;
        ++p;

        switch (ch) {
            case CharT{'%'}:
                handler.on_text(&ch, &ch + 1);
                break;
            case CharT{'n'}:
            case CharT{'t'}:
                handler.on_whitespace();
                break;
            // Year
            case CharT{'Y'}:
                handler.on_full_year();
                break;
            case CharT{'y'}:
                handler.on_short_year();
                break;
            case CharT{'C'}:
                handler.on_century();
                break;
            case CharT{'G'}:
                handler.on_iso_week_based_year();
                break;
            case CharT{'g'}:
                handler.on_iso_week_based_short_year();
                break;
            // Month
            case CharT{'b'}:
            case CharT{'B'}:
            case CharT{'h'}:
                handler.on_month_name();
                break;
            case CharT{'m'}:
                handler.on_dec_month();
                break;
            // Week
            case CharT{'U'}:
                handler.on_dec0_week_of_year();
                break;
            case CharT{'W'}:
                handler.on_dec1_week_of_year();
                break;
            case CharT{'V'}:
                handler.on_iso_week_of_year();
                break;
            // Day of year
            case CharT{'j'}:
                handler.on_day_of_year();
                break;
            // Day of month
            case CharT{'d'}:
            case CharT{'e'}:
                handler.on_day_of_month();
                break;
            // Day of week
            case CharT{'a'}:
            case CharT{'A'}:
                handler.on_weekday_name();
                break;
            case CharT{'w'}:
                handler.on_dec0_weekday();
                break;
            case CharT{'u'}:
                handler.on_dec1_weekday();
                break;
            // Hour
            case CharT{'H'}:
            case CharT{'k'}:
                handler.on_24_hour();
                break;
            case CharT{'I'}:
            case CharT{'l'}:
                handler.on_12_hour();
                break;
            // Minute
            case CharT{'M'}:
                handler.on_minute();
                break;
            // Second
            case CharT{'S'}:
                handler.on_second();
                break;
            // Subsecond
            case CharT{'.'}: {
                // p already increased before the switch
                if (p == end) {
                    handler.on_error("Unexpected end of chrono format string");
                    return p;
                }
                bool use_alternate = false;
                if (*p == CharT{'E'} || *p == CharT{'O'}) {
                    ++p;
                    if (p == end) {
                        handler.on_error(
                            "Unexpected end of chrono format string");
                        return p;
                    }
                    use_alternate = true;
                }
                if (*p != CharT{'S'}) {
                    handler.on_error(
                        "Expected `S` after `%.` in format string");
                    return p;
                }
                ++p;
                handler.on_subsecond(use_alternate
                                         ? numeric_system::alternative_e
                                         : numeric_system::standard);
                break;
            }
            // Timezones
            case CharT{'z'}:
                handler.on_tz_offset();
                break;
            case CharT{'Z'}:
                handler.on_tz_name();
                break;
            // Other
            case CharT{'c'}:
                handler.on_loc_datetime();
                break;
            case CharT{'x'}:
                handler.on_loc_date();
                break;
            case CharT{'X'}:
                handler.on_loc_time();
                break;
            case CharT{'D'}:
                handler.on_us_date();
                break;
            case CharT{'F'}:
                handler.on_iso_date();
                break;
            case CharT{'r'}:
                handler.on_loc_12_hour_time();
                break;
            case CharT{'R'}:
                handler.on_24_hour_time();
                break;
            case CharT{'T'}:
                handler.on_iso_time();
                break;
            case CharT{'p'}:
            case CharT{'P'}:
                handler.on_am_pm();
                break;
            case CharT{'s'}:
                handler.on_epoch_offset();
                break;
            case CharT{'Q'}:
                handler.on_duration_tick_count();
                break;
            case CharT{'q'}:
                handler.on_duration_suffix();
                break;
            // 'E'
            case CharT{'E'}: {
                if (p == end) {
                    handler.on_error("Unexpected end of chrono format string");
                    return p;
                }
                ch = *p;
                ++p;

                switch (ch) {
                    case CharT{'c'}:
                        handler.on_loc_datetime(numeric_system::alternative_e);
                        break;
                    case CharT{'C'}:
                        handler.on_century(numeric_system::alternative_e);
                        break;
                    case CharT{'x'}:
                        handler.on_loc_date(numeric_system::alternative_e);
                        break;
                    case CharT{'X'}:
                        handler.on_loc_time(numeric_system::alternative_e);
                        break;
                    case CharT{'y'}:
                        handler.on_loc_offset_year();
                        break;
                    case CharT{'Y'}:
                        handler.on_full_year(numeric_system::alternative_e);
                        break;
                    case CharT{'z'}:
                        handler.on_tz_offset(numeric_system::alternative_e);
                        break;
                    default:
                        handler.on_error(
                            "Invalid character following 'E' in chrono format "
                            "string");
                        return p;
                }
                break;
            }
            // 'O'
            case CharT{'O'}: {
                if (p == end) {
                    handler.on_error("Unexpected end of chrono format string");
                    return p;
                }
                ch = *p;
                ++p;

                switch (ch) {
                    case CharT{'d'}:
                    case CharT{'e'}:
                        handler.on_day_of_month(numeric_system::alternative_o);
                        break;
                    case CharT{'H'}:
                    case CharT{'k'}:
                        handler.on_24_hour(numeric_system::alternative_o);
                        break;
                    case CharT{'I'}:
                    case CharT{'l'}:
                        handler.on_12_hour(numeric_system::alternative_o);
                        break;
                    case CharT{'m'}:
                        handler.on_dec_month(numeric_system::alternative_o);
                        break;
                    case CharT{'M'}:
                        handler.on_minute(numeric_system::alternative_o);
                        break;
                    case CharT{'S'}:
                        handler.on_second(numeric_system::alternative_o);
                        break;
                    case CharT{'U'}:
                        handler.on_dec0_week_of_year(
                            numeric_system::alternative_o);
                        break;
                    case CharT{'w'}:
                        handler.on_dec0_weekday(numeric_system::alternative_o);
                        break;
                    case CharT{'W'}:
                        handler.on_dec1_weekday(numeric_system::alternative_o);
                        break;
                    case CharT{'y'}:
                        handler.on_short_year(numeric_system::alternative_o);
                        break;
                    case CharT{'z'}:
                        handler.on_tz_offset(numeric_system::alternative_o);
                        break;
                    default:
                        handler.on_error(
                            "Invalid character following 'O' in chrono format "
                            "string");
                        return p;
                }
                break;
            }
            default:
                handler.on_error("Invalid character in chrono format string");
                return p;
        }
        begin = p;

        if (!handler.get_error()) {
            return p;
        }
    }
    if (begin != p) {
        handler.on_text(begin, p);
    }
    handler.verify();
    return p;
}

struct setter_state {
    unsigned localized : 1;
    unsigned subsec_set : 1;
    unsigned sec_set : 1;
    unsigned min_set : 1;
    unsigned hour24_set : 1;
    unsigned hour12_set : 1;
    unsigned mday_set : 1;
    unsigned mon_set : 1;
    unsigned full_year_set : 1;
    unsigned century_set : 1;
    unsigned short_year_set : 1;
    unsigned wday_set : 1;
    unsigned yday_set : 1;
    unsigned tzoff_set : 1;
    unsigned tzname_set : 1;
    unsigned am_pm_set : 1;
    unsigned epoch_ticks_set : 1;
    unsigned duration_ticks_set : 1;
    unsigned duration_suffix_set : 1;
    unsigned is_pm : 1;
    unsigned char short_year_value{0};
    unsigned char century_value{0};

    constexpr setter_state()
        : localized(0),
          subsec_set(0),
          sec_set(0),
          min_set(0),
          hour24_set(0),
          hour12_set(0),
          mday_set(0),
          mon_set(0),
          full_year_set(0),
          century_set(0),
          short_year_set(0),
          wday_set(0),
          yday_set(0),
          tzoff_set(0),
          tzname_set(0),
          am_pm_set(0),
          epoch_ticks_set(0),
          duration_ticks_set(0),
          duration_suffix_set(0),
          is_pm(0)
    {
    }

#define SCN_CHRONO_SETTER_STATE_SET(field)                           \
    template <typename Handler>                                      \
    constexpr void set_##field(Handler& handler)                     \
    {                                                                \
        if (field##_set) {                                           \
            handler.set_error({scan_error::invalid_format_string,    \
                               #field "-field set multiple times"}); \
        }                                                            \
        field##_set = 1;                                             \
    }

    SCN_CHRONO_SETTER_STATE_SET(subsec)
    SCN_CHRONO_SETTER_STATE_SET(sec)
    SCN_CHRONO_SETTER_STATE_SET(min)
    SCN_CHRONO_SETTER_STATE_SET(hour24)
    SCN_CHRONO_SETTER_STATE_SET(hour12)
    SCN_CHRONO_SETTER_STATE_SET(mday)
    SCN_CHRONO_SETTER_STATE_SET(mon)
    SCN_CHRONO_SETTER_STATE_SET(full_year)
    SCN_CHRONO_SETTER_STATE_SET(century)
    SCN_CHRONO_SETTER_STATE_SET(short_year)
    SCN_CHRONO_SETTER_STATE_SET(wday)
    SCN_CHRONO_SETTER_STATE_SET(yday)
    SCN_CHRONO_SETTER_STATE_SET(tzoff)
    SCN_CHRONO_SETTER_STATE_SET(tzname)
    SCN_CHRONO_SETTER_STATE_SET(am_pm)
    SCN_CHRONO_SETTER_STATE_SET(epoch_ticks)
    SCN_CHRONO_SETTER_STATE_SET(duration_ticks)
    SCN_CHRONO_SETTER_STATE_SET(duration_suffix)

#undef SCN_CHRONO_SETTER_STATE_SET

    template <typename Handler>
    constexpr void verify(Handler& handler) const
    {
        if (hour24_set && hour12_set) {
            return handler.set_error(
                {scan_error::invalid_format_string,
                 "24-hour and 12-hour clocks can't both be in use "
                 "simultaneously"});
        }
        if (am_pm_set) {
            if (!hour12_set) {
                return handler.set_error(
                    {scan_error::invalid_format_string,
                     "AM/PM specifier can't be set without an hour set"});
            }
            if (hour24_set) {
                return handler.set_error(
                    {scan_error::invalid_format_string,
                     "Can't use AM/PM with a 24-hour clock"});
            }
        }
        if (full_year_set && (century_set || short_year_set)) {
            return handler.set_error(
                {scan_error::invalid_format_string,
                 "full-year (%Y) can't be used together with "
                 "century (%C) and short-year (%y)"});
        }
        if (tzoff_set && tzname_set) {
            return handler.set_error(
                {scan_error::invalid_format_string,
                 "tzoff (%z) can't be used together with tzname (%Z)"});
        }
        if ((wday_set || mday_set || yday_set) &&
            !(wday_set ^ mday_set ^ yday_set)) {
            return handler.set_error(
                {scan_error::invalid_format_string,
                 "Only up to one of wday (%a/%u/%w), mday "
                 "(%d/%e), and yday (%j) can be set at once"});
        }
    }

    template <typename Hour>
    constexpr void handle_am_pm(Hour& hour)
    {
        assert(hour12_set);
        assert(hour <= 12);
        if (is_pm) {
            // PM
            if (hour == 12) {
                // 12:xx PM -> 12:xx, no-op
            }
            else {
                hour += 12;
            }
        }
        else {
            // AM
            if (hour == 12) {
                // 12:xx AM -> 00:xx
                hour = 0;
            }
            else {
                // no-op
            }
        }
    }

    template <typename Year>
    constexpr void handle_short_year_and_century(Year& year, Year offset)
    {
        assert(!full_year_set);
        if (short_year_set && century_set) {
            year = century_value * 100 + short_year_value - offset;
        }
        else if (short_year_set) {
            if (short_year_value >= 69) {
                year = 1900 + short_year_value - offset;
            }
            else {
                year = 2000 + short_year_value - offset;
            }
        }
        else if (century_set) {
            year = 100 * century_value - offset;
        }
    }
};

namespace field_tags {

struct subsec {};
struct sec {};
struct min {};
struct hour {};
struct mday {};
struct mon {};
struct year {};
struct wday {};
struct yday {};
struct tzoff {};
struct tzname {};
struct duration {};

}  // namespace field_tags

template <typename T, typename Field>
struct always_supports_field;

template <typename Field>
struct always_supports_field<std::tm, Field> : std::true_type {};
template <>
struct always_supports_field<std::tm, field_tags::subsec> : std::false_type {};
template <>
struct always_supports_field<std::tm, field_tags::tzoff>
    : mp_valid<has_tm_gmtoff_predicate, std::tm> {};
template <>
struct always_supports_field<std::tm, field_tags::tzname> : std::false_type {};

template <typename Field>
struct always_supports_field<tm_with_tz, Field>
    : always_supports_field<std::tm, Field> {};
template <>
struct always_supports_field<tm_with_tz, field_tags::tzoff> : std::true_type {};
template <>
struct always_supports_field<tm_with_tz, field_tags::tzname> : std::true_type {
};

template <typename Field>
struct always_supports_field<datetime_components, Field> : std::true_type {};

template <typename Field>
struct always_supports_field<weekday, Field> : std::false_type {};
template <>
struct always_supports_field<weekday, field_tags::wday> : std::true_type {};

template <typename Field>
struct always_supports_field<day, Field> : std::false_type {};
template <>
struct always_supports_field<day, field_tags::mday> : std::true_type {};

template <typename Field>
struct always_supports_field<month, Field> : std::false_type {};
template <>
struct always_supports_field<month, field_tags::mon> : std::true_type {};

template <typename Field>
struct always_supports_field<year, Field> : std::false_type {};
template <>
struct always_supports_field<year, field_tags::year> : std::true_type {};

template <typename Field>
struct always_supports_field<year_month, Field> : std::false_type {};
template <>
struct always_supports_field<year_month, field_tags::year> : std::true_type {};
template <>
struct always_supports_field<year_month, field_tags::mon> : std::true_type {};

template <typename Field>
struct always_supports_field<month_day, Field> : std::false_type {};
template <>
struct always_supports_field<month_day, field_tags::mon> : std::true_type {};
template <>
struct always_supports_field<month_day, field_tags::mday> : std::true_type {};

template <typename Field>
struct always_supports_field<year_month_day, Field> : std::false_type {};
template <>
struct always_supports_field<year_month_day, field_tags::year>
    : std::true_type {};
template <>
struct always_supports_field<year_month_day, field_tags::mon> : std::true_type {
};
template <>
struct always_supports_field<year_month_day, field_tags::mday>
    : std::true_type {};

template <typename T>
struct always_supports_field<T, field_tags::duration> : std::false_type {};

template <typename T, typename Field>
struct always_requires_field : std::false_type {};

template <>
struct always_requires_field<weekday, field_tags::wday> : std::true_type {};
template <>
struct always_requires_field<day, field_tags::mday> : std::true_type {};
template <>
struct always_requires_field<month, field_tags::mon> : std::true_type {};
template <>
struct always_requires_field<year, field_tags::year> : std::true_type {};
template <>
struct always_requires_field<year_month, field_tags::year> : std::true_type {};
template <>
struct always_requires_field<year_month, field_tags::mon> : std::true_type {};
template <>
struct always_requires_field<month_day, field_tags::mon> : std::true_type {};
template <>
struct always_requires_field<month_day, field_tags::mday> : std::true_type {};
template <>
struct always_requires_field<year_month_day, field_tags::year>
    : std::true_type {};
template <>
struct always_requires_field<year_month_day, field_tags::mon> : std::true_type {
};
template <>
struct always_requires_field<year_month_day, field_tags::mday>
    : std::true_type {};

template <typename Derived>
struct null_chrono_spec_handler {
    constexpr void unsupported()
    {
        static_cast<Derived*>(this)->unsupported();
    }

    constexpr void on_localized()
    {
        unsupported();
    }

    constexpr void on_full_year(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_short_year(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_century(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_iso_week_based_year()
    {
        unsupported();
    }
    constexpr void on_iso_week_based_short_year()
    {
        unsupported();
    }
    constexpr void on_loc_offset_year()
    {
        unsupported();
    }

    constexpr void on_month_name()
    {
        unsupported();
    }
    constexpr void on_dec_month(numeric_system = numeric_system::standard)
    {
        unsupported();
    }

    constexpr void on_dec0_week_of_year(
        numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_dec1_week_of_year()
    {
        unsupported();
    }
    constexpr void on_iso_week_of_year()
    {
        unsupported();
    }
    constexpr void on_day_of_year()
    {
        unsupported();
    }
    constexpr void on_day_of_month(numeric_system = numeric_system::standard)
    {
        unsupported();
    }

    constexpr void on_weekday_name()
    {
        unsupported();
    }
    constexpr void on_dec0_weekday(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_dec1_weekday(numeric_system = numeric_system::standard)
    {
        unsupported();
    }

    constexpr void on_24_hour(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_12_hour(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_minute(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_second(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_subsecond(numeric_system = numeric_system::standard)
    {
        unsupported();
    }

    constexpr void on_tz_offset(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_tz_name()
    {
        unsupported();
    }

    constexpr void on_loc_datetime(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_loc_date(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_loc_time(numeric_system = numeric_system::standard)
    {
        unsupported();
    }
    constexpr void on_us_date()
    {
        unsupported();
    }
    constexpr void on_iso_date()
    {
        unsupported();
    }
    constexpr void on_loc_12_hour_time()
    {
        unsupported();
    }
    constexpr void on_24_hour_time()
    {
        unsupported();
    }
    constexpr void on_iso_time()
    {
        unsupported();
    }
    constexpr void on_am_pm()
    {
        unsupported();
    }

    constexpr void on_epoch_offset()
    {
        unsupported();
    }
    constexpr void on_duration_tick_count()
    {
        unsupported();
    }
    constexpr void on_duration_suffix()
    {
        unsupported();
    }

    constexpr void verify()
    {
        unsupported();
    }
};

template <typename T>
struct tm_format_checker {
    template <typename CharT>
    constexpr void on_text(const CharT*, const CharT*)
    {
    }
    constexpr void on_whitespace() {}

    constexpr void on_localized()
    {
        if constexpr (SCN_DISABLE_LOCALE) {
            on_error("'L' flag invalid when SCN_DISABLE_LOCALE is on");
        }
    }

    constexpr void on_full_year(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::year>::value) {
            on_error("Years not supported with this type");
        }
        st.set_full_year(*this);
    }
    constexpr void on_short_year(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::year>::value) {
            on_error("Years not supported with this type");
        }
        st.set_short_year(*this);
    }
    constexpr void on_century(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::year>::value) {
            on_error("Years not supported with this type");
        }
        st.set_century(*this);
    }
    constexpr void on_iso_week_based_year()
    {
        if (!always_supports_field<T, field_tags::year>::value) {
            on_error("Years not supported with this type");
        }
        unimplemented();
    }
    constexpr void on_iso_week_based_short_year()
    {
        if (!always_supports_field<T, field_tags::year>::value) {
            on_error("Years not supported with this type");
        }
        unimplemented();
    }
    constexpr void on_loc_offset_year()
    {
        if (!always_supports_field<T, field_tags::year>::value) {
            on_error("Years not supported with this type");
        }
        unimplemented();
    }

    constexpr void on_month_name()
    {
        if (!always_supports_field<T, field_tags::mon>::value) {
            on_error("Months not supported with this type");
        }
    }
    constexpr void on_dec_month(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::mon>::value) {
            on_error("Months not supported with this type");
        }
        st.set_mon(*this);
    }

    constexpr void on_dec0_week_of_year(
        numeric_system = numeric_system::standard)
    {
        unimplemented();
    }
    constexpr void on_dec1_week_of_year()
    {
        unimplemented();
    }
    constexpr void on_iso_week_of_year()
    {
        unimplemented();
    }
    constexpr void on_day_of_year()
    {
        if (!always_supports_field<T, field_tags::yday>::value) {
            on_error("Day-of-year not supported with this type");
        }
        st.set_yday(*this);
    }
    constexpr void on_day_of_month(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::mday>::value) {
            on_error("Day-of-month not supported with this type");
        }
        st.set_mday(*this);
    }

    constexpr void on_weekday_name()
    {
        if (!always_supports_field<T, field_tags::wday>::value) {
            on_error("Day-of-week not supported with this type");
        }
        st.set_wday(*this);
    }
    constexpr void on_dec0_weekday(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::wday>::value) {
            on_error("Day-of-week not supported with this type");
        }
        st.set_wday(*this);
    }
    constexpr void on_dec1_weekday(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::wday>::value) {
            on_error("Day-of-week not supported with this type");
        }
        st.set_wday(*this);
    }

    constexpr void on_24_hour(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::hour>::value) {
            on_error("Hours not supported with this type");
        }
        st.set_hour24(*this);
    }
    constexpr void on_12_hour(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::hour>::value) {
            on_error("Hours not supported with this type");
        }
        st.set_hour12(*this);
    }
    constexpr void on_minute(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::min>::value) {
            on_error("Minutes not supported with this type");
        }
        st.set_min(*this);
    }
    constexpr void on_second(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::sec>::value) {
            on_error("Seconds not supported with this type");
        }
        st.set_sec(*this);
    }
    constexpr void on_subsecond(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::subsec>::value) {
            on_error("Sub-seconds not supported with this type");
        }
        if constexpr (SCN_DISABLE_TYPE_STRING || SCN_DISABLE_TYPE_DOUBLE) {
            on_error(
                "Support for strings and doubles required for sub-seconds");
        }
        st.set_subsec(*this);
    }

    constexpr void on_tz_offset(numeric_system = numeric_system::standard)
    {
        if (!always_supports_field<T, field_tags::tzoff>::value) {
            on_error("Timezone offsets not supported with this type");
        }
        st.set_tzoff(*this);
    }
    constexpr void on_tz_name()
    {
        if (!always_supports_field<T, field_tags::tzname>::value) {
            on_error("Timezone names not supported with this type");
        }
        st.set_tzname(*this);
    }

    constexpr void on_loc_datetime(
        numeric_system sys = numeric_system::standard)
    {
        on_loc_date(sys);
        on_loc_time(sys);
    }
    constexpr void on_loc_date(numeric_system sys = numeric_system::standard)
    {
        on_full_year(sys);
        on_dec_month(sys);
        on_day_of_month(sys);
    }
    constexpr void on_loc_time(numeric_system sys = numeric_system::standard)
    {
        on_24_hour(sys);
        on_minute(sys);
        on_second(sys);
    }
    constexpr void on_us_date()
    {
        on_dec_month();
        on_day_of_month();
        on_short_year();
    }
    constexpr void on_iso_date()
    {
        on_full_year();
        on_dec_month();
        on_day_of_month();
    }
    constexpr void on_loc_12_hour_time()
    {
        on_24_hour();
        on_minute();
        on_second();
    }
    constexpr void on_24_hour_time()
    {
        on_24_hour();
        on_minute();
    }
    constexpr void on_iso_time()
    {
        on_24_hour();
        on_minute();
        on_second();
    }
    constexpr void on_am_pm()
    {
        if (!always_supports_field<T, field_tags::hour>::value) {
            on_error("AM/PM not supported with this type");
        }
        st.set_am_pm(*this);
    }

    constexpr void on_epoch_offset()
    {
        unimplemented();
    }
    constexpr void on_duration_tick_count()
    {
        unimplemented();
    }
    constexpr void on_duration_suffix()
    {
        unimplemented();
    }

    constexpr void verify()
    {
        if (always_requires_field<T, field_tags::subsec>::value &&
            !st.subsec_set) {
            on_error("Sub-seconds not set by the format string");
        }
        if (always_requires_field<T, field_tags::sec>::value && !st.sec_set) {
            on_error("Seconds not set by the format string");
        }
        if (always_requires_field<T, field_tags::min>::value && !st.min_set) {
            on_error("Minutes not set by the format string");
        }
        if (always_requires_field<T, field_tags::hour>::value &&
            !st.hour24_set && !st.hour12_set) {
            on_error("Hours not set by the format string");
        }
        if (always_requires_field<T, field_tags::mday>::value && !st.mday_set) {
            on_error("Day not set by the format string");
        }
        if (always_requires_field<T, field_tags::mon>::value && !st.mon_set) {
            on_error("Month not set by the format string");
        }
        if (always_requires_field<T, field_tags::year>::value &&
            !st.full_year_set && !st.century_set && !st.short_year_set) {
            on_error("Year not set by the format string");
        }
        if (always_requires_field<T, field_tags::wday>::value && !st.wday_set) {
            on_error("Day-of-week not set by the format string");
        }
        if (always_requires_field<T, field_tags::yday>::value && !st.yday_set) {
            on_error("Day-of-year not set by the format string");
        }
        if (always_requires_field<T, field_tags::tzoff>::value &&
            !st.tzoff_set) {
            on_error("Timezone offset not set by the format string");
        }
        if (always_requires_field<T, field_tags::tzname>::value &&
            !st.tzname_set) {
            on_error("Timezone name not set by the format string");
        }
        if (always_requires_field<T, field_tags::duration>::value &&
            !st.duration_ticks_set) {
            on_error("Duration tick count not set by the format string");
        }

        st.verify(*this);
    }

    [[nodiscard]] constexpr scan_expected<void> get_error() const
    {
        return err;
    }
    void on_error(const char* msg)
    {
        set_error({scan_error::invalid_format_string, msg});
    }
    void set_error(scan_error e)
    {
        if (err.has_value()) {
            err = unexpected(detail::handle_error(e));
        }
    }

    void unimplemented()
    {
        on_error("Unimplemented");
    }

    scan_expected<void> err{};
    setter_state st{};
};

template <typename T, typename CharT, typename ParseCtx>
constexpr auto chrono_parse_impl(ParseCtx& pctx,
                                 std::basic_string_view<CharT>& fmt_str) ->
    typename ParseCtx::iterator
{
    auto it = pctx.begin();
    auto end = pctx.end();
    if (it == end || *it == CharT{'}'}) {
        pctx.on_error(
            "Format string without specifiers is not valid for this type");
        return it;
    }

    auto checker = detail::tm_format_checker<T>{};
    end = detail::parse_chrono_format_specs(it, end, checker);
    if (end != it) {
        fmt_str = detail::make_string_view_from_pointers(it, end);
    }
    if (auto e = checker.get_error(); SCN_UNLIKELY(!e)) {
        assert(e.error().code() == scan_error::invalid_format_string);
        pctx.on_error(e.error().msg());
    }
    return end;
}

template <typename CharT, typename T, typename Context>
auto chrono_scan_impl(std::basic_string_view<CharT> fmt_str, T& t, Context& ctx)
    -> scan_expected<typename Context::iterator>;

template <typename CharT, typename T>
struct chrono_datetime_scanner {
    template <typename ParseCtx>
    constexpr auto parse(ParseCtx& pctx) -> typename ParseCtx::iterator
    {
        return detail::chrono_parse_impl<T, CharT>(pctx, m_fmt_str);
    }

    template <typename Context>
    auto scan(T& t, Context& ctx) const
        -> scan_expected<typename Context::iterator>
    {
        return detail::chrono_scan_impl(m_fmt_str, t, ctx);
    }

protected:
    std::basic_string_view<CharT> m_fmt_str{};
};

}  // namespace detail

template <typename CharT>
struct scanner<std::tm, CharT>
    : public detail::chrono_datetime_scanner<CharT, std::tm> {};

template <typename CharT>
struct scanner<tm_with_tz, CharT>
    : public detail::chrono_datetime_scanner<CharT, tm_with_tz> {};

template <typename CharT>
struct scanner<datetime_components, CharT>
    : public detail::chrono_datetime_scanner<CharT, datetime_components> {};

namespace detail {

template <typename CharT>
struct chrono_component_scanner
    : protected chrono_datetime_scanner<CharT, datetime_components> {
private:
    using base = chrono_datetime_scanner<CharT, datetime_components>;

public:
    template <typename ParseCtx>
    constexpr typename ParseCtx::iterator parse(ParseCtx& pctx)
    {
        if (pctx.begin() == pctx.end() || *pctx.begin() == CharT{'}'}) {
            pctx.on_error("Default format not supported for this type");
        }
        return base::parse(pctx);
    }
};

}  // namespace detail

template <typename CharT>
struct scanner<weekday, CharT>
    : public detail::chrono_component_scanner<CharT> {
    template <typename Context>
    scan_expected<typename Context::iterator> scan(weekday& wd,
                                                   Context& ctx) const
    {
        datetime_components dt{};
        auto r = detail::chrono_component_scanner<CharT>::scan(dt, ctx);
        if (!r) {
            return unexpected(r.error());
        }
        assert(dt.wday);
        wd = *dt.wday;
        return *r;
    }
};

template <typename CharT>
struct scanner<day, CharT> : public detail::chrono_component_scanner<CharT> {
    template <typename Context>
    scan_expected<typename Context::iterator> scan(day& d, Context& ctx) const
    {
        datetime_components dt{};
        auto r = detail::chrono_component_scanner<CharT>::scan(dt, ctx);
        if (!r) {
            return unexpected(r.error());
        }
        assert(dt.mday);
        d = day{static_cast<unsigned>(*dt.mday)};
        return *r;
    }
};

template <typename CharT>
struct scanner<month, CharT> : public detail::chrono_component_scanner<CharT> {
    template <typename Context>
    scan_expected<typename Context::iterator> scan(month& m, Context& ctx) const
    {
        datetime_components dt{};
        auto r = detail::chrono_component_scanner<CharT>::scan(dt, ctx);
        if (!r) {
            return unexpected(r.error());
        }
        assert(dt.mon);
        m = *dt.mon;
        return *r;
    }
};

template <typename CharT>
struct scanner<year, CharT> : public detail::chrono_component_scanner<CharT> {
    template <typename Context>
    scan_expected<typename Context::iterator> scan(year& y, Context& ctx) const
    {
        datetime_components dt{};
        auto r = detail::chrono_component_scanner<CharT>::scan(dt, ctx);
        if (!r) {
            return unexpected(r.error());
        }
        assert(dt.year);
        y = year{static_cast<int>(*dt.year)};
        return *r;
    }
};

template <typename CharT>
struct scanner<month_day, CharT>
    : public detail::chrono_component_scanner<CharT> {
    template <typename Context>
    scan_expected<typename Context::iterator> scan(month_day& md,
                                                   Context& ctx) const
    {
        datetime_components dt{};
        auto r = detail::chrono_component_scanner<CharT>::scan(dt, ctx);
        if (!r) {
            return unexpected(r.error());
        }
        assert(dt.mon);
        assert(dt.mday);
        md = month_day{month{static_cast<unsigned>(*dt.mon)},
                       day{static_cast<unsigned>(*dt.mday)}};
        return *r;
    }
};

template <typename CharT>
struct scanner<year_month, CharT>
    : public detail::chrono_component_scanner<CharT> {
    template <typename Context>
    scan_expected<typename Context::iterator> scan(year_month& ym,
                                                   Context& ctx) const
    {
        datetime_components dt{};
        auto r = detail::chrono_component_scanner<CharT>::scan(dt, ctx);
        if (!r) {
            return unexpected(r.error());
        }
        assert(dt.year);
        assert(dt.mon);
        ym = year_month{year{static_cast<int>(*dt.year)},
                        month{static_cast<unsigned>(*dt.mon)}};
        return *r;
    }
};

template <typename CharT>
struct scanner<year_month_day, CharT>
    : public detail::chrono_component_scanner<CharT> {
    template <typename Context>
    scan_expected<typename Context::iterator> scan(year_month_day& ymd,
                                                   Context& ctx) const
    {
        datetime_components dt{};
        auto r = detail::chrono_component_scanner<CharT>::scan(dt, ctx);
        if (!r) {
            return unexpected(r.error());
        }
        assert(dt.year);
        assert(dt.mon);
        assert(dt.mday);
        ymd = year_month_day{year{static_cast<int>(*dt.year)},
                             month{static_cast<unsigned>(*dt.mon)},
                             day{static_cast<unsigned>(*dt.mday)}};
        return *r;
    }
};

template <typename CharT, typename Duration>
struct scanner<std::chrono::time_point<std::chrono::system_clock, Duration>,
               CharT> : public detail::chrono_component_scanner<CharT> {
    using time_point_type =
        std::chrono::time_point<std::chrono::system_clock, Duration>;

    template <typename Context>
    scan_expected<typename Context::iterator> scan(time_point_type& tp,
                                                   Context& ctx) const
    {
        datetime_components dt{};
        auto r = detail::chrono_component_scanner<CharT>::scan(dt, ctx);
        if (!r) {
            return unexpected(r.error());
        }
        if (auto tp_value = detail::time_since_unix_epoch<Duration>(dt)) {
            tp = time_point_type{*tp_value};
        }
        else {
            return unexpected(scan_error{scan_error::invalid_scanned_value,
                                         "Invalid unix epoch"});
        }
        return *r;
    }
};

SCN_END_NAMESPACE
}  // namespace scn
