/**
 * Copyright (c) 2015, Timothy Stack
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
 * @file time-extension-functions.cc
 */

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <string.h>

#include "base/attr_line.builder.hh"
#include "base/date_time_scanner.hh"
#include "base/humanize.time.hh"
#include "base/lnav.tz.hh"
#include "base/math_util.hh"
#include "base/relative_time.hh"
#include "config.h"
#include "digestible/digestible.h"
#include "ptimec.hh"
#include "sql_util.hh"
#include "vtab_module.hh"
#include "vtab_module_json.hh"
#include "yajlpp/yajlpp.hh"

/**
 * Scan a timestamp passed to a SQL function.  The arguments are unrelated to
 * each other, so a locked format that only matches the start of one is not
 * trusted: the format is rediscovered to find one that matches all of it.
 */
static const char*
scan_sql_timestamp(date_time_scanner& dts,
                   const char* str,
                   size_t len,
                   exttm* tm_out,
                   timeval& tv_out)
{
    const auto* retval
        = dts.scan_relocking(str, len, nullptr, tm_out, tv_out, false);

    if (retval != nullptr && retval != str + len) {
        // The unlocked scan still tries the format that matched the start,
        // so it cannot do worse than that.
        dts.unlock();
        retval = dts.scan(str, len, nullptr, tm_out, tv_out, false);
    }

    return retval;
}

static std::optional<text_auto_buffer>
timeslice(sqlite3_value* time_in, std::optional<string_fragment> slice_in_opt)
{
    thread_local date_time_scanner dts;
    thread_local struct {
        std::string c_slice_str;
        relative_time c_rel_time;
    } cache;
    const auto slice_in = slice_in_opt.value_or("15m"_frag);

    if (slice_in.empty()) {
        throw sqlite_func_error("no time slice value given");
    }

    if (slice_in != cache.c_slice_str.c_str()) {
        auto parse_res = relative_time::from_str(slice_in);
        if (parse_res.isErr()) {
            throw sqlite_func_error(
                "unable to parse time slice value: {} -- {}",
                slice_in,
                parse_res.unwrapErr().pe_msg);
        }

        cache.c_rel_time = parse_res.unwrap();
        if (cache.c_rel_time.empty()) {
            throw sqlite_func_error("could not determine a time slice from: {}",
                                    slice_in);
        }

        cache.c_slice_str = slice_in.to_string();
    }

    struct timeval tv;
    struct exttm tm;

    switch (sqlite3_value_type(time_in)) {
        case SQLITE_BLOB:
        case SQLITE3_TEXT: {
            const char* time_in_str
                = reinterpret_cast<const char*>(sqlite3_value_text(time_in));

            if (scan_sql_timestamp(
                    dts, time_in_str, strlen(time_in_str), &tm, tv)
                == nullptr)
            {
                throw sqlite_func_error("unable to parse time value -- {}",
                                        time_in_str);
            }
            break;
        }
        case SQLITE_INTEGER: {
            // Round toward negative infinity so the fraction of a time
            // before the epoch is not negative.
            auto msecs = sqlite3_value_int64(time_in);
            auto secs = msecs / 1000;
            auto rem_msecs = msecs % 1000;
            if (rem_msecs < 0) {
                secs -= 1;
                rem_msecs += 1000;
            }

            tv.tv_sec = secs;
            if (gmtime_r(&tv.tv_sec, &tm.et_tm) == nullptr) {
                throw sqlite_func_error("time value is out of range -- {}",
                                        msecs);
            }
            tm.et_nsec = rem_msecs * 1000000;
            break;
        }
        case SQLITE_FLOAT: {
            auto secs = sqlite3_value_double(time_in);
            auto integ = std::floor(secs);

            if (!std::isfinite(secs)
                || integ < (double) std::numeric_limits<time_t>::min()
                || integ >= (double) std::numeric_limits<time_t>::max())
            {
                throw sqlite_func_error("time value is out of range -- {}",
                                        secs);
            }
            tv.tv_sec = integ;
            if (gmtime_r(&tv.tv_sec, &tm.et_tm) == nullptr) {
                throw sqlite_func_error("time value is out of range -- {}",
                                        secs);
            }
            tm.et_nsec = std::floor((secs - integ) * 1000000000.0);
            break;
        }
        case SQLITE_NULL: {
            return std::nullopt;
        }
    }

    // Times are only converted and formatted for the years 1970 to 9999.
    auto year = tm.et_tm.tm_year + 1900;
    if (year < 1970 || year > 9999) {
        throw sqlite_func_error(
            "time value is outside of the supported years 1970-9999 -- {}",
            year);
    }

    auto win_start_opt = cache.c_rel_time.window_start(tm);

    if (!win_start_opt) {
        return std::nullopt;
    }

    auto win_start = *win_start_opt;
    auto win_year = win_start.et_tm.tm_year + 1900;
    if (win_year < 1970 || win_year > 9999) {
        throw sqlite_func_error(
            "time slice starts outside of the supported years 1970-9999");
    }

    auto ts = auto_buffer::alloc(64);
    auto actual_length
        = sql_strftime(ts.in(), ts.capacity(), win_start.to_timeval());

    ts.resize(actual_length);
    return text_auto_buffer{std::move(ts)};
}

