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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <stdlib.h>

#include "command_executor.hh"
#include "config.h"
#include "lnav_config.hh"
#include "top_status_source.hh"

static time_t current_time = 1;

int
gettimeofday(struct timeval* tp, void* tzp)
{
    tp->tv_sec = current_time;
    tp->tv_usec = 0;

    return 0;
}

int
main(int argc, char* argv[])
{
    int retval = EXIT_SUCCESS;

    auto_sqlite3 db;

    if (sqlite3_open(":memory:", db.out()) != SQLITE_OK) {
        fprintf(stderr, "error: unable to create sqlite memory database\n");
        exit(EXIT_FAILURE);
    }

    top_status_source_cfg cfg;
    top_status_source tss(db, cfg);

    setenv("HOME", "/", 1);

    std::vector<lnav::console::user_message> errors;
    std::vector<std::filesystem::path> paths;

    load_config(paths, errors);

    {
        status_field& sf
            = tss.statusview_value_for_field(top_status_source::TSF_TIME);
        attr_line_t val;

        tss.update_time();
        val = sf.get_value();
        assert(val.get_string() == sf.get_value().get_string());
        current_time += 2;
        tss.update_time();
        assert(val.get_string() != sf.get_value().get_string());

        cfg.tssc_clock_format = "abc";
        tss.update_time();
        val = sf.get_value();
        assert(val.get_string() == " abc");
    }

    return retval;
}
