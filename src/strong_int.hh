/**
 * Copyright (c) 2007-2012, Timothy Stack
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
 *
 * @file strong_int.hh
 */

#ifndef strong_int_hh
#define strong_int_hh

#include <limits>
#include <type_traits>

/**
 * Template class for "strongly-typed" integers, in other words, integers that
 * have different semantic meaning and cannot be easily used in place of one
 * another.
 *
 * The arithmetic operators saturate: a result that does not fit in T is
 * clamped to T's minimum or maximum instead of wrapping.  Only operations
 * between two strong_ints are covered; mixing in a plain integer goes
 * through the implicit conversion to T and the built-in operators.
 *
 * @param T The integer type.
 * @param DISTINCT An class used solely to distinguish templates that have the
 * same integer type.
 */
template<typename T, class DISTINCT>
class strong_int {
    static_assert(std::is_integral_v<T>);

public:
    explicit constexpr strong_int(T v = 0) noexcept : value(v) {}
    /** From another integer type, clamped to T's range. */
    template<typename U,
             std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, T>
                                  && !std::is_same_v<U, bool>,
                              int>
             = 0>
    explicit constexpr strong_int(U v) noexcept : value(sat_cast(v))
    {
    }
    operator const T&() const { return this->value; }
    constexpr strong_int operator+(const strong_int& rhs) const
    {
        return strong_int(sat_add(this->value, rhs.value));
    }
    constexpr strong_int operator-(const strong_int& rhs) const
    {
        return strong_int(sat_sub(this->value, rhs.value));
    }
    constexpr strong_int operator/(const strong_int& rhs) const
    {
        return strong_int(sat_div(this->value, rhs.value));
    }
    constexpr strong_int operator*(const strong_int& rhs) const
    {
        return strong_int(sat_mul(this->value, rhs.value));
    }
    constexpr bool operator<(const strong_int& rhs) const
    {
        return this->value < rhs.value;
    }
    strong_int& operator+=(const strong_int& rhs)
    {
        this->value = sat_add(this->value, rhs.value);
        return *this;
    }
    strong_int& operator-=(const strong_int& rhs)
    {
        this->value = sat_sub(this->value, rhs.value);
        return *this;
    }
    constexpr strong_int operator-() const
    {
        return strong_int(sat_sub(T{0}, this->value));
    }
    strong_int& operator++()
    {
        this->value = sat_add(this->value, T{1});
        return *this;
    }
    strong_int& operator--()
    {
        this->value = sat_sub(this->value, T{1});
        return *this;
    }
    constexpr bool operator==(const strong_int& rhs) const
    {
        return this->value == rhs.value;
    }
    T* out() { return &this->value; }

    T& lvalue() { return this->value; }

private:
    static constexpr T MIN = std::numeric_limits<T>::min();
    static constexpr T MAX = std::numeric_limits<T>::max();

    template<typename U>
    static constexpr T sat_cast(U v)
    {
        if constexpr (std::is_signed_v<U> == std::is_signed_v<T>) {
            if (v < MIN) {
                return MIN;
            }
            if (v > MAX) {
                return MAX;
            }
        } else if constexpr (std::is_signed_v<U>) {
            if (v < 0) {
                return MIN;
            }
            if (static_cast<std::make_unsigned_t<U>>(v) > MAX) {
                return MAX;
            }
        } else if (v > static_cast<std::make_unsigned_t<T>>(MAX)) {
            return MAX;
        }
        return static_cast<T>(v);
    }

    static constexpr T sat_add(T lhs, T rhs)
    {
        T res{};
        if (__builtin_add_overflow(lhs, rhs, &res)) {
            return std::is_signed_v<T> && rhs < 0 ? MIN : MAX;
        }
        return res;
    }

    static constexpr T sat_sub(T lhs, T rhs)
    {
        T res{};
        if (__builtin_sub_overflow(lhs, rhs, &res)) {
            return std::is_signed_v<T> && rhs < 0 ? MAX : MIN;
        }
        return res;
    }

    static constexpr T sat_mul(T lhs, T rhs)
    {
        T res{};
        if (__builtin_mul_overflow(lhs, rhs, &res)) {
            return std::is_signed_v<T> && ((lhs < 0) != (rhs < 0)) ? MIN : MAX;
        }
        return res;
    }

    static constexpr T sat_div(T lhs, T rhs)
    {
        if constexpr (std::is_signed_v<T>) {
            // The one quotient that does not fit.
            if (lhs == MIN && rhs == T{-1}) {
                return MAX;
            }
        }
        return lhs / rhs;
    }

    T value;
};

/**
 * Macro that declares a strongly-typed integer and the empty class used as a
 * distinguisher.
 *
 * @param T The integer type.
 * @param name The name of the strongly-typed integer.
 */
#define STRONG_INT_TYPE(T, name) \
    class __##name##_distinct; \
    typedef strong_int<T, __##name##_distinct> name##_t
#endif
