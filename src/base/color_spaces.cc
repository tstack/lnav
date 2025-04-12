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

#include <array>
#include <cmath>

#include "color_spaces.hh"

#include "config.h"
#include "from_trait.hh"

bool
rgb_color::operator<(const rgb_color& rhs) const
{
    if (rc_r < rhs.rc_r)
        return true;
    if (rhs.rc_r < rc_r)
        return false;
    if (rc_g < rhs.rc_g)
        return true;
    if (rhs.rc_g < rc_g)
        return false;
    return rc_b < rhs.rc_b;
}

bool
rgb_color::operator>(const rgb_color& rhs) const
{
    return rhs < *this;
}

bool
rgb_color::operator<=(const rgb_color& rhs) const
{
    return !(rhs < *this);
}

bool
rgb_color::operator>=(const rgb_color& rhs) const
{
    return !(*this < rhs);
}

constexpr rgb_color
rgb_color::from(const ansi_color color)
{
    switch (color) {
        case ansi_color::black:
            return rgb_color(0, 0, 0);
        case ansi_color::red:
            return rgb_color(255, 0, 0);
        case ansi_color::green:
            return rgb_color(0, 255, 0);
        case ansi_color::yellow:
            return rgb_color(255, 255, 0);
        case ansi_color::blue:
            return rgb_color(0, 0, 255);
        case ansi_color::magenta:
            return rgb_color(175, 0, 175);
        case ansi_color::cyan:
            return rgb_color(0, 255, 255);
        case ansi_color::white:
            return rgb_color(192, 192, 192);
        default:
            return rgb_color(0, 0, 0);
    }
}

bool
rgb_color::operator==(const rgb_color& rhs) const
{
    return rc_r == rhs.rc_r && rc_g == rhs.rc_g && rc_b == rhs.rc_b;
}

bool
rgb_color::operator!=(const rgb_color& rhs) const
{
    return !(rhs == *this);
}

lab_color::lab_color(const rgb_color& rgb)
{
    double r = rgb.rc_r / 255.0, g = rgb.rc_g / 255.0, b = rgb.rc_b / 255.0, x,
           y, z;

    r = (r > 0.04045) ? pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
    g = (g > 0.04045) ? pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
    b = (b > 0.04045) ? pow((b + 0.055) / 1.055, 2.4) : b / 12.92;

    x = (r * 0.4124 + g * 0.3576 + b * 0.1805) / 0.95047;
    y = (r * 0.2126 + g * 0.7152 + b * 0.0722) / 1.00000;
    z = (r * 0.0193 + g * 0.1192 + b * 0.9505) / 1.08883;

    x = (x > 0.008856) ? pow(x, 1.0 / 3.0) : (7.787 * x) + 16.0 / 116.0;
    y = (y > 0.008856) ? pow(y, 1.0 / 3.0) : (7.787 * y) + 16.0 / 116.0;
    z = (z > 0.008856) ? pow(z, 1.0 / 3.0) : (7.787 * z) + 16.0 / 116.0;

    this->lc_l = (116.0 * y) - 16;
    this->lc_a = 500.0 * (x - y);
    this->lc_b = 200.0 * (y - z);
}

rgb_color
lab_color::to_rgb() const
{
    float var_Y = (this->lc_l + 16.) / 116.;
    float var_X = this->lc_a / 500. + var_Y;
    float var_Z = var_Y - this->lc_b / 200.;

    if (pow(var_Y, 3) > 0.008856)
        var_Y = pow(var_Y, 3);
    else
        var_Y = (var_Y - 16. / 116.) / 7.787;
    if (pow(var_X, 3) > 0.008856)
        var_X = pow(var_X, 3);
    else
        var_X = (var_X - 16. / 116.) / 7.787;
    if (pow(var_Z, 3) > 0.008856)
        var_Z = pow(var_Z, 3);
    else
        var_Z = (var_Z - 16. / 116.) / 7.787;

    float X
        = 95.047 * var_X;  // ref_X =  95.047     Observer= 2°, Illuminant= D65
    float Y = 100.000 * var_Y;  // ref_Y = 100.000
    float Z = 108.883 * var_Z;  // ref_Z = 108.883

    var_X = X
        / 100.;  // X from 0 to  95.047      (Observer = 2°, Illuminant = D65)
    var_Y = Y / 100.;  // Y from 0 to 100.000
    var_Z = Z / 100.;  // Z from 0 to 108.883

    float var_R = var_X * 3.2406 + var_Y * -1.5372 + var_Z * -0.4986;
    float var_G = var_X * -0.9689 + var_Y * 1.8758 + var_Z * 0.0415;
    float var_B = var_X * 0.0557 + var_Y * -0.2040 + var_Z * 1.0570;

    if (var_R > 0.0031308)
        var_R = 1.055 * pow(var_R, (1 / 2.4)) - 0.055;
    else
        var_R = 12.92 * var_R;
    if (var_G > 0.0031308)
        var_G = 1.055 * pow(var_G, (1 / 2.4)) - 0.055;
    else
        var_G = 12.92 * var_G;
    if (var_B > 0.0031308)
        var_B = 1.055 * pow(var_B, (1 / 2.4)) - 0.055;
    else
        var_B = 12.92 * var_B;

    return rgb_color{
        (short) (var_R * 255.),
        (short) (var_G * 255.),
        (short) (var_B * 255.),
    };
}

