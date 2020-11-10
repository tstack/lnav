/**
 * Copyright (c) 2007-2012, Timothy Stack
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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file lnav_util.hh
 *
 * Dumping ground for useful functions with no other home.
 */

#ifndef lnav_util_hh
#define lnav_util_hh

#include <time.h>
#include <sys/time.h>
#include <poll.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/resource.h>

#include "spookyhash/SpookyV2.h"

#include <future>
#include <string>
#include <vector>
#include <sstream>
#include <numeric>
#include <algorithm>
#include <type_traits>

#include "ptimec.hh"
#include "byte_array.hh"
#include "optional.hpp"
#include "base/result.h"
#include "fmt/format.h"
#include "ghc/filesystem.hpp"

#undef rounddown

/**
 * Round down a number based on a given granularity.
 *
 * @param
 * @param step The granularity.
 */
template<typename Size, typename Step>
inline int rounddown(Size size, Step step)
{
    return size - (size % step);
}

inline int rounddown_offset(size_t size, int step, int offset)
{
    return size - ((size - offset) % step);
}

inline size_t roundup_size(size_t size, int step)
{
    size_t retval = size + step;

    retval -= (retval % step);

    return retval;
}

inline int32_t read_le32(const unsigned char *data)
{
    return (
        (data[0] <<  0) |
        (data[1] <<  8) |
        (data[2] << 16) |
        (data[3] << 24));
}

inline time_t day_num(time_t ti)
{
    return ti / (24 * 60 * 60);
}

inline time_t hour_num(time_t ti)
{
    return ti / (60 * 60);
}

std::string time_ago(time_t last_time, bool convert_local = false);

std::string precise_time_ago(const struct timeval &tv, bool convert_local = false);

typedef int64_t mstime_t;

inline mstime_t getmstime() {
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL;
}

#if SIZEOF_OFF_T == 8
#define FORMAT_OFF_T    "%qd"
#elif SIZEOF_OFF_T == 4
#define FORMAT_OFF_T    "%ld"
#else
#error "off_t has unhandled size..."
#endif

struct hash_updater {
    hash_updater(SpookyHash *context) : su_context(context) { };

    void operator()(const std::string &str)
    {
        this->su_context->Update(str.c_str(), str.length());
    }

    SpookyHash *su_context;
};

std::string hash_string(const std::string &str);

std::string hash_bytes(const char *str1, size_t s1len, ...);

template<typename UnaryFunction, typename Member>
struct object_field_t {
    object_field_t(UnaryFunction &func, Member &mem)
        : of_func(func), of_mem(mem) {};

    template<typename Object>
    void operator()(Object obj)
    {
        this->of_func(obj.*(this->of_mem));
    };

    UnaryFunction &of_func;
    Member         of_mem;
};

template<typename UnaryFunction, typename Member>
object_field_t<UnaryFunction, Member> object_field(UnaryFunction &func,
                                                   Member mem)
{
    return object_field_t<UnaryFunction, Member>(func, mem);
}

bool change_to_parent_dir();

void split_ws(const std::string &str, std::vector<std::string> &toks_out);

enum class file_format_t {
    FF_UNKNOWN,
    FF_SQLITE_DB,
    FF_ARCHIVE,
};

file_format_t detect_file_format(const ghc::filesystem::path& filename);

template<>
struct fmt::formatter<file_format_t> : fmt::formatter<fmt::string_view> {
    template<typename FormatContext>
    auto format(file_format_t ff, FormatContext& ctx) {
        fmt::string_view name = "unknown";
        switch (ff) {
            case file_format_t::FF_SQLITE_DB:
                name = "SQLite Database";
                break;
            case file_format_t::FF_ARCHIVE:
                name = "Archive";
                break;
            default:
                break;
        }
        return fmt::formatter<fmt::string_view>::format(name, ctx);
    }
};

bool next_format(const char * const fmt[], int &index, int &locked_index);

namespace std {
    inline string to_string(const string &s) { return s; }
    inline string to_string(const char *s) { return s; }
}

inline bool is_glob(const char *fn)
{
    return (strchr(fn, '*') != nullptr ||
            strchr(fn, '?') != nullptr ||
            strchr(fn, '[') != nullptr);
};

bool is_url(const char *fn);

std::string build_path(const std::vector<ghc::filesystem::path> &paths);

bool read_file(const ghc::filesystem::path &path, std::string &out);

/**
 * Convert the time stored in a 'tm' struct into epoch time.
 *
 * @param t The 'tm' structure to convert to epoch time.
 * @return The given time in seconds since the epoch.
 */
