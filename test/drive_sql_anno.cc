/**
 * Copyright (c) 2017, Timothy Stack
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
 * @file drive_sql_anno.cc
 */

#include <stdio.h>
#include <stdlib.h>


#include "lnav.hh"
#include "sql_util.hh"

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;

    log_argv(argc, argv);

    if (argc < 2) {
        fprintf(stderr, "error: expecting an SQL statement\n");
        retval = EXIT_FAILURE;
    }
    else {
        attr_line_t al(argv[1]);

        annotate_sql_statement(al);

        for (auto &attr : al.get_attrs()) {
            auto &lr = attr.sa_range;

            printf("  %d:%d (%s) -- %s\n",
                   lr.lr_start, lr.lr_end,
                   attr.sa_type->sat_name,
                   al.get_substring(lr).c_str());
        }

        if (argc == 3) {
            int near;
            sscanf(argv[2], "%d", &near);

            auto iter = find_string_attr(al.get_attrs(), (size_t) near);
            if (iter != al.get_attrs().end()) {
                printf("nearest %s\n",
                       al.get_substring(iter->sa_range).c_str());
            }
        }
    }

    return retval;
}