static std::optional<double>
sql_timediff(string_fragment time1, string_fragment time2)
{
    struct timeval tv1, tv2, retval;
    date_time_scanner dts1, dts2;
    auto parse_res1 = relative_time::from_str(time1);

    if (parse_res1.isOk()) {
        tv1 = parse_res1.unwrap().adjust_now().to_timeval();
    } else if (!dts1.convert_to_timeval(
                   time1.data(), time1.length(), nullptr, tv1))
    {
        return std::nullopt;
    }

    auto parse_res2 = relative_time::from_str(time2);
    if (parse_res2.isOk()) {
        tv2 = parse_res2.unwrap().adjust_now().to_timeval();
    } else if (!dts2.convert_to_timeval(
                   time2.data(), time2.length(), nullptr, tv2))
    {
        return std::nullopt;
    }

    timersub(&tv1, &tv2, &retval);

    return (double) retval.tv_sec + (double) retval.tv_usec / 1000000.0;
}

static std::string
sql_humanize_duration(double value)
{
    static const auto MAX_SECS
        = std::chrono::duration<double>(std::chrono::nanoseconds::max())
              .count();

    if (!std::isfinite(value) || std::abs(value) >= MAX_SECS) {
        throw sqlite_func_error("duration is out of range -- {}", value);
    }

    return humanize::time::duration::from(std::chrono::duration<double>{value})
        .with_compact(false)
        .to_string();
}

static std::optional<std::string>
sql_timezone(string_fragment tz_str, string_fragment ts_str)
{
    thread_local date_time_scanner dts;
    struct timeval tv;
    exttm tm1;

    auto scan_end
        = scan_sql_timestamp(dts, ts_str.data(), ts_str.length(), &tm1, tv);
    if (scan_end == nullptr) {
        auto um = lnav::console::user_message::error(
            attr_line_t("unrecognized timestamp: ").append(ts_str));
        throw um;
    }
    size_t matched_size = scan_end - ts_str.data();
    auto ts_remaining = ts_str.substr(matched_size);
    if (!ts_remaining.empty()) {
        auto um
            = lnav::console::user_message::error(
                  attr_line_t("invalid timestamp: ").append(ts_str))
                  .with_reason(attr_line_t("the leading part of the timestamp "
                                           "was matched, however, the trailing "
                                           "text ")
                                   .append_quoted(ts_remaining)
                                   .append(" was not"))
                  .with_note(attr_line_t("input matched time format ")
                                 .append_quoted(
                                     PTIMEC_FORMATS[dts.dts_fmt_lock].pf_fmt))
                  .with_help("fix the timestamp or remove the trailing text")
                  .move();

        auto ts_attr = attr_line_t().append(ts_str).move();
        attr_line_builder alb(ts_attr);

        alb.append("\n").append(matched_size, ' ');
        {
            auto attr_guard = alb.with_attr(VC_ROLE.value(role_t::VCR_COMMENT));

            alb.append("^");
            if (ts_remaining.length() > 1) {
                if (ts_remaining.length() > 2) {
                    alb.append(ts_remaining.length() - 2, '-');
                }
                alb.append("^");
            }
            alb.append(" unrecognized input");
        }
        um.with_note(ts_attr);
        throw um;
    }

    thread_local std::string last_tz;
    thread_local const date::time_zone* tz;

    if (tz_str != last_tz) {
        auto locate_res = lnav::locate_zone(tz_str);
        if (locate_res.isErr()) {
            throw locate_res.unwrapErr();
        }
        tz = locate_res.unwrap();
        last_tz = tz_str.to_string();
    }

    auto stime = std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::system_clock::from_time_t(tv.tv_sec));
    stime += std::chrono::microseconds{tv.tv_usec};
    auto ztime = date::make_zoned(tz, stime);

    auto retval = date::format("%FT%T%z", ztime);

    return retval;
}

