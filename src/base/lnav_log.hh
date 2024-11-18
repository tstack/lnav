/**
 * Copyright (c) 2014, Timothy Stack
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
 * @file lnav_log.hh
 */

#ifndef lnav_log_hh
#define lnav_log_hh

#include <cstdint>
#include <optional>
#include <string>

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#ifndef lnav_dead2
#    define lnav_dead2 __attribute__((noreturn))
#endif

struct termios;

enum class lnav_log_level_t : uint32_t {
    TRACE,
    DEBUG,
    INFO,
    WARNING,
    ERROR,
};

void log_argv(int argc, char* argv[]);
void log_host_info();
void log_rusage_raw(enum lnav_log_level_t level,
                    const char* src_file,
                    int line_number,
                    const struct rusage& ru);
void log_msg(enum lnav_log_level_t level,
             const char* src_file,
             int line_number,
             const char* fmt,
             ...);
void log_msg_extra(const char* fmt, ...);
void log_msg_extra_complete();
void log_install_handlers();
void log_abort() lnav_dead2;

class log_pipe_err_handle {
public:
    log_pipe_err_handle(int fd) : h_old_stderr_fd(fd) {}

    log_pipe_err_handle(const log_pipe_err_handle&) = delete;

    log_pipe_err_handle(log_pipe_err_handle&& other) noexcept;

    ~log_pipe_err_handle();

private:
    int h_old_stderr_fd{-1};
};

log_pipe_err_handle log_pipe_err(int readfd, int writefd);
void log_set_thread_prefix(std::string prefix);
void log_backtrace(lnav_log_level_t level);

struct log_state_dumper {
public:
    log_state_dumper();

    virtual ~log_state_dumper();

    virtual void log_state(){

    };

    log_state_dumper(const log_state_dumper&) = delete;
    log_state_dumper& operator=(const log_state_dumper&) = delete;
};

struct log_crash_recoverer {
public:
    log_crash_recoverer();

    virtual ~log_crash_recoverer();

    virtual void log_crash_recover() = 0;
};

extern std::optional<FILE*> lnav_log_file;
extern const char* lnav_log_crash_dir;
extern std::optional<const struct termios*> lnav_log_orig_termios;
extern enum lnav_log_level_t lnav_log_level;

#define log_msg_wrapper(level, fmt...) \
    do { \
        if (lnav_log_level <= level) { \
            log_msg(level, __FILE__, __LINE__, fmt); \
        } \
    } while (false)

#define log_rusage(level, ru) log_rusage_raw(level, __FILE__, __LINE__, ru);

#define log_error(fmt...) log_msg_wrapper(lnav_log_level_t::ERROR, fmt);

#define log_warning(fmt...) log_msg_wrapper(lnav_log_level_t::WARNING, fmt);

#define log_info(fmt...) log_msg_wrapper(lnav_log_level_t::INFO, fmt);

#define log_debug(fmt...) log_msg_wrapper(lnav_log_level_t::DEBUG, fmt);

#define log_trace(fmt...) log_msg_wrapper(lnav_log_level_t::TRACE, fmt);

#define require(e) ((void) ((e) ? 0 : lnav_require(#e, __FILE__, __LINE__)))
#define lnav_require(e, file, line) \
    (log_msg( \
         lnav_log_level_t::ERROR, file, line, "failed precondition `%s'", e), \
     log_abort(), \
     1)

#define require_true(lhs) \
    ((void) ((lhs) ? 0 : lnav_require_unary(#lhs, lhs, __FILE__, __LINE__)))
#define require_false(lhs) \
    ((void) ((!lhs) ? 0 : lnav_require_unary(#lhs, lhs, __FILE__, __LINE__)))
#define lnav_require_unary(e, lhs, file, line) \
    (log_msg(lnav_log_level_t::ERROR, \
             file, \
             line, \
             "failed precondition `%s' (lhs=%s)", \
             e, \
             std::to_string(lhs).c_str()), \
     log_abort(), \
     1)

#define require_ge(lhs, rhs) \
    ((void) ((lhs >= rhs) \
                 ? 0 \
                 : lnav_require_binary( \
                       #lhs " >= " #rhs, lhs, rhs, __FILE__, __LINE__)))
#define require_gt(lhs, rhs) \
    ((void) ((lhs > rhs) \
                 ? 0 \
                 : lnav_require_binary( \
                       #lhs " > " #rhs, lhs, rhs, __FILE__, __LINE__)))
#define require_lt(lhs, rhs) \
    ((void) ((lhs < rhs) \
                 ? 0 \
                 : lnav_require_binary( \
                       #lhs " < " #rhs, lhs, rhs, __FILE__, __LINE__)))

#define lnav_require_binary(e, lhs, rhs, file, line) \
    (log_msg(lnav_log_level_t::ERROR, \
             file, \
             line, \
             "failed precondition `%s' (lhs=%s; rhs=%s)", \
             e, \
             std::to_string(lhs).c_str(), \
             std::to_string(rhs).c_str()), \
     log_abort(), \
     1)

#define ensure(e) ((void) ((e) ? 0 : lnav_ensure(#e, __FILE__, __LINE__)))
#define lnav_ensure(e, file, line) \
    (log_msg( \
         lnav_log_level_t::ERROR, file, line, "failed postcondition `%s'", e), \
     log_abort(), \
     1)

#define log_perror(e) \
    ((void) ((e != -1) ? 0 : lnav_log_perror(#e, __FILE__, __LINE__)))
#define lnav_log_perror(e, file, line) \
    (log_msg(lnav_log_level_t::ERROR, \
             file, \
             line, \
             "syscall failed `%s' -- %s", \
             e, \
             strerror(errno)), \
     1)

#endif
