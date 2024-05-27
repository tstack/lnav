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
 * @file lnav_log.cc
 */

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

#ifdef HAVE_EXECINFO_H
#    include <execinfo.h>
#endif
#if BACKWARD_HAS_DW == 1
#    include "backward-cpp/backward.hpp"
#endif

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#if defined HAVE_NCURSESW_CURSES_H
#    include <ncursesw/curses.h>
#    include <ncursesw/termcap.h>
#elif defined HAVE_NCURSESW_H
#    include <ncursesw.h>
#    include <termcap.h>
#elif defined HAVE_NCURSES_CURSES_H
#    include <ncurses/curses.h>
#    include <ncurses/termcap.h>
#elif defined HAVE_NCURSES_H
#    include <ncurses.h>
#    include <termcap.h>
#elif defined HAVE_CURSESW_H
#    include <cursesw.h>
#elif defined HAVE_CURSES_H
#    include <curses.h>
#    include <termcap.h>
#else
#    error "SysV or X/Open-compatible Curses header file required"
#endif

#include "ansi_scrubber.hh"
#include "auto_mem.hh"
#include "enum_util.hh"
#include "lnav_log.hh"
#include "opt_util.hh"

static constexpr size_t BUFFER_SIZE = 256 * 1024;
static constexpr size_t MAX_LOG_LINE_SIZE = 2 * 1024;

std::optional<FILE*> lnav_log_file;
lnav_log_level_t lnav_log_level = lnav_log_level_t::DEBUG;
const char* lnav_log_crash_dir;
std::optional<const struct termios*> lnav_log_orig_termios;
// NOTE: This mutex is leaked so that it is not destroyed during exit.
// Otherwise, any attempts to log will fail.
static std::mutex*
lnav_log_mutex()
{
    static auto* retval = new std::mutex();

    return retval;
}

static std::vector<log_state_dumper*>&
DUMPER_LIST()
{
    static auto* retval = new std::vector<log_state_dumper*>();

    return *retval;
}
static std::vector<log_crash_recoverer*> CRASH_LIST;

struct thid {
    static uint32_t COUNTER;

    thid() noexcept : t_id(COUNTER++) {}

    uint32_t t_id;
};

uint32_t thid::COUNTER = 0;

thread_local thid current_thid;
thread_local std::string thread_log_prefix;

static struct {
    size_t lr_length;
    off_t lr_frag_start;
    off_t lr_frag_end;
    char lr_data[BUFFER_SIZE];
} log_ring = {0, BUFFER_SIZE, 0, {}};

static const char* LEVEL_NAMES[] = {
    "T",
    "D",
    "I",
    "W",
    "E",
};

static char*
log_alloc()
{
    off_t data_end = log_ring.lr_length + MAX_LOG_LINE_SIZE;

    if (data_end >= (off_t) BUFFER_SIZE) {
        const char* new_start = &log_ring.lr_data[MAX_LOG_LINE_SIZE];

        new_start = (const char*) memchr(
            new_start, '\n', log_ring.lr_length - MAX_LOG_LINE_SIZE);
        log_ring.lr_frag_start = new_start - log_ring.lr_data;
        log_ring.lr_frag_end = log_ring.lr_length;
        log_ring.lr_length = 0;

        assert(log_ring.lr_frag_start >= 0);
        assert(log_ring.lr_frag_start <= (off_t) BUFFER_SIZE);
    } else if (data_end >= log_ring.lr_frag_start) {
        const char* new_start = &log_ring.lr_data[log_ring.lr_frag_start];

        new_start = (const char*) memchr(
            new_start, '\n', log_ring.lr_frag_end - log_ring.lr_frag_start);
        assert(new_start != nullptr);
        log_ring.lr_frag_start = new_start - log_ring.lr_data;
        assert(log_ring.lr_frag_start >= 0);
        assert(log_ring.lr_frag_start <= (off_t) BUFFER_SIZE);
    }

    return &log_ring.lr_data[log_ring.lr_length];
}