/**
 * time_buckets(t_msecs, value, max_buckets [, lo_msecs, hi_msecs]): the values
 * over time, in buckets a power of two of milliseconds wide, the narrowest
 * that fits the span (lo to hi, or the group's own first to last time) in
 * max_buckets.  Several groups given the same span get the same buckets, and
 * rows outside of the span are skipped.  max_buckets, lo, and hi are read
 * from the first row.  Each bucket is an object ({"start", "stop", "count",
 * "p10", "median", "p90", "max"}), so members can be added without breaking
 * readers.
 *
 * The rows are bucketed as they come in, widening the buckets as the span
 * grows.  Since the widths are powers of two, doubling the width merges each
 * pair of neighboring buckets.  A bucket keeps its values for exact
 * percentiles until it has more than EXACT_LIMIT, then estimates them with a
 * t-digest, so the memory used is bounded by max_buckets and not the number
 * of rows.
 */
struct time_bucket {
    /**
     * The most values kept for exact percentiles.  They take less memory
     * than a t-digest, which reserves about 800 centroids of 16 bytes.
     */
    static constexpr size_t EXACT_LIMIT = 1024;
    /** The compression factor for the t-digest. */
    static constexpr size_t TDIGEST_SIZE = 200;

    int64_t tb_count{0};
    int64_t tb_max_int{std::numeric_limits<int64_t>::min()};
    double tb_max_double{-std::numeric_limits<double>::infinity()};
    /** The values, while every value is an integer and there are few. */
    std::vector<int64_t> tb_ints;
    /** The values, once one is a double, while there are few. */
    std::vector<double> tb_doubles;
    /**
     * Held by pointer, since moving a tdigest allocates a new one, and
     * buckets are moved whenever they are widened.
     */
    std::unique_ptr<digestible::tdigest<double>> tb_digest;

    void add_int(int64_t v)
    {
        this->tb_count += 1;
        this->tb_max_int = std::max(this->tb_max_int, v);
        this->tb_max_double
            = std::max(this->tb_max_double, static_cast<double>(v));
        if (this->tb_digest) {
            this->tb_digest->insert(static_cast<double>(v));
        } else {
            this->tb_ints.push_back(v);
            this->spill();
        }
    }

    void add_double(double v)
    {
        this->tb_count += 1;
        this->tb_max_double = std::max(this->tb_max_double, v);
        if (this->tb_digest) {
            this->tb_digest->insert(v);
        } else {
            this->tb_doubles.push_back(v);
            this->spill();
        }
    }

    void to_doubles()
    {
        this->tb_doubles.insert(
            this->tb_doubles.end(), this->tb_ints.begin(), this->tb_ints.end());
        this->tb_ints = std::vector<int64_t>();
    }

