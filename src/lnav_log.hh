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
 * @file lnav_log.hh
 */

#ifndef __lnav_log_hh
#define __lnav_log_hh

#include <errno.h>
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/queue.h>

#ifndef __dead2
#define __dead2 __attribute__((noreturn))
#endif

enum lnav_log_level_t {
    LOG_LEVEL_TRACE,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
};

void log_argv(int argc, char *argv[]);
void log_host_info(void);
void log_msg(enum lnav_log_level_t level, const char *src_file, int line_number,
    const char *fmt, ...);
void log_msg_extra(const char *fmt, ...);
void log_msg_extra_complete();
void log_install_handlers(void);
void log_abort(void) __dead2;

class log_state_dumper {
public:
    log_state_dumper() {
        LIST_INSERT_HEAD(&DUMPER_LIST.lsl_list, this, lsd_link);
    }

    virtual ~log_state_dumper() {
        LIST_REMOVE(this, lsd_link);
    };

    virtual void log_state() {

    };

    struct log_state_list {
        log_state_list() {
            LIST_INIT(&this->lsl_list);
        }

        LIST_HEAD(dumper_head, log_state_dumper) lsl_list;
    };

    static log_state_list DUMPER_LIST;

    LIST_ENTRY(log_state_dumper) lsd_link;
};

extern FILE *lnav_log_file;
extern const char *lnav_log_crash_dir;
extern const struct termios *lnav_log_orig_termios;
extern enum lnav_log_level_t lnav_log_level;

#define log_msg_wrapper(level, fmt...) \
    do { \
        if (lnav_log_level <= level) { \
            log_msg(level, __FILE__, __LINE__, fmt); \
        } \
    } \
    while (false)

#define log_error(fmt...) \
    log_msg_wrapper(LOG_LEVEL_ERROR, fmt);

#define log_warning(fmt...) \
    log_msg_wrapper(LOG_LEVEL_WARNING, fmt);

#define log_info(fmt...) \
    log_msg_wrapper(LOG_LEVEL_INFO, fmt);

#define log_debug(fmt...) \
    log_msg_wrapper(LOG_LEVEL_DEBUG, fmt);

#define log_trace(fmt...) \
    log_msg_wrapper(LOG_LEVEL_TRACE, fmt);

#define require(e)  \
    ((void) ((e) ? 0 : __require (#e, __FILE__, __LINE__)))
#define __require(e, file, line) \
    (log_msg(LOG_LEVEL_ERROR, file, line, "failed precondition `%s'", e), log_abort(), 1)

#define ensure(e)  \
    ((void) ((e) ? 0 : __ensure (#e, __FILE__, __LINE__)))
#define __ensure(e, file, line) \
    (log_msg(LOG_LEVEL_ERROR, file, line, "failed postcondition `%s'", e), log_abort(), 1)

#define log_perror(e)  \
    ((void) ((e != -1) ? 0 : __log_perror (#e, __FILE__, __LINE__)))
#define __log_perror(e, file, line) \
    (log_msg(LOG_LEVEL_ERROR, file, line, "syscall failed `%s' -- %s", e, strerror(errno)), 1)

#endif
