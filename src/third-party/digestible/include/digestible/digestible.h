#ifndef _DIGESTIBLE_H_
#define _DIGESTIBLE_H_

/*
 * Licensed to Ted Dunning under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace digestible {

// XXX: yes, this could be a std::pair. But being able to refer to values by name
// instead of .first and .second makes merge(), quantile(), and
// cumulative_distribution() way more readable.
template <typename Values = float, typename Weight = unsigned>
struct centroid
{
    Values mean;
    Weight weight;

    centroid(Values new_mean, Weight new_weight)
        : mean(new_mean)
        , weight(new_weight)
    {}
};

template <typename Values = float, typename Weight = unsigned>
inline bool operator<(const centroid<Values, Weight> &lhs,
                      const centroid<Values, Weight> &rhs)
{
    return (lhs.mean < rhs.mean);
}

template <typename Values = float, typename Weight = unsigned>
inline bool operator>(const centroid<Values, Weight> &lhs,
                      const centroid<Values, Weight> &rhs)
{
    return (lhs.mean > rhs.mean);
}

template <typename Values = float, typename Weight = unsigned>
class tdigest
{
    using centroid_t = centroid<Values, Weight>;

    struct tdigest_impl {
        std::vector<centroid_t> values;
        Weight total_weight;

        explicit tdigest_impl(size_t size)
            : total_weight(0)
        {
            values.reserve(size);
        }

        tdigest_impl() = delete;

        enum class insert_result { OK, NEED_COMPRESS };

        insert_result insert(Values value, Weight weight)
        {
            assert(weight);
            values.emplace_back(value, weight);
            total_weight += weight;
            return (values.size() != values.capacity() ? insert_result::OK
                                                       : insert_result::NEED_COMPRESS);
        }

        insert_result insert(const centroid_t &val)
        {
            return (insert(val.mean, val.weight));
        }

        void reset()
        {
            values.clear();
            total_weight = 0;
        }

        size_t capacity() const { return (values.capacity()); }
    };

    tdigest_impl one;
    tdigest_impl two;

    // XXX: buffer multiplier must be > 0. BUT how much greater
    // will affect size vs speed balance. The effects of which
    // have not been studied. Set to 2 to favor size. Informal
    // benchmarks for this value yielded acceptable results.
    static constexpr size_t buffer_multiplier = 2;
    tdigest_impl buffer;

    tdigest_impl *active;

    Values min_val, max_val;
    bool run_forward;

    void swap(tdigest&);

public:
    explicit tdigest(size_t size);

    tdigest() = delete;

    tdigest(const tdigest&);
    tdigest(tdigest&&) noexcept;
    tdigest& operator=(tdigest);

    /**
     * Inserts the given value into the t-digest input buffer.
     * Assumes a weight of 1 for the sample.
     * If this operation fills the input buffer an automatic merge()
     * operation is triggered.
     *
     * @param value
     *  value to insert
     */
    void insert(Values value)
    {
        insert(value, 1);
    }

    /**
     * Inserts the given value with the given weight into the t-digest input buffer.
     * If this operation fills the input buffer an automatic merge()
     * operation is triggered.
     *
     * @param value
     *  value to insert
     *
     * @param weight
     *  weight associated with the provided value
     */
    void insert(Values value, Weight weight)
    {
        if (buffer.insert(value, weight)
            == tdigest_impl::insert_result::NEED_COMPRESS) {
            merge();
        }
    }

    /**
     * Insert and merge an existing t-digest. Assumes source t-digest
     * is not receiving new data.
     *
     * @param src
     *  source t-digest to copy values from
     */
    void insert(const tdigest<Values, Weight> &src);

    /**
     * Reset internal state of the t-digest.
     *
     * Clears t-digest, incoming data buffer, and resets min/max values.
     */
    void reset();

    /**
     * Retrieve contents of the t-digest.
     *
     * @return
     *  list of <value, weight> pairs sorted by value in ascending order.
     */
    std::vector<std::pair<Values, Weight>> get() const;

    /**
     * Merge incoming data into the t-digest.
     */
    void merge();

    /**
     * Retrieve the probability that a random data point in the digest is
     * less than or equal to x.
     *
     * @param x
     *  value of interest
     *
     * @return
     *  a value between [0, 1]
     */
    double cumulative_distribution(Values x) const;

    /**
     * Retrieve the value such that p percent of all data points or less than or
     * equal to that value.
     *
     * @param p
     *  percentile of interest; valid input between [0.0, 100.0]
     *
     * @return
     *  a value from the dataset that satisfies the above condition
     */
    double quantile(double p) const;

    /**
     * Return number of centroids in the t-digest
     */
    size_t centroid_count() const
    {
        return ((*active).values.size());
    }

    /**
     * Retrieve the number of merged data points in the t-digest
     *
     * @return
     *   the total weight of all data
     */
    size_t size() const
    {
        return ((*active).total_weight);
    }

    /**
     * Retrieve maximum value seen by this t-digest.
     * Value is cleared by reset().
     *
     * @return
     *  maximum value seen by this t-digest.
     */
    Values max() const
    {
        return (max_val);
    }

    /**
     * Retrieve minimum value seen by this t-digest.
     * Value is cleared by reset().
     *
     * @return
     *  minimum value seen by this t-digest.
     */
    Values min() const
    {
        return (min_val);
    }
};


