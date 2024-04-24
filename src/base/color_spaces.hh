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

struct rgb_color {
    explicit rgb_color(short r = -1, short g = -1, short b = -1)
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
    lab_color() : lc_l(0), lc_a(0), lc_b(0) {}

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

#endif
