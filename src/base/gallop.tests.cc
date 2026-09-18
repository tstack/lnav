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

#include <random>
#include <vector>

#include "gallop.hh"

#include "doctest/doctest.h"

using namespace lnav;

TEST_CASE("gallop_lower_bound-empty")
{
    std::vector<int> v;

    CHECK(gallop_lower_bound(v.begin(), v.end(), 42) == v.end());
}

TEST_CASE("gallop_lower_bound-single")
{
    std::vector<int> v{10};

    CHECK(gallop_lower_bound(v.begin(), v.end(), 5) - v.begin() == 0);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 10) - v.begin() == 0);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 15) == v.end());
}

TEST_CASE("gallop_lower_bound-at-first")
{
    // The bound at offset zero is the case the doubling can skip past, since
    // the first probe is at offset one.
    std::vector<int> v{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    CHECK(gallop_lower_bound(v.begin(), v.end(), -1) - v.begin() == 0);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 0) - v.begin() == 0);
}

TEST_CASE("gallop_lower_bound-at-last")
{
    std::vector<int> v{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    CHECK(gallop_lower_bound(v.begin(), v.end(), 9) - v.begin() == 9);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 10) == v.end());
    CHECK(gallop_lower_bound(v.begin(), v.end(), 1000) == v.end());
}

TEST_CASE("gallop_lower_bound-near-first")
{
    // Offsets one through five straddle the hand-off from the doubling to the
    // bisection of the final bracket.
    std::vector<int> v;
    for (int lpc = 0; lpc < 64; lpc++) {
        v.push_back(lpc * 2);
    }

    for (int lpc = 1; lpc <= 5; lpc++) {
        CHECK(gallop_lower_bound(v.begin(), v.end(), lpc * 2) - v.begin()
              == lpc);
        // An absent value lands on the first element above it.
        CHECK(gallop_lower_bound(v.begin(), v.end(), lpc * 2 - 1) - v.begin()
              == lpc);
    }
}

TEST_CASE("gallop_lower_bound-duplicates")
{
    // lower_bound() returns the *first* of an equal run.
    std::vector<int> v{0, 1, 1, 1, 1, 1, 1, 1, 2, 3};

    CHECK(gallop_lower_bound(v.begin(), v.end(), 1) - v.begin() == 1);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 2) - v.begin() == 8);
}

TEST_CASE("gallop_lower_bound-all-equal")
{
    std::vector<int> v(100, 7);

    CHECK(gallop_lower_bound(v.begin(), v.end(), 7) - v.begin() == 0);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 6) - v.begin() == 0);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 8) == v.end());
}

TEST_CASE("gallop_lower_bound-far-bound")
{
    // A long range with the answer at the far end is the case the doubling
    // pays for; it still has to be right.
    std::vector<int> v;
    for (int lpc = 0; lpc < 100000; lpc++) {
        v.push_back(lpc);
    }

    CHECK(gallop_lower_bound(v.begin(), v.end(), 99999) - v.begin() == 99999);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 65536) - v.begin() == 65536);
}

TEST_CASE("gallop_lower_bound-heterogeneous")
{
    // The value does not have to be the element type, which is how the log
    // merge searches an array of loglines with a timestamp.
    struct elem {
        int e_key;
        const char* e_data;

        bool operator<(int rhs) const { return this->e_key < rhs; }
    };

    std::vector<elem> v{
        {1, "a"}, {3, "b"}, {5, "c"}, {7, "d"}, {9, "e"},
    };

    CHECK(gallop_lower_bound(v.begin(), v.end(), 5) - v.begin() == 2);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 4) - v.begin() == 2);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 0) - v.begin() == 0);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 10) == v.end());
}

TEST_CASE("gallop_lower_bound-comparator")
{
    // Descending order, to check that nothing assumes operator<.
    std::vector<int> v{9, 7, 5, 3, 1};
    const auto desc = [](int lhs, int rhs) { return lhs > rhs; };

    CHECK(gallop_lower_bound(v.begin(), v.end(), 5, desc) - v.begin() == 2);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 6, desc) - v.begin() == 2);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 9, desc) - v.begin() == 0);
    CHECK(gallop_lower_bound(v.begin(), v.end(), 0, desc) == v.end());
}

TEST_CASE("gallop_lower_bound-differential")
{
    // The whole contract is "the same answer as std::lower_bound", so check
    // that directly over a pile of random inputs.
    std::mt19937 gen(42);

    for (size_t len : {0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 100, 1000}) {
        // A small spread of values relative to the length makes duplicates
        // and gaps common.
        std::uniform_int_distribution<int> val_dist(0, (int) len / 2 + 2);

        for (int trial = 0; trial < 20; trial++) {
            std::vector<int> v;
            for (size_t lpc = 0; lpc < len; lpc++) {
                v.push_back(val_dist(gen));
            }
            std::sort(v.begin(), v.end());

            for (int needle = -1; needle <= (int) len / 2 + 3; needle++) {
                const auto expected
                    = std::lower_bound(v.begin(), v.end(), needle);
                const auto actual
                    = gallop_lower_bound(v.begin(), v.end(), needle);

                CHECK(actual == expected);
            }
        }
    }
}