    void merge(time_bucket& other)
    {
        this->tb_count += other.tb_count;
        this->tb_max_int = std::max(this->tb_max_int, other.tb_max_int);
        this->tb_max_double
            = std::max(this->tb_max_double, other.tb_max_double);
        if (other.tb_digest) {
            if (this->tb_digest) {
                this->tb_digest->insert(*other.tb_digest);
            } else {
                this->tb_digest = std::move(other.tb_digest);
            }
        }
        this->tb_ints.insert(
            this->tb_ints.end(), other.tb_ints.begin(), other.tb_ints.end());
        this->tb_doubles.insert(this->tb_doubles.end(),
                                other.tb_doubles.begin(),
                                other.tb_doubles.end());
        this->spill();
    }

    /**
     * Moves the exact values into the t-digest, once there are too many or
     * there is already a t-digest.
     */
    void spill()
    {
        if (!this->tb_digest
            && this->tb_ints.size() + this->tb_doubles.size() <= EXACT_LIMIT)
        {
            return;
        }
        if (!this->tb_digest) {
            this->tb_digest
                = std::make_unique<digestible::tdigest<double>>(TDIGEST_SIZE);
        }
        for (const auto v : this->tb_ints) {
            this->tb_digest->insert(static_cast<double>(v));
        }
        for (const auto v : this->tb_doubles) {
            this->tb_digest->insert(v);
        }
        // Assigning {} would clear them but keep their capacity.
        this->tb_ints = std::vector<int64_t>();
        this->tb_doubles = std::vector<double>();
    }

    /** Readies the values for gen_percentile(). */
    void finish()
    {
        if (this->tb_digest) {
            this->tb_digest->merge();
        } else {
            std::sort(this->tb_ints.begin(), this->tb_ints.end());
            std::sort(this->tb_doubles.begin(), this->tb_doubles.end());
        }
    }

    void gen_percentile(yajlpp_generator& gen, double p, bool all_ints) const
    {
        if (this->tb_digest) {
            const auto q = this->tb_digest->quantile(p);
            if (all_ints) {
                gen(round_to_int64(q));
            } else {
                gen(q);
            }
        } else if (all_ints) {
            gen(this->tb_ints[lnav::math::nearest_rank(p, this->tb_ints.size())
                              - 1]);
        } else {
            gen(this->tb_doubles
                    [lnav::math::nearest_rank(p, this->tb_doubles.size()) - 1]);
        }
    }

    void gen_max(yajlpp_generator& gen, bool all_ints) const
    {
        if (all_ints) {
            gen(this->tb_max_int);
        } else {
            gen(this->tb_max_double);
        }
    }

    static int64_t round_to_int64(double d)
    {
        // 2^63 is the nearest double to INT64_MAX, and is out of range.
        if (d >= 9223372036854775808.0) {
            return std::numeric_limits<int64_t>::max();
        }
        if (d <= -9223372036854775808.0) {
            return std::numeric_limits<int64_t>::min();
        }
        return std::llround(d);
    }
};

struct time_buckets_context {
    using bucket_map = std::map<int64_t, time_bucket>;

    bool tbc_initialized{true};
    bool tbc_all_ints{true};
    int64_t tbc_max_buckets{0};
    std::optional<int64_t> tbc_lo;
    std::optional<int64_t> tbc_hi;
    std::optional<int64_t> tbc_min_time;
    std::optional<int64_t> tbc_max_time;
    /** The log2 of the bucket width. */
    int tbc_shift{0};
    /** The buckets, by their start time shifted right by tbc_shift. */
    bucket_map tbc_buckets;
    /** The last bucket added to, since rows usually come in time order. */
    bucket_map::iterator tbc_last{tbc_buckets.end()};

