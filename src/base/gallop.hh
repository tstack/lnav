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

#ifndef lnav_gallop_hh
#define lnav_gallop_hh

#include <algorithm>
#include <iterator>

namespace lnav {

/**
 * The same contract as std::lower_bound(): return the first element of the
 * sorted range for which comp(element, value) is false.
 *
 * The difference is the probe sequence.  std::lower_bound() halves the whole
 * range, so it costs log2(distance(first, last)) no matter where the answer
 * is.  This starts at "first" and doubles -- 1, 2, 4, 8 -- until it overshoots,
 * then bisects the last bracket, which costs log2(distance(first, result)).
 * A bound that sits close to "first" is therefore found in a couple of probes
 * against a long range, and those probes walk forward instead of jumping
 * around, which is friendlier to the prefetcher.
 *
 * The trade is the usual one for galloping: roughly twice the probes of a
 * plain bisection when the answer really is at the far end of the range.  Use
 * this where the bound is expected to land near "first" most of the time, and
 * std::lower_bound() otherwise.
 *
 * The range must be partitioned with respect to comp(element, value), and the
 * iterators must be random-access.
 */
template<typename Iter, typename T, typename Cmp>
Iter
gallop_lower_bound(Iter first, Iter last, const T& value, Cmp comp)
{
    using diff_t = typename std::iterator_traits<Iter>::difference_type;

    const auto len = std::distance(first, last);
    diff_t prev = 0;
    diff_t step = 2;

    while (step < len && comp(*(first + step), value)) {
        prev = step;
        step *= 2;
    }

    // The gallop only proves that the element at "prev" is below the bound,
    // so the bracket is (prev, step] and the bisection covers [prev, step).
    // When the loop never ran, that is [0, 1), which still tests the first
    // element.  Clamping to "len" keeps the iterator inside the range, since
    // the doubling can run past the end.
    return std::lower_bound(
        first + prev, first + std::min(step, len), value, comp);
}

template<typename Iter, typename T>
Iter
gallop_lower_bound(Iter first, Iter last, const T& value)
{
    return gallop_lower_bound(
        first, last, value, [](const auto& lhs, const auto& rhs) {
            return lhs < rhs;
        });
}

}  // namespace lnav

#endif
