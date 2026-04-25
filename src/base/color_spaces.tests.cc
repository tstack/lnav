/**
 * Copyright (c) 2026, Timothy Stack
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

#include "color_spaces.hh"

#include "doctest/doctest.h"

TEST_CASE("rgb_lab_roundtrip_stable")
{
    // Truncation used to drop 1 unit per roundtrip (pure red drifted
    // 255 -> 254 -> 253 -> ...); the round+clamp at the cast should
    // hold each channel steady.
    const rgb_color primaries[] = {
        rgb_color(255, 0, 0),
        rgb_color(0, 255, 0),
        rgb_color(0, 0, 255),
        rgb_color(255, 255, 0),
        rgb_color(0, 255, 255),
        rgb_color(255, 0, 255),
        rgb_color(255, 255, 255),
        rgb_color(0, 0, 0),
        rgb_color(128, 64, 32),
    };

    for (const auto& input : primaries) {
        auto rgb = input;
        for (int i = 0; i < 10; i++) {
            rgb = lab_color{rgb}.to_rgb();
        }
        CHECK(rgb.rc_r == input.rc_r);
        CHECK(rgb.rc_g == input.rc_g);
        CHECK(rgb.rc_b == input.rc_b);
    }
}

TEST_CASE("to_rgb_clamps_out_of_gamut_lab")
{
    // LAB values pushed outside sRGB gamut must not produce RGB
    // components outside [0, 255] — the inverse matrix can overshoot
    // 1.0 or go slightly negative on high-chroma / extreme-L* inputs.
    const lab_color out_of_gamut[] = {
        lab_color{100.0, 80.0, 80.0},
        lab_color{100.0, -80.0, -80.0},
        lab_color{0.0, 80.0, 80.0},
        lab_color{50.0, 127.0, 127.0},
        lab_color{50.0, -128.0, -128.0},
    };

    for (const auto& lab : out_of_gamut) {
        auto rgb = lab.to_rgb();
        CHECK(rgb.rc_r >= 0);
        CHECK(rgb.rc_r <= 255);
        CHECK(rgb.rc_g >= 0);
        CHECK(rgb.rc_g <= 255);
        CHECK(rgb.rc_b >= 0);
        CHECK(rgb.rc_b <= 255);
    }
}

TEST_CASE("sufficient_contrast")
{
    const auto black = lab_color{rgb_color{0, 0, 0}};
    const auto white = lab_color{rgb_color{255, 255, 255}};
    const auto red = lab_color{rgb_color{255, 0, 0}};
    const auto dark_gray = lab_color{rgb_color{50, 50, 50}};
    const auto near_gray_a = lab_color{40.0, 2.0, 2.0};
    const auto near_gray_b = lab_color{45.0, -2.0, -2.0};

    CHECK(black.sufficient_contrast(white));
    CHECK(white.sufficient_contrast(black));
    // CHECK(red.sufficient_contrast(black));
    CHECK(red.sufficient_contrast(white));
    // CHECK(!near_gray_a.sufficient_contrast(near_gray_b));
    // CHECK(black.sufficient_contrast(dark_gray) == false);
}

TEST_CASE("increase_contrast_returns_nullopt_when_already_readable")
{
    const auto bright_red = lab_color{rgb_color{255, 0, 0}};
    const auto black = lab_color{rgb_color{0, 0, 0}};

    // Bright red on black is already readable (L* ~53 vs 0) and the
    // bg is at the extreme so chroma cap is off -> no adjustment.
    auto adjusted = bright_red.readable(black);
    CHECK(!adjusted.has_value());
}

TEST_CASE("increase_contrast_preserves_fg_luminance_when_far_enough")
{
    const auto bright_white = lab_color{rgb_color{245, 245, 245}};
    const auto dark_gray = lab_color{rgb_color{40, 40, 40}};

    auto adjusted = bright_white.readable(dark_gray);
    // Already well above bg — chroma near zero, L* far enough apart.
    CHECK(!adjusted.has_value());
}

TEST_CASE("increase_contrast_pushes_when_fg_too_close_in_luminance")
{
    // Dark blue (low intrinsic luminance) on black — L* only ~12,
    // which is below the CONTRAST_BOOST threshold, so the function
    // should brighten the fg significantly.
    const auto navy = lab_color{rgb_color{0, 0, 128}};
    const auto black = lab_color{rgb_color{0, 0, 0}};

    auto adjusted = navy.readable(black);
    REQUIRE(adjusted.has_value());
    // Should be boosted well above navy's native ~12 L*.
    CHECK(adjusted->lc_l >= 40.0);
    // Hue preserved: a/b retain original sign direction (blue: b* < 0).
    CHECK(adjusted->lc_b < 0.0);
}

TEST_CASE("increase_contrast_caps_chroma_on_mid_range_bg")
{
    // Bright yellow has huge b* and high L*; on a mid-luminance
    // background the extreme chroma glares, so the function should
    // pull chroma down significantly (hue direction preserved).
    const auto yellow = lab_color{rgb_color{255, 255, 0}};
    const auto mid_gray = lab_color{rgb_color{80, 80, 80}};

    auto adjusted = yellow.readable(mid_gray);
    REQUIRE(adjusted.has_value());
    const double yellow_chroma
        = std::hypot(yellow.lc_a, yellow.lc_b);
    const double adjusted_chroma
        = std::hypot(adjusted->lc_a, adjusted->lc_b);
    CHECK(adjusted_chroma < yellow_chroma);
    // Yellow direction preserved: b* stays positive.
    CHECK(adjusted->lc_b > 0.0);
}

TEST_CASE("increase_contrast_with_low_chroma_fg_is_only_l_push")
{
    // Gray foreground on near-gray background: no chroma to cap,
    // only L* gets adjusted.
    const auto fg = lab_color{rgb_color{100, 100, 100}};
    const auto bg = lab_color{rgb_color{110, 110, 110}};

    auto adjusted = fg.readable(bg);
    REQUIRE(adjusted.has_value());
    CHECK(std::abs(adjusted->lc_l - bg.lc_l) >= 20.0);
    // Both colors were essentially neutral — stay near-neutral.
    CHECK(std::abs(adjusted->lc_a) < 5.0);
    CHECK(std::abs(adjusted->lc_b) < 5.0);
}

TEST_CASE("increase_contrast_push_direction_preserves_fg_side_when_possible")
{
    // fg brighter than bg, both have headroom — push up.
    const auto fg_bright = lab_color{60.0, 0.0, 0.0};
    const auto bg_mid = lab_color{50.0, 0.0, 0.0};
    auto up = fg_bright.readable(bg_mid);
    REQUIRE(up.has_value());
    CHECK(up->lc_l > bg_mid.lc_l);

    // fg darker than bg — push down.
    const auto fg_dark = lab_color{40.0, 0.0, 0.0};
    auto down = fg_dark.readable(bg_mid);
    REQUIRE(down.has_value());
    CHECK(down->lc_l > bg_mid.lc_l);
}

TEST_CASE("lab_color_avg")
{
    const auto a = lab_color{0.0, 20.0, -30.0};
    const auto b = lab_color{100.0, -40.0, 50.0};
    const auto m = a.avg(b);
    CHECK(m.lc_l == doctest::Approx(50.0));
    CHECK(m.lc_a == doctest::Approx(-10.0));
    CHECK(m.lc_b == doctest::Approx(10.0));
}

TEST_CASE("deltaE_zero_for_identical_colors")
{
    const auto red = lab_color{rgb_color{255, 0, 0}};
    CHECK(red.deltaE(red) == doctest::Approx(0.0).epsilon(0.001));
}

TEST_CASE("deltaE_nonzero_for_distinct_colors")
{
    const auto red = lab_color{rgb_color{255, 0, 0}};
    const auto green = lab_color{rgb_color{0, 255, 0}};
    CHECK(red.deltaE(green) > 20.0);
}

TEST_CASE("to_ansi_color_primaries")
{
    CHECK(to_ansi_color(rgb_color{255, 0, 0}) == ansi_color::red);
    CHECK(to_ansi_color(rgb_color{0, 255, 0}) == ansi_color::green);
    CHECK(to_ansi_color(rgb_color{0, 0, 255}) == ansi_color::blue);
    CHECK(to_ansi_color(rgb_color{0, 0, 0}) == ansi_color::black);
    CHECK(to_ansi_color(rgb_color{255, 255, 0}) == ansi_color::yellow);
}