    void add_time(int64_t t)
    {
        if (!this->tbc_min_time || t < *this->tbc_min_time) {
            this->tbc_min_time = t;
        }
        if (!this->tbc_max_time || t > *this->tbc_max_time) {
            this->tbc_max_time = t;
        }

        const auto lo = this->tbc_lo.value_or(*this->tbc_min_time);
        const auto hi = this->tbc_hi.value_or(*this->tbc_max_time);
        // hi >= lo, so the difference fits in a uint64_t even when the
        // int64_t one would overflow.  By a shift of 63 every key is -1 or
        // 0, which fits since max_buckets is at least 2.
        auto key_span = [lo, hi](int shift) {
            return static_cast<uint64_t>(hi >> shift)
                - static_cast<uint64_t>(lo >> shift);
        };
        const auto max_buckets = static_cast<uint64_t>(this->tbc_max_buckets);
        auto shift = this->tbc_shift;
        while (shift < 63 && key_span(shift) >= max_buckets) {
            shift += 1;
        }
        if (shift == this->tbc_shift) {
            return;
        }

        const auto widen_by = shift - this->tbc_shift;
        bucket_map wider;
        for (auto& [key, bucket] : this->tbc_buckets) {
            // try_emplace() only moves from the bucket when it inserts.
            auto [iter, inserted]
                = wider.try_emplace(key >> widen_by, std::move(bucket));
            if (!inserted) {
                iter->second.merge(bucket);
            }
        }
        this->tbc_buckets = std::move(wider);
        this->tbc_shift = shift;
        this->tbc_last = this->tbc_buckets.end();
    }

    time_bucket& bucket_for(int64_t t)
    {
        const auto key = t >> this->tbc_shift;
        if (this->tbc_last == this->tbc_buckets.end()
            || this->tbc_last->first != key)
        {
            this->tbc_last = this->tbc_buckets.try_emplace(key).first;
        }
        return this->tbc_last->second;
    }
};

static bool
is_sql_number(sqlite3_value* val)
{
    auto type = sqlite3_value_numeric_type(val);

    return type == SQLITE_INTEGER || type == SQLITE_FLOAT;
}

static void
time_buckets_step(sqlite3_context* context, int argc, sqlite3_value** argv)
{
    auto* tbc = (time_buckets_context*) sqlite3_aggregate_context(
        context, sizeof(time_buckets_context));
    if (tbc == nullptr) {
        sqlite3_result_error_nomem(context);
        return;
    }
    if (!tbc->tbc_initialized) {
        new (tbc) time_buckets_context;
    }

    if (argc != 3 && argc != 5) {
        sqlite3_result_error(
            context, "time_buckets: expecting 3 or 5 arguments", -1);
        return;
    }
    if (tbc->tbc_max_buckets == 0) {
        auto max_buckets = sqlite3_value_int64(argv[2]);
        if (sqlite3_value_numeric_type(argv[2]) != SQLITE_INTEGER
            || max_buckets < 2)
        {
            sqlite3_result_error(
                context,
                "time_buckets: max_buckets must be an integer of at least 2",
                -1);
            return;
        }
        if (argc == 5) {
            if (sqlite3_value_type(argv[3]) != SQLITE_NULL) {
                if (!is_sql_number(argv[3])) {
                    sqlite3_result_error(
                        context,
                        "time_buckets: lo must be a time in milliseconds",
                        -1);
                    return;
                }
                tbc->tbc_lo = sqlite3_value_int64(argv[3]);
            }
            if (sqlite3_value_type(argv[4]) != SQLITE_NULL) {
                if (!is_sql_number(argv[4])) {
                    sqlite3_result_error(
                        context,
                        "time_buckets: hi must be a time in milliseconds",
                        -1);
                    return;
                }
                tbc->tbc_hi = sqlite3_value_int64(argv[4]);
            }
            if (tbc->tbc_lo && tbc->tbc_hi && *tbc->tbc_lo > *tbc->tbc_hi) {
                sqlite3_result_error(
                    context, "time_buckets: lo must not be after hi", -1);
                return;
            }
        }
        tbc->tbc_max_buckets = max_buckets;
    }

    if (!is_sql_number(argv[0])) {
        return;
    }
    const auto t = sqlite3_value_int64(argv[0]);
    if ((tbc->tbc_lo && t < *tbc->tbc_lo) || (tbc->tbc_hi && t > *tbc->tbc_hi))
    {
        return;
    }

    const auto v_type = sqlite3_value_numeric_type(argv[1]);
    if (v_type != SQLITE_INTEGER && v_type != SQLITE_FLOAT) {
        return;
    }
    if (v_type == SQLITE_FLOAT && tbc->tbc_all_ints) {
        tbc->tbc_all_ints = false;
        for (auto& [key, bucket] : tbc->tbc_buckets) {
            bucket.to_doubles();
        }
    }

    tbc->add_time(t);
    auto& bucket = tbc->bucket_for(t);
    if (tbc->tbc_all_ints) {
        bucket.add_int(sqlite3_value_int64(argv[1]));
    } else {
        bucket.add_double(sqlite3_value_double(argv[1]));
    }
}