template <typename Values, typename Weight>
tdigest<Values, Weight>::tdigest(size_t size)
    : one(size)
    , two(size)
    , buffer(size * buffer_multiplier)
    , active(&one)
    , min_val(std::numeric_limits<Values>::max())
    , max_val(std::numeric_limits<Values>::lowest())
    , run_forward(true)
{}

template <typename Values, typename Weight>
tdigest<Values, Weight>::tdigest(const tdigest<Values, Weight>& other)
    : one(other.one)
    , two(other.two)
    , buffer(other.buffer)
    , active(other.active == &other.one ? &one : &two)
    , min_val(other.min_val)
    , max_val(other.max_val)
    , run_forward(other.run_forward)
{}

template <typename Values, typename Weight>
void tdigest<Values, Weight>::swap(tdigest<Values, Weight>& other)
{
    std::swap(one, other.one);
    std::swap(two, other.two);
    std::swap(buffer, other.buffer);
    active = other.active == &other.one ? &one : &two;
    other.active = active == &one ? &other.one : &other.two;
    std::swap(min_val, other.min_val);
    std::swap(max_val, other.max_val);
    std::swap(run_forward, other.run_forward);
}

template <typename Values, typename Weight>
tdigest<Values, Weight>::tdigest(tdigest<Values, Weight>&& other) noexcept
    : tdigest(other.one.capacity())
{
    swap(other);
}

template <typename Values, typename Weight>
tdigest<Values, Weight>& tdigest<Values, Weight>::operator=(tdigest<Values, Weight> other)
{
    swap(other);
    return (*this);
}

template <typename Values, typename Weight>
void tdigest<Values, Weight>::insert(const tdigest<Values, Weight> &src)
{
    max_val = std::max(max_val, src.max_val);
    min_val = std::min(min_val, src.min_val);

    auto insert_fn = [this](const auto &val) {
        if (buffer.insert(val) == tdigest_impl::insert_result::NEED_COMPRESS) { merge(); }
    };

    std::for_each(src.active->values.begin(), src.active->values.end(), insert_fn);
    // Explicitly merge any unmerged data for a consistent end state.
    merge();
}

template <typename Values, typename Weight>
void tdigest<Values, Weight>::reset()
{
    one.reset();
    two.reset();
    buffer.reset();
    active  = &one;
    min_val = std::numeric_limits<Values>::max();
    max_val = std::numeric_limits<Values>::lowest();
}

template <typename Values, typename Weight>
std::vector<std::pair<Values, Weight>> tdigest<Values, Weight>::get() const
{
    std::vector<std::pair<Values, Weight>> to_return;

    std::transform(active->values.begin(), active->values.end(), std::back_inserter(to_return),
                   [](const centroid_t &val) { return (std::make_pair(val.mean, val.weight)); });

    return (to_return);
}

/**
 * When taken together the following 4 functions (Z, normalizer_fn, k, q)
 * comprise the scaling function for t-digests.
 */

/**
 * C++ translation of the "k_2" version found in the reference implementation
 * available here: https://github.com/tdunning/t-digest
 */
inline double Z(double compression, double n)
{
    return (4 * log(n / compression) + 24);
}

/**
 * C++ translation of the "k_2" version found in the reference implementation
 * available here: https://github.com/tdunning/t-digest
 */
inline double normalizer_fn(double compression, double n)
{
    return (compression / Z(compression, n));
}

/**
 * C++ translation of the "k_2" version found in the reference implementation
 * available here: https://github.com/tdunning/t-digest
 */