time_t tm2sec(const struct tm *t);

inline
time_t convert_log_time_to_local(time_t value) {
    struct tm tm;

    localtime_r(&value, &tm);
#ifdef HAVE_STRUCT_TM_TM_ZONE
    tm.tm_zone = NULL;
#endif
    tm.tm_isdst = 0;
    return tm2sec(&tm);
}

struct tm *secs2tm(time_t *tim_p, struct tm *res);

extern const char *std_time_fmt[];

inline
bool operator<(const struct timeval &left, time_t right) {
    return left.tv_sec < right;
};

inline
bool operator<(time_t left, const struct timeval &right) {
    return left < right.tv_sec;
};

inline
bool operator<(const struct timeval &left, const struct timeval &right) {
    return left.tv_sec < right.tv_sec ||
        ((left.tv_sec == right.tv_sec) && (left.tv_usec < right.tv_usec));
};

inline
bool operator!=(const struct timeval &left, const struct timeval &right) {
    return left.tv_sec != right.tv_sec ||
           left.tv_usec != right.tv_usec;
};

struct date_time_scanner {
    date_time_scanner() : dts_keep_base_tz(false),
                          dts_local_time(false),
                          dts_local_offset_cache(0),
                          dts_local_offset_valid(0),
                          dts_local_offset_expiry(0) {
        this->clear();
    };

    void clear() {
        this->dts_base_time = 0;
        memset(&this->dts_base_tm, 0, sizeof(this->dts_base_tm));
        this->dts_fmt_lock = -1;
        this->dts_fmt_len = -1;
    };

    void unlock() {
        this->dts_fmt_lock = -1;
        this->dts_fmt_len = -1;
    }

    void set_base_time(time_t base_time) {
        this->dts_base_time = base_time;
        localtime_r(&base_time, &this->dts_base_tm.et_tm);
    };

    /**
     * Convert a timestamp to local time.
     *
     * Calling localtime_r is slow since it wants to lookup the timezone on
     * every call, so we cache the result and only call it again if the
     * requested time falls outside of a fifteen minute range.
     */
    void to_localtime(time_t t, struct exttm &tm_out) {
        if (t < (24 * 60 * 60)) {
            // Don't convert and risk going past the epoch.
            return;
        }

        if (t < this->dts_local_offset_valid ||
                t >= this->dts_local_offset_expiry) {
            time_t new_gmt;

            localtime_r(&t, &tm_out.et_tm);
#ifdef HAVE_STRUCT_TM_TM_ZONE
            tm_out.et_tm.tm_zone = NULL;
#endif
            tm_out.et_tm.tm_isdst = 0;

            new_gmt = tm2sec(&tm_out.et_tm);
            this->dts_local_offset_cache = t - new_gmt;
            this->dts_local_offset_valid = t;
            this->dts_local_offset_expiry = t + (EXPIRE_TIME - 1);
            this->dts_local_offset_expiry -=
                    this->dts_local_offset_expiry % EXPIRE_TIME;
        }
        else {
            time_t adjust_gmt = t - this->dts_local_offset_cache;
            gmtime_r(&adjust_gmt, &tm_out.et_tm);
        }
    };

    bool dts_keep_base_tz;
    bool dts_local_time;
    time_t dts_base_time;
    struct exttm dts_base_tm;
    int dts_fmt_lock;
    int dts_fmt_len;
    time_t dts_local_offset_cache;
    time_t dts_local_offset_valid;
    time_t dts_local_offset_expiry;

    static const int EXPIRE_TIME = 15 * 60;

    const char *scan(const char *time_src,
                     size_t time_len,
                     const char * const time_fmt[],
                     struct exttm *tm_out,
                     struct timeval &tv_out,
                     bool convert_local = true);

    size_t ftime(char *dst, size_t len, const struct exttm &tm) {
        off_t off = 0;

        PTIMEC_FORMATS[this->dts_fmt_lock].pf_ffunc(dst, off, len, tm);

        return (size_t) off;
    };

    bool convert_to_timeval(const char *time_src,
                            ssize_t time_len,
                            const char * const time_fmt[],
                            struct timeval &tv_out) {
        struct exttm tm;

        if (time_len == -1) {
            time_len = strlen(time_src);
        }
        if (this->scan(time_src, time_len, time_fmt, &tm, tv_out) != NULL) {
            return true;
        }
        return false;
    };

    bool convert_to_timeval(const std::string &time_src,
                            struct timeval &tv_out) {
        struct exttm tm;

