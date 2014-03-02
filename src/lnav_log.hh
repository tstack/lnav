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

#include <stdio.h>
#include <termios.h>

enum lnav_log_level_t {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
};

void log_host_info(void);
void log_msg(lnav_log_level_t level, const char *src_file, int line_number,
    const char *fmt, ...);
void log_install_handlers(void);

extern FILE *lnav_log_file;
extern const char *lnav_log_crash_dir;
extern const struct termios *lnav_log_orig_termios;
extern lnav_log_level_t lnav_log_level;

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


#endif
