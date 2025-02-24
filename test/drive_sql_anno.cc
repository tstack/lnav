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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file drive_sql_anno.cc
 */

#include <stdio.h>
#include <stdlib.h>

#include "base/lnav_log.hh"
#include "sql.formatter.hh"
#include "sql_help.hh"
#include "sql_util.hh"
#include "sqlite-extension-func.hh"

int
main(int argc, char* argv[])
{
    int retval = EXIT_SUCCESS;
    auto_mem<sqlite3> db(sqlite3_close);

    log_argv(argc, argv);

    if (argc < 2) {
        fprintf(stderr, "error: expecting an SQL statement\n");
        retval = EXIT_FAILURE;
    } else if (sqlite3_open(":memory:", db.out()) != SQLITE_OK) {
        fprintf(stderr, "error: unable to make sqlite memory database\n");
        retval = EXIT_FAILURE;
    } else {
        register_sqlite_funcs(db.in(), sqlite_registration_funcs);

        auto al = attr_line_t(argv[1]);

        annotate_sql_statement(al);

        for (const auto& line_al : al.split_lines()) {
            printf("  %14s %s\n", " ", line_al.al_string.c_str());
            for (const auto& attr : line_al.get_attrs()) {
                const auto& lr = attr.sa_range;

                printf("  %14s %s%s\n",
                       attr.sa_type->sat_name,
                       std::string(lr.lr_start, ' ').c_str(),
                       std::string(lr.length(), '-').c_str());
            }
        }

        int near = al.length();
        if (argc == 3) {
            if (sscanf(argv[2], "%d", &near) != 1) {
                fprintf(stderr, "error: expecting an integer for third arg\n");
                return EXIT_FAILURE;
            }

            auto avail_help = find_sql_help_for_line(al, near);
            for (const auto& ht : avail_help) {
                printf("%s: %s\n", ht->ht_name, ht->ht_summary);
            }
        }

        auto formatted = lnav::db::format(al, near);

        printf("Formatted:\n%s\n", formatted.fr_content.c_str());
        printf("Cursor offset: %d\n", formatted.fr_cursor_offset);
    }

    return retval;
}
