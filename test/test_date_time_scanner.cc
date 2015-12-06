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
 */

#include "config.h"

#include <assert.h>
#include <locale.h>

#include "lnav_util.hh"
#include "../src/lnav_util.hh"

static const char *BAD_TIMES[] = {
    "1-2-3 1:2:3",

    "2013-22-01 12:01:22",
    "2013-00-01 12:01:22",

    NULL
};

int main(int argc, char *argv[])
{
    for (int lpc = 0; BAD_TIMES[lpc]; lpc++) {
        date_time_scanner dts;
        struct timeval tv;
        struct exttm tm;

        printf("Checking bad time: %s\n", BAD_TIMES[lpc]);
        assert(dts.scan(BAD_TIMES[lpc], strlen(BAD_TIMES[lpc]), NULL, &tm, tv) == NULL);
    }

    {
        const char *en_date = "Jan  1 12:00:00";
        const char *es_date = " 1/Ene/2014:12:00:00 +0000";
        struct timeval en_tv, es_tv;
        struct exttm en_tm, es_tm;
        date_time_scanner dts;

        if (setlocale(LC_TIME, "es_ES.UTF-8") != NULL) {
            assert(dts.scan(en_date, strlen(en_date), NULL, &en_tm, en_tv) != NULL);
            dts.clear();
            assert(dts.scan(es_date, strlen(es_date), NULL, &es_tm, es_tv) != NULL);
        }
    }

    {
        const char *epoch_str = "ts 1428721664 ]";
        struct exttm tm;
        off_t off = 0;

        bool rc = ptime_fmt("ts %s ]", &tm, epoch_str, off, strlen(epoch_str));
        assert(rc);
        assert(tm2sec(&tm.et_tm) == 1428721664);
    }
}