void
log_argv(int argc, char* argv[])
{
    const char* log_path = getenv("LNAV_LOG_PATH");

    if (log_path != nullptr) {
        lnav_log_file = make_optional_from_nullable(fopen(log_path, "ae"));
    }

    log_info("argv[%d] =", argc);
    for (int lpc = 0; lpc < argc; lpc++) {
        log_info("    [%d] = %s", lpc, argv[lpc]);
    }
}

void
log_set_thread_prefix(std::string prefix)
{
    // thread_log_prefix = std::move(prefix);
}

void
log_host_info()
{
    char cwd[MAXPATHLEN];
    char jittarget[128];
    struct utsname un;
    struct rusage ru;
    uint32_t pcre_jit;

    uname(&un);
    pcre2_config(PCRE2_CONFIG_JIT, &pcre_jit);
    pcre2_config(PCRE2_CONFIG_JITTARGET, jittarget);

    log_info("uname:");
    log_info("  sysname=%s", un.sysname);
    log_info("  nodename=%s", un.nodename);
    log_info("  machine=%s", un.machine);
    log_info("  release=%s", un.release);
    log_info("  version=%s", un.version);
    log_info("PCRE:");
    log_info("  jit=%d", pcre_jit);
    log_info("  jittarget=%s", jittarget);
    log_info("Environment:");
    log_info("  HOME=%s", getenv("HOME"));
    log_info("  XDG_CONFIG_HOME=%s", getenv("XDG_CONFIG_HOME"));
    log_info("  LANG=%s", getenv("LANG"));
    log_info("  PATH=%s", getenv("PATH"));
    log_info("  TERM=%s", getenv("TERM"));
    log_info("  TZ=%s", getenv("TZ"));
    log_info("Process:");
    log_info("  pid=%d", getpid());
    log_info("  ppid=%d", getppid());
    log_info("  pgrp=%d", getpgrp());
    log_info("  uid=%d", getuid());
    log_info("  gid=%d", getgid());
    log_info("  euid=%d", geteuid());
    log_info("  egid=%d", getegid());
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        log_info("  ERROR: getcwd failed");
    } else {
        log_info("  cwd=%s", cwd);
    }
    log_info("Executable:");
    log_info("  version=%s", VCS_PACKAGE_STRING);

    getrusage(RUSAGE_SELF, &ru);
    log_rusage(lnav_log_level_t::INFO, ru);
}

void
log_rusage_raw(enum lnav_log_level_t level,
               const char* src_file,
               int line_number,
               const struct rusage& ru)
{
    log_msg(level, src_file, line_number, "rusage:");
    log_msg(level,
            src_file,
            line_number,
            "  utime=%d.%06d",
            ru.ru_utime.tv_sec,
            ru.ru_utime.tv_usec);
    log_msg(level,
            src_file,
            line_number,
            "  stime=%d.%06d",
            ru.ru_stime.tv_sec,
            ru.ru_stime.tv_usec);
    log_msg(level, src_file, line_number, "  maxrss=%ld", ru.ru_maxrss);
    log_msg(level, src_file, line_number, "  ixrss=%ld", ru.ru_ixrss);
    log_msg(level, src_file, line_number, "  idrss=%ld", ru.ru_idrss);
    log_msg(level, src_file, line_number, "  isrss=%ld", ru.ru_isrss);
    log_msg(level, src_file, line_number, "  minflt=%ld", ru.ru_minflt);
    log_msg(level, src_file, line_number, "  majflt=%ld", ru.ru_majflt);
    log_msg(level, src_file, line_number, "  nswap=%ld", ru.ru_nswap);
    log_msg(level, src_file, line_number, "  inblock=%ld", ru.ru_inblock);
    log_msg(level, src_file, line_number, "  oublock=%ld", ru.ru_oublock);
    log_msg(level, src_file, line_number, "  msgsnd=%ld", ru.ru_msgsnd);
    log_msg(level, src_file, line_number, "  msgrcv=%ld", ru.ru_msgrcv);
    log_msg(level, src_file, line_number, "  nsignals=%ld", ru.ru_nsignals);
    log_msg(level, src_file, line_number, "  nvcsw=%ld", ru.ru_nvcsw);
    log_msg(level, src_file, line_number, "  nivcsw=%ld", ru.ru_nivcsw);
}