inline double k(double q, double normalizer)
{
    const double q_min = 1e-15;
    const double q_max = 1 - q_min;
    if (q < q_min) {
        return (2 * k(q_min, normalizer));
    } else if (q > q_max) {
        return (2 * k(q_max, normalizer));
    }

    return (log(q / (1 - q)) * normalizer);
}

/**
 * C++ translation of the "k_2" version found in the reference implementation
 * available here: https://github.com/tdunning/t-digest
 */
inline double q(double k, double normalizer)
{
    double w = exp(k / normalizer);
    return (w / (1 + w));
}

/**
 * Based on the equivalent function in the reference implementation available here:
 * https://github.com/tdunning/t-digest
 */
template <typename Values, typename Weight>
void tdigest<Values, Weight>::merge()
{
    auto &inactive = (&one == active) ? two : one;

    if (buffer.values.empty()) {
        return;
    }

    auto &inputs = buffer.values;

    if (run_forward) {
        std::sort(inputs.begin(), inputs.end(), std::less<centroid_t>());

        // Update min/max values only if sorted first/last centroids are single points.
        min_val = std::min(min_val,
                           inputs.front().weight == 1 ? inputs.front().mean :
                           std::numeric_limits<Values>::max());

        max_val = std::max(max_val,
                           inputs.back().weight == 1 ? inputs.back().mean :
                           std::numeric_limits<Values>::min());

    } else {
        std::sort(inputs.begin(), inputs.end(), std::greater<centroid_t>());

        // Update min/max values only if sorted first/last centroids are single points.
        min_val = std::min(min_val,
                           inputs.back().weight == 1 ? inputs.back().mean
                           : std::numeric_limits<Values>::max());

        max_val = std::max(max_val,
                           inputs.front().weight == 1 ? inputs.front().mean
                           : std::numeric_limits<Values>::min());
    }

    const Weight new_total_weight = buffer.total_weight + active->total_weight;
    const double normalizer       = normalizer_fn(inactive.values.capacity(), new_total_weight);
    double k1                     = k(0, normalizer);
    double next_q_limit_weight    = new_total_weight * q(k1 + 1, normalizer);

    double weight_so_far = 0;
    double weight_to_add = inputs.front().weight;
    double mean_to_add   = inputs.front().mean;

    auto compress_fn = [&inactive, new_total_weight, &k1, normalizer, &next_q_limit_weight,
                        &weight_so_far, &weight_to_add, &mean_to_add](const centroid_t &current) {
        if ((weight_so_far + weight_to_add + current.weight) <= next_q_limit_weight) {
            weight_to_add += current.weight;
            assert(weight_to_add);
            mean_to_add =
              mean_to_add + (current.mean - mean_to_add) * current.weight / weight_to_add;

        } else {
            weight_so_far += weight_to_add;

            double new_q =
              static_cast<double>(weight_so_far) / static_cast<double>(new_total_weight);
            k1                  = k(new_q, normalizer);
            next_q_limit_weight = new_total_weight * q(k1 + 1, normalizer);

            if constexpr (std::is_integral<Values>::value) {
                mean_to_add = std::round(mean_to_add);
            }
            inactive.insert(mean_to_add, weight_to_add);
            mean_to_add   = current.mean;
            weight_to_add = current.weight;
        }
    };

    std::for_each(inputs.begin() + 1, inputs.end(), compress_fn);

    if (weight_to_add != 0) { inactive.insert(mean_to_add, weight_to_add); }

    if (!run_forward) { std::sort(inactive.values.begin(), inactive.values.end()); }
    run_forward = !run_forward;

    buffer.reset();

    // Seed buffer with the current t-digest in preparation for the next
    // merge.
    inputs.assign(inactive.values.begin(), inactive.values.end());

    auto new_inactive = active;
    active = &inactive;
    new_inactive->reset();
}

/**
 * XXX: replace with std::lerp when C++20 support becomes available.
 * This version adapted from libcxx, which is part of the LLVM project
 * under an "Apache 2.0 license with LLVM Exceptions." The project can be
 * found at https://github.com/llvm-mirror/libcxx
 */
inline double lerp(double a, double b, double t) noexcept
{
    if ((a <= 0 && b >= 0) || (a >= 0 && b <= 0)) return t * b + (1 - t) * a;

    if (t == 1) return b;
    const double x = a + t * (b - a);
    if ((t > 1) == (b > a))
        return b < x ? x : b;
    else
        return x < b ? x : b;
}

/**
 * Based on the equivalent function in the reference implementation available here:
 * https://github.com/tdunning/t-digest
 */