static void
time_buckets_final(sqlite3_context* context)
{
    auto* tbc = (time_buckets_context*) sqlite3_aggregate_context(context, 0);
    if (tbc == nullptr || !tbc->tbc_initialized) {
        sqlite3_result_null(context);
        return;
    }

    if (tbc->tbc_buckets.empty()) {
        sqlite3_result_null(context);
    } else {
        const auto shift = tbc->tbc_shift;
        // Shift as unsigned, since left-shifting a negative key is undefined.
        auto key_time = [shift](int64_t key) {
            return static_cast<int64_t>(static_cast<uint64_t>(key) << shift);
        };
        const auto max_key = std::numeric_limits<int64_t>::max() >> shift;
        const auto all_ints = tbc->tbc_all_ints;

        yajlpp_gen gen;
        yajl_gen_config(gen, yajl_gen_beautify, false);
        {
            yajlpp_array buckets(gen);

            for (auto& [key, bucket] : tbc->tbc_buckets) {
                bucket.finish();

                yajlpp_map entry(gen);
                entry.gen("start");
                entry.gen(key_time(key));
                entry.gen("stop");
                entry.gen(key == max_key
                                   ? std::numeric_limits<int64_t>::max()
                                   : key_time(key + 1));
                entry.gen("count");
                entry.gen(bucket.tb_count);
                entry.gen("p10");
                bucket.gen_percentile(entry.gen, 10, all_ints);
                entry.gen("median");
                bucket.gen_percentile(entry.gen, 50, all_ints);
                entry.gen("p90");
                bucket.gen_percentile(entry.gen, 90, all_ints);
                entry.gen("max");
                bucket.gen_max(entry.gen, all_ints);
            }
        }

        to_sqlite(context, json_string(gen));
    }

    tbc->~time_buckets_context();
}

int
time_extension_functions(struct FuncDef** basic_funcs,
                         struct FuncDefAgg** agg_funcs)
{
    static struct FuncDef time_funcs[] = {
        sqlite_func_adapter<decltype(&timeslice), timeslice>::builder(
            help_text(
                "timeslice",
                "Return the start of the slice of time that the given "
                "timestamp falls in.  "
                "If the time falls outside of the slice, NULL is returned.")
                .sql_function()
                .with_prql_path({"time", "slice"})
                .with_parameter({
                    "time",
                    "The timestamp to get the time slice for.  A number is "
                    "taken as time since the epoch: an integer in "
                    "milliseconds and a real in seconds.",
                })
                .with_parameter({"slice", "The size of the time slices"})
                .with_tags({"datetime"})
                .with_example({
                    "To get the timestamp rounded down to the start of the "
                    "ten minute slice",
                    "SELECT timeslice('2017-01-01T05:05:00', '10m')",
                })
                .with_example({
                    "To group log messages into five minute buckets and count "
                    "them",
                    "SELECT timeslice(log_time_msecs, '5m') AS slice, "
                    "count(1)\n    FROM lnav_example_log GROUP BY slice",
                })
                .with_example({
                    "To group log messages by those before 4:30am and after",
                    "SELECT timeslice(log_time_msecs, 'before 4:30am') AS "
                    "slice, count(1) FROM lnav_example_log GROUP BY slice",
                })),