void
log_msg(lnav_log_level_t level,
        const char* src_file,
        int line_number,
        const char* fmt,
        ...)
{
    struct timeval curr_time;
    struct tm localtm;
    ssize_t prefix_size;
    va_list args;
    ssize_t rc;

    if (level < lnav_log_level) {
        return;
    }

    std::lock_guard<std::mutex> log_lock(*lnav_log_mutex());

    {
        // get the base name of the file.  NB: can't use basename() since it
        // can modify its argument
        const char* last_slash = src_file;

        for (int lpc = 0; src_file[lpc]; lpc++) {
            if (src_file[lpc] == '/' || src_file[lpc] == '\\') {
                last_slash = &src_file[lpc + 1];
            }
        }

        src_file = last_slash;
    }

    va_start(args, fmt);
    gettimeofday(&curr_time, nullptr);
    localtime_r(&curr_time.tv_sec, &localtm);
    auto line = log_alloc();
    auto gmtoff = std::abs(localtm.tm_gmtoff) / 60;
    prefix_size
        = snprintf(line,
                   MAX_LOG_LINE_SIZE,
                   "%4d-%02d-%02dT%02d:%02d:%02d.%03d%c%02d:%02d %s t%u %s:%d ",
                   localtm.tm_year + 1900,
                   localtm.tm_mon + 1,
                   localtm.tm_mday,
                   localtm.tm_hour,
                   localtm.tm_min,
                   localtm.tm_sec,
                   (int) (curr_time.tv_usec / 1000),
                   localtm.tm_gmtoff < 0 ? '-' : '+',
                   (int) gmtoff / 60,
                   (int) gmtoff % 60,
                   LEVEL_NAMES[lnav::enums::to_underlying(level)],
                   current_thid.t_id,
                   src_file,
                   line_number);
#if 0
    if (!thread_log_prefix.empty()) {
        prefix_size += snprintf(
            &line[prefix_size], MAX_LOG_LINE_SIZE - prefix_size,
            "%s ",
            thread_log_prefix.c_str());
    }
#endif
    rc = vsnprintf(
        &line[prefix_size], MAX_LOG_LINE_SIZE - prefix_size, fmt, args);
    if (rc >= (ssize_t) (MAX_LOG_LINE_SIZE - prefix_size)) {
        rc = MAX_LOG_LINE_SIZE - prefix_size - 1;
    }
    line[prefix_size + rc] = '\n';
    log_ring.lr_length += prefix_size + rc + 1;
    lnav_log_file | [&](auto file) {
        fwrite(line, 1, prefix_size + rc + 1, file);
        fflush(file);
    };
    va_end(args);
}

void
log_msg_extra(const char* fmt, ...)
{
    std::lock_guard<std::mutex> mg(*lnav_log_mutex());
    va_list args;

    va_start(args, fmt);
    auto line = log_alloc();
    auto rc = vsnprintf(line, MAX_LOG_LINE_SIZE - 1, fmt, args);
    log_ring.lr_length += rc;
    lnav_log_file | [&](auto file) {
        fwrite(line, 1, rc, file);
        fflush(file);
    };
    va_end(args);
}

void
log_msg_extra_complete()
{
    std::lock_guard<std::mutex> mg(*lnav_log_mutex());
    auto line = log_alloc();
    line[0] = '\n';
    log_ring.lr_length += 1;
    lnav_log_file | [&](auto file) {
        fwrite(line, 1, 1, file);
        fflush(file);
    };
}