double
lab_color::deltaE(const lab_color& other) const
{
    double deltaL = this->lc_l - other.lc_l;
    double deltaA = this->lc_a - other.lc_a;
    double deltaB = this->lc_b - other.lc_b;
    double c1 = sqrt(this->lc_a * this->lc_a + this->lc_b * this->lc_b);
    double c2 = sqrt(other.lc_a * other.lc_a + other.lc_b * other.lc_b);
    double deltaC = c1 - c2;
    double deltaH = deltaA * deltaA + deltaB * deltaB - deltaC * deltaC;
    deltaH = deltaH < 0.0 ? 0.0 : sqrt(deltaH);
    double sc = 1.0 + 0.045 * c1;
    double sh = 1.0 + 0.015 * c1;
    double deltaLKlsl = deltaL / (1.0);
    double deltaCkcsc = deltaC / (sc);
    double deltaHkhsh = deltaH / (sh);
    double i = deltaLKlsl * deltaLKlsl + deltaCkcsc * deltaCkcsc
        + deltaHkhsh * deltaHkhsh;
    return i < 0.0 ? 0.0 : sqrt(i);
}

bool
lab_color::operator<(const lab_color& rhs) const
{
    if (lc_l < rhs.lc_l)
        return true;
    if (rhs.lc_l < lc_l)
        return false;
    if (lc_a < rhs.lc_a)
        return true;
    if (rhs.lc_a < lc_a)
        return false;
    return lc_b < rhs.lc_b;
}

bool
lab_color::operator>(const lab_color& rhs) const
{
    return rhs < *this;
}

bool
lab_color::operator<=(const lab_color& rhs) const
{
    return !(rhs < *this);
}

bool
lab_color::operator>=(const lab_color& rhs) const
{
    return !(*this < rhs);
}

ansi_color
to_ansi_color(const rgb_color& color)
{
    static const auto term_colors = std::array<lab_color, 8>{
        lab_color{rgb_color::from(ansi_color::black)},
        lab_color{rgb_color::from(ansi_color::red)},
        lab_color{rgb_color::from(ansi_color::green)},
        lab_color{rgb_color::from(ansi_color::yellow)},
        lab_color{rgb_color::from(ansi_color::blue)},
        lab_color{rgb_color::from(ansi_color::magenta)},
        lab_color{rgb_color::from(ansi_color::cyan)},
        lab_color{rgb_color::from(ansi_color::white)},
    };

    const auto desired = lab_color{color};
    auto retval = ansi_color::white;
    double lowest_delta = std::numeric_limits<double>::max();
    for (uint8_t lpc = 0; lpc < term_colors.size(); ++lpc) {
        if (const auto this_delta = term_colors[lpc].deltaE(desired);
            this_delta < lowest_delta)
        {
            lowest_delta = this_delta;
            retval = ansi_color{lpc};
        }
    }

    return retval;
}

bool
lab_color::operator==(const lab_color& rhs) const
{
    return lc_l == rhs.lc_l && lc_a == rhs.lc_a && lc_b == rhs.lc_b;
}

bool
lab_color::operator!=(const lab_color& rhs) const
{
    return !(rhs == *this);
}

bool
lab_color::sufficient_contrast(const lab_color& other) const
{
    if (std::abs(this->lc_l - other.lc_l) < 45) {
        return false;
    }

    return (std::signbit(this->lc_a) != std::signbit(other.lc_a)
            || std::signbit(this->lc_b) != std::signbit(other.lc_b));
}

namespace styling {

const color_unit color_unit::EMPTY = color_unit{transparent{}};

}  // namespace styling
