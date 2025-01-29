/**
 * Copyright (c) 2020, Timothy Stack
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

#ifndef lnav_math_util_hh
#define lnav_math_util_hh

#include <sys/types.h>

#undef rounddown
#undef roundup

/**
 * Round down a number based on a given granularity.
 *
 * @param
 * @param step The granularity.
 */
template<typename Size, typename Step>
auto
rounddown(Size size, Step step)
{
    return size - (size % step);
}

template<typename Size, typename Step>
auto
roundup(Size size, Step step)
{
    auto retval = size + (step - 1);

    retval -= (retval % step);

    return retval;
}

inline int
rounddown_offset(size_t size, int step, int offset)
{
    return size - ((size - offset) % step);
}

template<typename Size, typename Step>
auto
roundup_size(Size size, Step step)
{
    auto retval = size + step;

    retval -= (retval % step);

    return retval;
}

template<typename T>
T
abs_diff(T a, T b)
{
    return a > b ? a - b : b - a;
}

template<typename T>
class clamped {
public:
    static clamped from(T value, T min, T max) { return {value, min, max}; }

    clamped& operator+=(T rhs)
    {
        if (rhs < 0) {
            return this->operator-=(-rhs);
        }

        if (this->c_value + rhs < this->c_max) {
            this->c_value += rhs;
        } else {
            this->c_value = this->c_max;
        }

        return *this;
    }

    clamped& operator-=(T rhs)
    {
        if (rhs < 0) {
            return this->operator+=(-rhs);
        }

        if (this->c_value - rhs > this->c_min) {
            this->c_value -= rhs;
        } else {
            this->c_value = this->c_min;
        }

        return *this;
    }

    bool available_to_consume(T rhs) const
    {
        return (this->c_value - rhs > this->c_min);
    }

    bool try_consume(T rhs)
    {
        if (rhs == 0) {
            return false;
        }

        if (this->c_value - rhs > this->c_min) {
            this->c_value -= rhs;
            return true;
        }

        return false;
    }

    operator T() const { return this->c_value; }

    bool is_min() const { return this->c_value == this->c_min; }

    T get_min() const { return this->c_min; }

    T get_max() const { return this->c_max; }

private:
    clamped(T value, T min, T max) : c_value(value), c_min(min), c_max(max) {}

    T c_value;
    T c_min;
    T c_max;
};

template<typename T>
size_t
count_digits(T n)
{
    return n == 0 ? 1 : 1 + std::floor(std::log10(std::abs(n)));
}

#endif