void
log_backtrace(lnav_log_level_t level)
{
#ifdef HAVE_EXECINFO_H
    int frame_count;
    void* frames[128];

    frame_count = backtrace(frames, 128);
    auto bt = backtrace_symbols(frames, frame_count);
    for (int lpc = 0; lpc < frame_count; lpc++) {
        log_msg(level, __FILE__, __LINE__, "%s", bt[lpc]);
    }
#endif
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
static void
sigabrt(int sig, siginfo_t* info, void* ctx)
{
    char crash_path[1024], latest_crash_path[1024];
    int fd;
#ifdef HAVE_EXECINFO_H
    int frame_count;
    void* frames[128];
#endif
    struct tm localtm;
    time_t curr_time;

    if (lnav_log_crash_dir == nullptr) {
        printf("%*s", (int) log_ring.lr_length, log_ring.lr_data);
        return;
    }

    log_error("Received signal: %d", sig);

#ifdef HAVE_EXECINFO_H
    frame_count = backtrace(frames, 128);
#endif
    curr_time = time(nullptr);
    localtime_r(&curr_time, &localtm);
    snprintf(crash_path,
             sizeof(crash_path),
             "%s/crash-%4d-%02d-%02d-%02d-%02d-%02d.%d.log",
             lnav_log_crash_dir,
             localtm.tm_year + 1900,
             localtm.tm_mon + 1,
             localtm.tm_mday,
             localtm.tm_hour,
             localtm.tm_min,
             localtm.tm_sec,
             getpid());
    snprintf(latest_crash_path,
             sizeof(latest_crash_path),
             "%s/latest-crash.log",
             lnav_log_crash_dir);
    if ((fd = open(crash_path, O_CREAT | O_TRUNC | O_RDWR, 0600)) != -1) {
        if (log_ring.lr_frag_start < (off_t) BUFFER_SIZE) {
            (void) write(fd,
                         &log_ring.lr_data[log_ring.lr_frag_start],
                         log_ring.lr_frag_end - log_ring.lr_frag_start);
        }
        (void) write(fd, log_ring.lr_data, log_ring.lr_length);
#ifdef HAVE_EXECINFO_H
        backtrace_symbols_fd(frames, frame_count, fd);
#endif
#if BACKWARD_HAS_DW == 1
        {
            ucontext_t* uctx = static_cast<ucontext_t*>(ctx);
            void* error_addr = nullptr;

#    ifdef REG_RIP  // x86_64
            error_addr
                = reinterpret_cast<void*>(uctx->uc_mcontext.gregs[REG_RIP]);
#    elif defined(REG_EIP)  // x86_32
            error_addr
                = reinterpret_cast<void*>(uctx->uc_mcontext.gregs[REG_EIP]);
#    endif

            backward::StackTrace st;

            if (error_addr) {
                st.load_from(error_addr,
                             32,
                             reinterpret_cast<void*>(uctx),
                             info->si_addr);
            } else {
                st.load_here(32, reinterpret_cast<void*>(uctx), info->si_addr);
            }
            backward::TraceResolver tr;

            tr.load_stacktrace(st);
            for (size_t lpc = 0; lpc < st.size(); lpc++) {
                auto trace = tr.resolve(st[lpc]);
                char buf[1024];

                snprintf(buf,
                         sizeof(buf),
                         "Frame %lu:%s:%s (%s:%d)\n",
                         lpc,
                         trace.object_filename.c_str(),
                         trace.object_function.c_str(),
                         trace.source.filename.c_str(),
                         trace.source.line);
                write(fd, buf, strlen(buf));
            }
        }
#endif
        log_ring.lr_length = 0;
        log_ring.lr_frag_start = BUFFER_SIZE;
        log_ring.lr_frag_end = 0;

        log_host_info();

        for (auto lsd : DUMPER_LIST()) {
            lsd->log_state();
        }

        if (log_ring.lr_frag_start < (off_t) BUFFER_SIZE) {
            write(fd,
                  &log_ring.lr_data[log_ring.lr_frag_start],
                  log_ring.lr_frag_end - log_ring.lr_frag_start);
        }
        write(fd, log_ring.lr_data, log_ring.lr_length);
        if (getenv("DUMP_CRASH") != nullptr) {
            char buffer[1024];
            int rc;

            lseek(fd, 0, SEEK_SET);
            while ((rc = read(fd, buffer, sizeof(buffer))) > 0) {
                write(STDERR_FILENO, buffer, rc);
            }
        }
        close(fd);

        remove(latest_crash_path);
        symlink(crash_path, latest_crash_path);
    }

    lnav_log_orig_termios | [](auto termios) {
        for (auto lcr : CRASH_LIST) {
            lcr->log_crash_recover();
        }

        tcsetattr(STDOUT_FILENO, TCSAFLUSH, termios);
        dup2(STDOUT_FILENO, STDERR_FILENO);
    };
    fmt::print(R"(

{red_start}==== GURU MEDITATION ===={norm}

Unfortunately, lnav has crashed, sorry for the inconvenience.

You can help improve lnav by executing the following command
to upload the crash logs to https://crash.lnav.org:

  {green_start}${norm} {bold_start}lnav -m crash upload{norm}

Or, you can send the following file to {PACKAGE_BUGREPORT}:

  {crash_path}

{red_start}========================={norm}
)",
               fmt::arg("red_start", ANSI_COLOR(1)),
               fmt::arg("green_start", ANSI_COLOR(2)),
               fmt::arg("bold_start", ANSI_BOLD_START),
               fmt::arg("norm", ANSI_NORM),
               fmt::arg("PACKAGE_BUGREPORT", PACKAGE_BUGREPORT),
               fmt::arg("crash_path", crash_path));

#ifndef ATTACH_ON_SIGNAL
    if (isatty(STDIN_FILENO)) {
        char response;

        fprintf(stderr, "\nWould you like to attach a debugger? (y/N) ");
        fflush(stderr);

        if (scanf("%c", &response) > 0 && tolower(response) == 'y') {
            pid_t lnav_pid = getpid();
            pid_t child_pid;

            switch ((child_pid = fork())) {
                case 0: {
                    char pid_str[32];

                    snprintf(pid_str, sizeof(pid_str), "--pid=%d", lnav_pid);
                    execlp("gdb", "gdb", pid_str, nullptr);

                    snprintf(pid_str, sizeof(pid_str), "%d", lnav_pid);
                    execlp("lldb", "lldb", "--attach-pid", pid_str, nullptr);

                    fprintf(stderr, "Could not attach gdb or lldb, exiting.\n");
                    _exit(1);
                    break;
                }

                case -1: {
                    break;
                }

                default: {
                    int status;

                    while (wait(&status) < 0) {
                    }
                    break;
                }
            }
        }
    }
#endif

    _exit(1);
}
#pragma GCC diagnostic pop