        if (this->scan(time_src.c_str(), time_src.size(),
                       NULL, &tm, tv_out) != NULL) {
            return true;
        }
        return false;
    }
};

template<typename T>
size_t strtonum(T &num_out, const char *data, size_t len);

inline bool pollfd_ready(const std::vector<struct pollfd> &pollfds, int fd,
                         short events = POLLIN | POLLHUP)
{
    return std::any_of(pollfds.begin(), pollfds.end(),
                       [fd, events](const auto &entry) {
                           return entry.fd == fd && entry.revents & events;
                       });
};

inline void rusagesub(const struct rusage &left, const struct rusage &right, struct rusage &diff_out)
{
    timersub(&left.ru_utime, &right.ru_utime, &diff_out.ru_utime);
    timersub(&left.ru_stime, &right.ru_stime, &diff_out.ru_stime);
    diff_out.ru_maxrss = left.ru_maxrss - right.ru_maxrss;
    diff_out.ru_ixrss = left.ru_ixrss - right.ru_ixrss;
    diff_out.ru_idrss = left.ru_idrss - right.ru_idrss;
    diff_out.ru_isrss = left.ru_isrss - right.ru_isrss;
    diff_out.ru_minflt = left.ru_minflt - right.ru_minflt;
    diff_out.ru_majflt = left.ru_majflt - right.ru_majflt;
    diff_out.ru_nswap = left.ru_nswap - right.ru_nswap;
    diff_out.ru_inblock = left.ru_inblock - right.ru_inblock;
    diff_out.ru_oublock = left.ru_oublock - right.ru_oublock;
    diff_out.ru_msgsnd = left.ru_msgsnd - right.ru_msgsnd;
    diff_out.ru_msgrcv = left.ru_msgrcv - right.ru_msgrcv;
    diff_out.ru_nvcsw = left.ru_nvcsw - right.ru_nvcsw;
    diff_out.ru_nivcsw = left.ru_nivcsw - right.ru_nivcsw;
}

inline void rusageadd(const struct rusage &left, const struct rusage &right, struct rusage &diff_out)
{
    timeradd(&left.ru_utime, &right.ru_utime, &diff_out.ru_utime);
    timeradd(&left.ru_stime, &right.ru_stime, &diff_out.ru_stime);
    diff_out.ru_maxrss = left.ru_maxrss + right.ru_maxrss;
    diff_out.ru_ixrss = left.ru_ixrss + right.ru_ixrss;
    diff_out.ru_idrss = left.ru_idrss + right.ru_idrss;
    diff_out.ru_isrss = left.ru_isrss + right.ru_isrss;
    diff_out.ru_minflt = left.ru_minflt + right.ru_minflt;
    diff_out.ru_majflt = left.ru_majflt + right.ru_majflt;
    diff_out.ru_nswap = left.ru_nswap + right.ru_nswap;
    diff_out.ru_inblock = left.ru_inblock + right.ru_inblock;
    diff_out.ru_oublock = left.ru_oublock + right.ru_oublock;
    diff_out.ru_msgsnd = left.ru_msgsnd + right.ru_msgsnd;
    diff_out.ru_msgrcv = left.ru_msgrcv + right.ru_msgrcv;
    diff_out.ru_nvcsw = left.ru_nvcsw + right.ru_nvcsw;
    diff_out.ru_nivcsw = left.ru_nivcsw + right.ru_nivcsw;
}

size_t abbreviate_str(char *str, size_t len, size_t max_len);

inline int statp(const ghc::filesystem::path &path, struct stat *buf) {
    return stat(path.c_str(), buf);
}

inline int openp(const ghc::filesystem::path &path, int flags) {
    return open(path.c_str(), flags);
}

inline int openp(const ghc::filesystem::path &path, int flags, mode_t mode) {
    return open(path.c_str(), flags, mode);
}

Result<std::pair<ghc::filesystem::path, int>, std::string>
open_temp_file(const ghc::filesystem::path &pattern);

bool is_dev_null(const struct stat &st);
bool is_dev_null(int fd);

template<typename A>
struct final_action {   // slightly simplified
    A act;
    final_action(A a) :act{a} {}
    ~final_action() { act(); }
};

template<typename A>
final_action<A> finally(A act)   // deduce action type
{
    return final_action<A>{act};
}

std::string ok_prefix(std::string msg);
std::string err_prefix(std::string msg);
Result<std::string, std::string> err_to_ok(std::string msg);

#endif
