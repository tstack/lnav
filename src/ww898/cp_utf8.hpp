/*
 * MIT License
 * 
 * Copyright (c) 2017-2019 Mikhail Pilin
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *  
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <cstdint>
#include <stdexcept>

namespace ww898 {
namespace utf {

// Supported combinations:
//   0xxx_xxxx
//   110x_xxxx 10xx_xxxx
//   1110_xxxx 10xx_xxxx 10xx_xxxx
//   1111_0xxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
//   1111_10xx 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
//   1111_110x 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
struct utf8 final
{
    static size_t const max_unicode_symbol_size = 4;
    static size_t const max_supported_symbol_size = 6;

    static uint32_t const max_supported_code_point = 0x7FFFFFFF;

    using char_type = uint8_t;

    template<typename PeekFn>
    static size_t char_size(PeekFn && peek_fn)
    {
        char_type const ch0 = std::forward<PeekFn>(peek_fn)();
        if (ch0 < 0x80) // 0xxx_xxxx
            return 1;
        if (ch0 < 0xC0)
            throw std::runtime_error("The utf8 first char in sequence is incorrect");
        if (ch0 < 0xE0) // 110x_xxxx 10xx_xxxx
            return 2;
        if (ch0 < 0xF0) // 1110_xxxx 10xx_xxxx 10xx_xxxx
            return 3;
        if (ch0 < 0xF8) // 1111_0xxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
            return 4;
        if (ch0 < 0xFC) // 1111_10xx 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
            return 5;
        if (ch0 < 0xFE) // 1111_110x 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
            return 6;
        throw std::runtime_error("The utf8 first char in sequence is incorrect");
    }

    template<typename ReadFn>
    static uint32_t read(ReadFn && read_fn)
    {
        char_type const ch0 = read_fn();
        if (ch0 < 0x80) // 0xxx_xxxx
            return ch0;
        if (ch0 < 0xC0)
            throw std::runtime_error("The utf8 first char in sequence is incorrect");
        if (ch0 < 0xE0) // 110x_xxxx 10xx_xxxx
        {
            char_type const ch1 = read_fn(); if (ch1 >> 6 != 2) goto _err;
            return (ch0 << 6) + ch1 - 0x3080;
        }
        if (ch0 < 0xF0) // 1110_xxxx 10xx_xxxx 10xx_xxxx
        {
            char_type const ch1 = read_fn(); if (ch1 >> 6 != 2) goto _err;
            char_type const ch2 = read_fn(); if (ch2 >> 6 != 2) goto _err;
            return (ch0 << 12) + (ch1 << 6) + ch2 - 0xE2080;
        }
        if (ch0 < 0xF8) // 1111_0xxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
        {
            char_type const ch1 = read_fn(); if (ch1 >> 6 != 2) goto _err;
            char_type const ch2 = read_fn(); if (ch2 >> 6 != 2) goto _err;
            char_type const ch3 = read_fn(); if (ch3 >> 6 != 2) goto _err;
            return (ch0 << 18) + (ch1 << 12) + (ch2 << 6) + ch3 - 0x3C82080;
        }
        if (ch0 < 0xFC) // 1111_10xx 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
        {
            char_type const ch1 = read_fn(); if (ch1 >> 6 != 2) goto _err;
            char_type const ch2 = read_fn(); if (ch2 >> 6 != 2) goto _err;
            char_type const ch3 = read_fn(); if (ch3 >> 6 != 2) goto _err;
            char_type const ch4 = read_fn(); if (ch4 >> 6 != 2) goto _err;
            return (ch0 << 24) + (ch1 << 18) + (ch2 << 12) + (ch3 << 6) + ch4 - 0xFA082080;
        }
        if (ch0 < 0xFE) // 1111_110x 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
        {
            char_type const ch1 = read_fn(); if (ch1 >> 6 != 2) goto _err;
            char_type const ch2 = read_fn(); if (ch2 >> 6 != 2) goto _err;
            char_type const ch3 = read_fn(); if (ch3 >> 6 != 2) goto _err;
            char_type const ch4 = read_fn(); if (ch4 >> 6 != 2) goto _err;
            char_type const ch5 = read_fn(); if (ch5 >> 6 != 2) goto _err;
            return (ch0 << 30) + (ch1 << 24) + (ch2 << 18) + (ch3 << 12) + (ch4 << 6) + ch5 - 0x82082080;
        }
        throw std::runtime_error("The utf8 first char in sequence is incorrect");
        _err: throw std::runtime_error("The utf8 slave char in sequence is incorrect");
    }

    template<typename WriteFn>
    static void write(uint32_t const cp, WriteFn && write_fn)
    {
        if (cp < 0x80)          // 0xxx_xxxx
            write_fn(static_cast<char_type>(cp));
        else if (cp < 0x800)    // 110x_xxxx 10xx_xxxx
        {
            write_fn(static_cast<char_type>(0xC0 | cp >>  6));
            goto _1;
        }
        else if (cp < 0x10000)  // 1110_xxxx 10xx_xxxx 10xx_xxxx
        {
            write_fn(static_cast<char_type>(0xE0 | cp >> 12));
            goto _2;
        }
        else if (cp < 0x200000) // 1111_0xxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
        {
            write_fn(static_cast<char_type>(0xF0 | cp >> 18));
            goto _3;
        }
        else if (cp < 0x4000000) // 1111_10xx 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
        {
            write_fn(static_cast<char_type>(0xF8 | cp >> 24));
            goto _4;
        }
        else if (cp < 0x80000000) // 1111_110x 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
        {
            write_fn(static_cast<char_type>(0xFC | cp >> 30));
            goto _5;
        }
        else
            throw std::runtime_error("Tool large UTF8 code point");
        return;
        _5: write_fn(static_cast<char_type>(0x80 | (cp >> 24 & 0x3F)));
        _4: write_fn(static_cast<char_type>(0x80 | (cp >> 18 & 0x3F)));
        _3: write_fn(static_cast<char_type>(0x80 | (cp >> 12 & 0x3F)));
        _2: write_fn(static_cast<char_type>(0x80 | (cp >>  6 & 0x3F)));
        _1: write_fn(static_cast<char_type>(0x80 | (cp       & 0x3F)));
    }
};

}}
