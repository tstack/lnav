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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file lnav_log.cc
 */

#include "config.h"

#include <time.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>
#include <pthread.h>

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/param.h>

#if defined HAVE_NCURSESW_CURSES_H
#  include <ncursesw/termcap.h>
#elif defined HAVE_NCURSESW_H
#  include <termcap.h>
#elif defined HAVE_NCURSES_CURSES_H
#  include <ncurses/termcap.h>
#elif defined HAVE_NCURSES_H
#  include <termcap.h>
#elif defined HAVE_CURSES_H
#  include <termcap.h>
#else
#  error "SysV or X/Open-compatible Curses header file required"
#endif

#include "lnav_log.hh"
#include "pthreadpp.hh"

static const size_t BUFFER_SIZE = 256 * 1024;
static const size_t MAX_LOG_LINE_SIZE = 2048;

static const char *CRASH_MSG =
    "\n"
    "\n"
    "==== GURU MEDITATION ====\n"
    "Unfortunately, lnav has crashed, sorry for the inconvenience.\n"
    "\n"
    "You can help improve lnav by sending the following file to " PACKAGE_BUGREPORT " :\n"
    "  %s\n"
    "=========================\n";

FILE *lnav_log_file;
lnav_log_level_t lnav_log_level = LOG_LEVEL_DEBUG;
const char *lnav_log_crash_dir;
const struct termios *lnav_log_orig_termios;
static pthread_mutex_t lnav_log_mutex = PTHREAD_MUTEX_INITIALIZER;

log_state_dumper::log_state_list log_state_dumper::DUMPER_LIST;

static struct {
    size_t lr_length;
    off_t lr_frag_start;
    off_t lr_frag_end;
    char lr_data[BUFFER_SIZE];
} log_ring = {
    0,
    BUFFER_SIZE,
    0,
};

static const char *LEVEL_NAMES[] = {
    "T",
    "D",
    "I",
    "W",
    "E",
};

static char *log_alloc(void)
{
    off_t data_end = log_ring.lr_length + MAX_LOG_LINE_SIZE;

    if (data_end >= (off_t)BUFFER_SIZE) {
        const char *new_start = &log_ring.lr_data[MAX_LOG_LINE_SIZE];

        new_start = (const char *)memchr(
            new_start, '\n', log_ring.lr_length - MAX_LOG_LINE_SIZE);
        log_ring.lr_frag_start = new_start - log_ring.lr_data;
        log_ring.lr_frag_end = log_ring.lr_length;
        log_ring.lr_length = 0;

        assert(log_ring.lr_frag_start >= 0);
        assert(log_ring.lr_frag_start <= (off_t)BUFFER_SIZE);
    } else if (data_end >= log_ring.lr_frag_start) {
        const char *new_start = &log_ring.lr_data[log_ring.lr_frag_start];

        new_start = (const char *)memchr(
            new_start, '\n', log_ring.lr_frag_end - log_ring.lr_frag_start);
        assert(new_start != NULL);
        log_ring.lr_frag_start = new_start - log_ring.lr_data;
        assert(log_ring.lr_frag_start >= 0);
        assert(log_ring.lr_frag_start <= (off_t)BUFFER_SIZE);
    }

    return &log_ring.lr_data[log_ring.lr_length];
}

void log_argv(int argc, char *argv[])
{
    const char *log_path = getenv("LNAV_LOG_PATH");

    if (log_path != NULL) {
        lnav_log_file = fopen(log_path, "a");
    }

    log_info("argv[%d] =", argc);
    for (int lpc = 0; lpc < argc; lpc++) {
        log_info("    [%d] = %s", lpc, argv[lpc]);
    }
}

void log_host_info(void)
{
    char cwd[MAXPATHLEN];
    struct utsname un;

    uname(&un);
    log_info("uname:");
    log_info("  sysname=%s", un.sysname);
    log_info("  nodename=%s", un.nodename);
    log_info("  machine=%s", un.machine);
    log_info("  release=%s", un.release);
    log_info("  version=%s", un.version);
    log_info("Environment:");
    log_info("  HOME=%s", getenv("HOME"));
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
    getcwd(cwd, sizeof(cwd));
    log_info("  cwd=%s", cwd);
    log_info("Executable:");
    log_info("  version=%s", VCS_PACKAGE_STRING);
}

void log_msg(lnav_log_level_t level, const char *src_file, int line_number,
    const char *fmt, ...)
{
    struct timeval curr_time;
    struct tm localtm;
    ssize_t prefix_size;
    va_list args;
    char *line;
    ssize_t rc;

    if (level < lnav_log_level) {
        return;
    }

    mutex_guard mg(lnav_log_mutex);

    va_start(args, fmt);
    gettimeofday(&curr_time, NULL);
    localtime_r(&curr_time.tv_sec, &localtm);
    line = log_alloc();
    prefix_size = snprintf(
        line, MAX_LOG_LINE_SIZE,
        "%4d-%02d-%02dT%02d:%02d:%02d.%03d %s %s:%d ",
        localtm.tm_year + 1900,
        localtm.tm_mon + 1,
        localtm.tm_mday,
        localtm.tm_hour,
        localtm.tm_min,
        localtm.tm_sec,
        (int)(curr_time.tv_usec / 1000),
        LEVEL_NAMES[level],
        basename((char *)src_file),
        line_number);
    rc = vsnprintf(&line[prefix_size], MAX_LOG_LINE_SIZE - prefix_size,
        fmt, args);
    if (rc >= (ssize_t)(MAX_LOG_LINE_SIZE - prefix_size)) {
        rc = MAX_LOG_LINE_SIZE - prefix_size - 1;
    }
    line[prefix_size + rc] = '\n';
    log_ring.lr_length += prefix_size + rc + 1;
    if (lnav_log_file != NULL) {
        fwrite(line, 1, prefix_size + rc + 1, lnav_log_file);
        fflush(lnav_log_file);
    }
    va_end(args);
}