void
log_install_handlers()
{
    const size_t stack_size = 8 * 1024 * 1024;
    const int sigs[] = {
        SIGABRT,
        SIGSEGV,
        SIGBUS,
        SIGILL,
        SIGFPE,
    };
    static auto_mem<void> stack_content;

    stack_t ss;

    stack_content = malloc(stack_size);
    ss.ss_sp = stack_content;
    ss.ss_size = stack_size;
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);
    for (const auto sig : sigs) {
        struct sigaction sa;

        memset(&sa, 0, sizeof(sa));
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER | SA_RESETHAND;
        sigfillset(&sa.sa_mask);
        sigdelset(&sa.sa_mask, sig);
        sa.sa_sigaction = sigabrt;

        sigaction(sig, &sa, nullptr);
    }
}

void
log_abort()
{
    raise(SIGABRT);
    _exit(1);
}

void
log_pipe_err(int fd)
{
    std::thread reader([fd]() {
        char buffer[1024];
        bool done = false;

        while (!done) {
            int rc = read(fd, buffer, sizeof(buffer));

            switch (rc) {
                case -1:
                case 0:
                    done = true;
                    break;
                default:
                    while (buffer[rc - 1] == '\n' || buffer[rc - 1] == '\r') {
                        rc -= 1;
                    }

                    log_error("%.*s", rc, buffer);
                    break;
            }
        }

        close(fd);
    });

    reader.detach();
}

log_state_dumper::
log_state_dumper()
{
    DUMPER_LIST().push_back(this);
}

log_state_dumper::~
log_state_dumper()
{
    auto iter = std::find(DUMPER_LIST().begin(), DUMPER_LIST().end(), this);
    if (iter != DUMPER_LIST().end()) {
        DUMPER_LIST().erase(iter);
    }
}

log_crash_recoverer::
log_crash_recoverer()
{
    CRASH_LIST.push_back(this);
}

log_crash_recoverer::~
log_crash_recoverer()
{
    auto iter = std::find(CRASH_LIST.begin(), CRASH_LIST.end(), this);

    if (iter != CRASH_LIST.end()) {
        CRASH_LIST.erase(iter);
    }
}
