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

#ifndef __lnav_util_hh
#define __lnav_util_hh

#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <poll.h>
#include <sys/types.h>

#include "spookyhash/SpookyV2.h"

#include <string>
#include <vector>

#include "ptimec.hh"
#include "byte_array.hh"

inline std::string trim(const std::string &str)
{
    std::string::size_type start, end;

    for (start = 0; start < str.size() && isspace(str[start]); start++);
    for (end = str.size(); end > 0 && isspace(str[end - 1]); end--);

    return str.substr(start, end - start);
}

size_t unquote(char *dst, const char *str, size_t len);

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

inline int roundup_size(size_t size, int step)
{
    int retval = size + step;

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

std::string time_ago(time_t last_time);

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

/* XXX figure out how to do this with the template */
void sqlite_close_wrapper(void *mem);

std::string get_current_dir(void);

bool change_to_parent_dir(void);

std::pair<std::string, std::string> split_path(const char *path, ssize_t len);

inline
std::pair<std::string, std::string> split_path(const std::string &path) {
    return split_path(path.c_str(), path.size());
};

enum file_format_t {
    FF_UNKNOWN,
    FF_SQLITE_DB,
};

file_format_t detect_file_format(const std::string &filename);

bool next_format(const char * const fmt[], int &index, int &locked_index);

inline bool is_glob(const char *fn)
{
    return (strchr(fn, '*') != NULL ||
            strchr(fn, '?') != NULL ||
            strchr(fn, '[') != NULL);
};

bool is_url(const char *fn);

inline bool startswith(const char *str, const char *prefix)
{
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

inline bool startswith(std::string str, const char *prefix)
{
    return startswith(str.c_str(), prefix);
}

inline bool endswith(const char *str, const char *suffix)
{
    size_t len = strlen(str), suffix_len = strlen(suffix);

    if (suffix_len > len) {
        return false;
    }

    return strcmp(&str[len - suffix_len], suffix) == 0;
}

std::string build_path(const std::vector<std::string> &paths);

bool read_file(const char *filename, std::string &out);

/**
 * Convert the time stored in a 'tm' struct into epoch time.
 *
 * @param t The 'tm' structure to convert to epoch time.
 * @return The given time in seconds since the epoch.
 */
time_t tm2sec(const struct tm *t);

struct tm *secs2tm(time_t *tim_p, struct tm *res);

extern const char *std_time_fmt[];

struct date_time_scanner {
    date_time_scanner() : dts_keep_base_tz(false),
                          dts_local_time(false),
                          dts_local_offset_cache(0),
                          dts_local_offset_valid(0),
                          dts_local_offset_expiry(0) {
        this->clear();
    };

    void clear(void) {
        this->dts_base_time = 0;
        memset(&this->dts_base_tm, 0, sizeof(this->dts_base_tm));
        this->dts_fmt_lock = -1;
        this->dts_fmt_len = -1;
    };

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
                     struct timeval &tv_out);

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

inline bool pollfd_ready(const std::vector<struct pollfd> &pollfds, int fd, short events = POLLIN|POLLHUP) {
    for (std::vector<struct pollfd>::const_iterator iter = pollfds.begin();
            iter != pollfds.end();
            ++iter) {
        if (iter->fd == fd && iter->revents & events) {
            return true;
        }
    }

    return false;
};

bool wordexperr(int rc, std::string &msg);

#endif