void log_msg_extra(const char *fmt, ...)
{
    va_list args;
    ssize_t rc;
    char *line;

    mutex_guard mg(lnav_log_mutex);

    va_start(args, fmt);
    line = log_alloc();
    rc = vsnprintf(line, MAX_LOG_LINE_SIZE - 1, fmt, args);
    log_ring.lr_length += rc;
    if (lnav_log_file != NULL) {
        fwrite(line, 1, rc, lnav_log_file);
        fflush(lnav_log_file);
    }
    va_end(args);
}

void log_msg_extra_complete()
{
    char *line;

    mutex_guard mg(lnav_log_mutex);

    line = log_alloc();
    line[0] = '\n';
    log_ring.lr_length += 1;
    if (lnav_log_file != NULL) {
        fwrite(line, 1, 1, lnav_log_file);
        fflush(lnav_log_file);
    }
}

static void sigabrt(int sig)
{
    char crash_path[1024], latest_crash_path[1024];
    int fd, frame_count;
    void *frames[128];
    struct tm localtm;
    time_t curr_time;

    if (lnav_log_crash_dir == NULL) {
        return;
    }

    log_error("Received signal: %d", sig);

#ifdef HAVE_EXECINFO_H
    frame_count = backtrace(frames, 128);
#endif
    curr_time = time(NULL);
    localtime_r(&curr_time, &localtm);
    snprintf(crash_path, sizeof(crash_path),
        "%s/crash-%4d-%02d-%02d-%02d-%02d-%02d.%d.log",
        lnav_log_crash_dir,
        localtm.tm_year + 1900,
        localtm.tm_mon + 1,
        localtm.tm_mday,
        localtm.tm_hour,
        localtm.tm_min,
        localtm.tm_sec,
        getpid());
    snprintf(latest_crash_path, sizeof(latest_crash_path),
        "%s/latest-crash.log", lnav_log_crash_dir);
    if ((fd = open(crash_path, O_CREAT|O_TRUNC|O_WRONLY, 0600)) != -1) {
        if (log_ring.lr_frag_start < (off_t)BUFFER_SIZE) {
            write(fd, &log_ring.lr_data[log_ring.lr_frag_start],
                log_ring.lr_frag_end - log_ring.lr_frag_start);
        }
        write(fd, log_ring.lr_data, log_ring.lr_length);
#ifdef HAVE_EXECINFO_H
        backtrace_symbols_fd(frames, frame_count, fd);
#endif
        log_ring.lr_length = 0;
        log_ring.lr_frag_start = BUFFER_SIZE;
        log_ring.lr_frag_end = 0;

        log_host_info();

        {
            log_state_dumper *lsd;

            for (lsd = LIST_FIRST(&log_state_dumper::DUMPER_LIST.lsl_list);
                    lsd != NULL;
                    lsd = LIST_NEXT(lsd, lsd_link)) {
                lsd->log_state();
            }
        }

        if (log_ring.lr_frag_start < (off_t)BUFFER_SIZE) {
            write(fd, &log_ring.lr_data[log_ring.lr_frag_start],
                    log_ring.lr_frag_end - log_ring.lr_frag_start);
        }
        write(fd, log_ring.lr_data, log_ring.lr_length);
        close(fd);

        remove(latest_crash_path);
        symlink(crash_path, latest_crash_path);
    }

    if (lnav_log_orig_termios != NULL) {
        tcsetattr(STDOUT_FILENO, TCSAFLUSH, lnav_log_orig_termios);
    }
    fprintf(stderr, CRASH_MSG, crash_path);

#ifndef ATTACH_ON_SIGNAL
    if (isatty(STDIN_FILENO)) {
        char response;

        fprintf(stderr, "\nWould you like to attach a debugger? (y/N) ");
        fflush(stderr);

        scanf("%c", &response);

        if (tolower(response) == 'y') {
            pid_t lnav_pid = getpid();
            pid_t child_pid;

            switch ((child_pid = fork())) {
                case 0: {
                    char pid_str[32];

                    snprintf(pid_str, sizeof(pid_str), "--pid=%d", lnav_pid);
                    execlp("gdb", "gdb", pid_str, NULL);

                    snprintf(pid_str, sizeof(pid_str),
                        "--attach-pid=%d", lnav_pid);
                    execlp("lldb", "lldb", pid_str, NULL);

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

void log_install_handlers(void)
{
    signal(SIGABRT, sigabrt);
    signal(SIGSEGV, sigabrt);
    signal(SIGBUS, sigabrt);
    signal(SIGILL, sigabrt);
    signal(SIGFPE, sigabrt);
}

void log_abort(void)
{
    sigabrt(SIGABRT);
    _exit(1);
}