template <typename Values, typename Weight>
double tdigest<Values, Weight>::quantile(double p) const
{
    if (p < 0 || p > 100) {
        throw std::out_of_range("Requested quantile must be between 0 and 100.");
    }

    if (active->values.empty()) {
        return (0);
    } else if (active->values.size() == 1) {
        return (active->values.front().mean);
    }

    const Weight index = (p / 100) * active->total_weight;

    if (index < 1) { return (min_val); }

    // For smaller quantiles, interpolate between minimum value and the first
    // centroid.
    const auto &first = active->values.front();
    if (first.weight > 1 && index < (first.weight / 2)) {
        return (lerp(min_val, first.mean, static_cast<double>(index - 1) / (first.weight / 2 - 1)));
    }

    if (index > active->total_weight - 1) { return (max_val); }

    // For larger quantiles, interpolate between maximum value and the last
    // centroid.
    const auto &last = active->values.back();
    if (last.weight > 1 && active->total_weight - index <= last.weight / 2) {
        return (max_val
                - static_cast<double>(active->total_weight - index - 1) /
                (last.weight / 2 - 1)
                * (max_val - last.mean));
    }

    Weight weight_so_far = active->values.front().weight / 2;
    double quantile      = 0;
    auto quantile_fn     = [index, &weight_so_far, &quantile](const centroid_t &left,
                                                          const centroid_t &right) {
        Weight delta_weight = (left.weight + right.weight) / 2;
        if (weight_so_far + delta_weight > index) {
            Weight lower = index - weight_so_far;
            Weight upper = weight_so_far + delta_weight - index;

            quantile = (left.mean * upper + right.mean * lower) / (lower + upper);

            return (true);
        }

        weight_so_far += delta_weight;
        return (false);
    };

    // Even though we're using adjacent_find here, we don't actually intend to find
    // anything.  We just want to iterate over pairs of centroids until we calculate
    // the quantile.
    auto it = std::adjacent_find(active->values.begin(), active->values.end(), quantile_fn);

    // Did we fail to find a pair of bracketing centroids?
    if (it == active->values.end()) {
        // Must be between max_val and the last centroid.
        return active->values.back().mean;
    }

    return (quantile);
}

/**
 * Based on the equivalent function in the reference implementation available here:
 * https://github.com/tdunning/t-digest
 */
template <typename Values, typename Weight>
double tdigest<Values, Weight>::cumulative_distribution(Values x) const
{
    if (active->values.empty()) { return (1.0); }

    if (active->values.size() == 1) {
        if (x < min_val) { return (0); }
        if (x > max_val) { return (1.0); }
        if (x - min_val <= (max_val - min_val)) { return (0.5); }
        return ((x - min_val) / (max_val - min_val));
    }

    // From here on out we divide by active->total_weight in multiple places
    // along several code paths.
    // Let's make sure we're not going to divide by zero.
    assert(active->total_weight);

    // Is x at one of the extremes?
    if (x < active->values.front().mean) {
        const auto &first = active->values.front();
        if (first.mean - min_val > 0) {
            return (lerp(1, first.weight / 2 - 1, (x - min_val) / (first.mean - min_val)) / active->total_weight);
        }
        return (0);
    }
    if (x > active->values.back().mean) {
        const auto &last = active->values.back();
        if (max_val - last.mean > 0) {
            return (1 - (lerp(1, last.weight / 2 - 1, (max_val - x) / (max_val - last.mean)  / active->total_weight)));
        }
        return (1.0);
    }

    Weight weight_so_far = 0;
    double cdf           = 0;
    auto cdf_fn          = [x, &weight_so_far, &cdf, total_weight = active->total_weight](
                    const centroid_t &left, const centroid_t &right) {
        assert(total_weight);
        if (left.mean <= x && x < right.mean) {
            // x is bracketed between left and right.

            Weight delta_weight = (right.weight + left.weight) / static_cast<Weight>(2);
            double base         = weight_so_far + (left.weight / static_cast<Weight>(2));

            cdf = lerp(base, base + delta_weight,
                       static_cast<double>((x - left.mean)) / (right.mean - left.mean))
                  / total_weight;
            return (true);
        }

        weight_so_far += left.weight;
        return (false);
    };

    auto it = std::adjacent_find(active->values.begin(), active->values.end(), cdf_fn);

    // Did we fail to find a pair of bracketing centroids?
    if (it == active->values.end()) {
        // Might be between max_val and the last centroid.
        if (x == active->values.back().mean) {
            return ((1 - 0.5 / active->total_weight));
        }
    }

    return (cdf);
}

}  // namespace digestible

#endif
