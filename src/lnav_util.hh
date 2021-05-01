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
#include <numeric>
#include <type_traits>

#include "ptimec.hh"
#include "byte_array.hh"
#include "optional.hpp"
#include "base/result.h"
#include "base/intern_string.hh"
#include "fmt/format.h"
#include "ghc/filesystem.hpp"

#if SIZEOF_OFF_T == 8
#define FORMAT_OFF_T    "%lld"
#elif SIZEOF_OFF_T == 4
#define FORMAT_OFF_T    "%ld"
#else
#error "off_t has unhandled size..."
#endif

class hasher {
public:
    hasher() {
        this->h_context.Init(0, 0);
    }

    hasher &update(const std::string &str) {
        this->h_context.Update(str.data(), str.length());

        return *this;
    }

    hasher &update(const string_fragment &str) {
        this->h_context.Update(str.data(), str.length());

        return *this;
    }

    hasher &update(const char *bits, size_t len) {
        this->h_context.Update(bits, len);

        return *this;
    }

    template<typename T,
    typename = std::enable_if<std::is_arithmetic<T>::value>>
    hasher &update(T value) {
        this->h_context.Update(&value, sizeof(value));

        return *this;
    }

    std::string to_string() {
        byte_array<2, uint64> bits;

        this->h_context.Final(bits.out(0), bits.out(1));
        return bits.to_string();
    }
private:
    SpookyHash h_context;
};

bool change_to_parent_dir();

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

std::string build_path(const std::vector<ghc::filesystem::path> &paths);

Result<std::string, std::string> read_file(const ghc::filesystem::path &path);

std::string to_netloc(const nonstd::optional<std::string>& username,
                      std::string hostname);

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