        sqlite_func_adapter<decltype(&sql_timediff), sql_timediff>::builder(
            help_text(
                "timediff",
                "Compute the difference between two timestamps in seconds")
                .sql_function()
                .with_prql_path({"time", "diff"})
                .with_parameter({"time1", "The first timestamp"})
                .with_parameter(
                    {"time2", "The timestamp to subtract from the first"})
                .with_tags({"datetime"})
                .with_example({
                    "To get the difference between two timestamps",
                    "SELECT timediff('2017-02-03T04:05:06', "
                    "'2017-02-03T04:05:00')",
                })
                .with_example({
                    "To get the difference between relative timestamps",
                    "SELECT timediff('today', 'yesterday')",
                })),

        sqlite_func_adapter<decltype(&sql_humanize_duration),
                            sql_humanize_duration>::
            builder(
                help_text("humanize_duration",
                          "Format the given seconds value as an abbreviated "
                          "duration string")
                    .sql_function()
                    .with_prql_path({"humanize", "duration"})
                    .with_parameter({"secs", "The duration in seconds"})
                    .with_tags({"datetime", "string"})
                    .with_example({
                        "To format a duration",
                        "SELECT humanize_duration(15 * 60)",
                    })
                    .with_example({
                        "To format a sub-second value",
                        "SELECT humanize_duration(1.5)",
                    })),

        sqlite_func_adapter<decltype(&sql_timezone), sql_timezone>::builder(
            help_text("timezone", "Convert a timestamp to the given timezone")
                .sql_function()
                .with_prql_path({"time", "to_zone"})
                .with_parameter({"tz", "The target timezone"})
                .with_parameter({"ts", "The source timestamp"})
                .with_tags({"datetime", "string"})
                .with_example({
                    "To convert a time to America/Los_Angeles",
                    "SELECT timezone('America/Los_Angeles', "
                    "'2022-03-02T10:00')",
                })),

        {nullptr},
    };

    static struct FuncDefAgg time_agg_funcs[] = {
        {
            "time_buckets",
            -1,
            SQLITE_UTF8
#ifdef SQLITE_RESULT_SUBTYPE
                | SQLITE_RESULT_SUBTYPE
#endif
            ,
            0,
            time_buckets_step,
            time_buckets_final,
            help_text("time_buckets")
                .sql_agg_function()
                .with_summary(
                    "Returns the values over time as a JSON array with an "
                    "object for each time bucket that has values, in time "
                    "order: its start and stop in milliseconds, the count, "
                    "and the p10, median, p90, and max of the values.  The "
                    "percentiles use the nearest-rank method for a bucket "
                    "with up to 1024 values and are estimated with a "
                    "t-digest past that, so memory does not grow with the "
                    "number of rows.  The buckets "
                    "are a power of two of milliseconds wide, the narrowest "
                    "that fits the time span in max_buckets buckets.  The "
                    "max_buckets, lo, and hi arguments are read from the "
                    "first row")
                .with_parameter(
                    {"t", "The time, in milliseconds (e.g. log_time_msecs)"})
                .with_parameter({"value", "The numeric value"})
                .with_parameter(
                    {"max_buckets",
                     "The most buckets to use for the span, at least 2"})
                .with_parameter(
                    help_text("lo",
                              "The start of the span, in milliseconds, so "
                              "several groups get the same buckets.  Rows "
                              "before it are skipped.  Defaults to the "
                              "group's first time")
                        .optional())
                .with_parameter(help_text("hi",
                                          "The end of the span, in "
                                          "milliseconds.  Rows after it are "
                                          "skipped.  Defaults to the group's "
                                          "last time")
                                    .optional())
                .with_tags({"datetime"})
                .with_example({
                    "To get the values over time in at most 4 buckets",
                    "SELECT time_buckets(column1, column2, 4) FROM (VALUES "
                    "(0, 1), (1, 5), (7, 3), (9, 2))",
                }),
        },

        {nullptr},
    };

    *basic_funcs = time_funcs;
    *agg_funcs = time_agg_funcs;

    return SQLITE_OK;
}
