/**
 * Copyright (c) 2024, Timothy Stack
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

#ifndef color_spaces_hh
#define color_spaces_hh

#include "result.h"
#include "intern_string.hh"
#include "mapbox/variant.hpp"

using palette_color = uint8_t;

enum class ansi_color : uint8_t {
    black,
    red,
    green,
    yellow,
    blue,
    magenta,
    cyan,
    white,
};

#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7

struct rgb_color {
    static constexpr rgb_color from(ansi_color color);

    explicit constexpr rgb_color(short r = -1, short g = -1, short b = -1)
        : rc_r(r), rc_g(g), rc_b(b)
    {
    }

    bool empty() const
    {
        return this->rc_r == -1 && this->rc_g == -1 && this->rc_b == -1;
    }

    bool operator==(const rgb_color& rhs) const;

    bool operator!=(const rgb_color& rhs) const;

    bool operator<(const rgb_color& rhs) const;

    bool operator>(const rgb_color& rhs) const;

    bool operator<=(const rgb_color& rhs) const;

    bool operator>=(const rgb_color& rhs) const;

    short rc_r;
    short rc_g;
    short rc_b;
};

struct lab_color {
    constexpr lab_color() : lc_l(0), lc_a(0), lc_b(0) {}

    explicit lab_color(const rgb_color& rgb);

    double deltaE(const lab_color& other) const;

    bool sufficient_contrast(const lab_color& other) const;

    lab_color& operator=(const lab_color& other)
    {
        this->lc_l = other.lc_l;
        this->lc_a = other.lc_a;
        this->lc_b = other.lc_b;

        return *this;
    }

    bool operator==(const lab_color& rhs) const;

    bool operator!=(const lab_color& rhs) const;

    bool operator<(const lab_color& rhs) const;

    bool operator>(const lab_color& rhs) const;

    bool operator<=(const lab_color& rhs) const;

    bool operator>=(const lab_color& rhs) const;

    double lc_l;
    double lc_a;
    double lc_b;
};

ansi_color to_ansi_color(const rgb_color& rgb);

namespace styling {

struct semantic {
    bool operator==(const semantic& rhs) const
    {
        return true;
    }
};

struct transparent {
    bool operator==(const transparent& rhs) const
    {
        return true;
    }
};

class color_unit {
public:
    static Result<color_unit, std::string> from_str(const string_fragment& sf);

    static color_unit make_empty() { return color_unit{transparent{}}; }

    static color_unit from_rgb(const rgb_color& rgb)
    {
        return color_unit{rgb};
    }

    static color_unit from_palette(const palette_color& indexed)
    {
        return color_unit{indexed};
    }

    color_unit& operator=(const rgb_color& rhs)
    {
        this->cu_value = rhs;
        return *this;
    }

    color_unit& operator=(const palette_color& rhs)
    {
        this->cu_value = rhs;
        return *this;
    }

    bool operator==(const color_unit& rhs) const
    {
        return this->cu_value == rhs.cu_value;
    }

    bool empty() const
    {
        return this->cu_value.match(
            [](transparent) { return true; },
            [](semantic) { return false; },
            [](const palette_color& pc) { return false; },
            [](const rgb_color& rc) { return rc.empty(); });
    }

    using variants_t = mapbox::util::variant<transparent, semantic, palette_color, rgb_color>;

    variants_t cu_value;

private:
    explicit color_unit(variants_t value) : cu_value(std::move(value)) {}
};

}  // namespace styling

#endif
