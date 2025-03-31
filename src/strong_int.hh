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

/**
 * Template class for "strongly-typed" integers, in other words, integers that
 * have different semantic meaning and cannot be easily used in place of one
 * another.
 *
 * @param T The integer type.
 * @param DISTINCT An class used solely to distinguish templates that have the
 * same integer type.
 */
template<typename T, class DISTINCT>
class strong_int {
public:
    explicit constexpr strong_int(T v = 0) noexcept : value(v){};
    operator const T&() const { return this->value; }
    constexpr strong_int operator+(const strong_int& rhs) const
    {
        return strong_int(this->value + rhs.value);
    }
    constexpr strong_int operator-(const strong_int& rhs) const
    {
        return strong_int(this->value - rhs.value);
    }
    constexpr strong_int operator/(const strong_int& rhs) const
    {
        return strong_int(this->value / rhs.value);
    }
    constexpr strong_int operator*(const strong_int& rhs) const
    {
        return strong_int(this->value * rhs.value);
    }
    constexpr bool operator<(const strong_int& rhs) const
    {
        return this->value < rhs.value;
    }
    strong_int& operator+=(const strong_int& rhs)
    {
        this->value += rhs.value;
        return *this;
    }
    strong_int& operator-=(const strong_int& rhs)
    {
        this->value -= rhs.value;
        return *this;
    }
    strong_int& operator-()
    {
        this->value = -this->value;
        return *this;
    }
    strong_int& operator++()
    {
        this->value++;
        return *this;
    }
    strong_int& operator--()
    {
        this->value--;
        return *this;
    }
    constexpr bool operator==(const strong_int& rhs) const
    {
        return this->value == rhs.value;
    }
    T* out() { return &this->value; }

    T& lvalue() { return this->value; }

private:
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
