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
 * THIS SOFTWARE IS PROVIDED BY TIMOTHY STACK AND CONTRIBUTORS ''AS IS'' AND ANY
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
 * @file drive_json_ptr_dump.cc
 */

#include "config.h"

#include <stdlib.h>

#include "json_ptr.hh"

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;

    char buffer[1024];
    yajl_status status;
    json_ptr_walk jpw;
    ssize_t rc;

    log_argv(argc, argv);

    while ((rc = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) {
        status = jpw.parse(buffer, rc);
        if (status == yajl_status_error) {
            fprintf(stderr, "error:cannot parse JSON input -- %s\n",
                jpw.jpw_error_msg.c_str());
            retval = EXIT_FAILURE;
            break;
        }
        else if (status == yajl_status_client_canceled) {
            fprintf(stderr, "client cancel\n");
            break;
        }
    }
    status = jpw.complete_parse();
    if (status == yajl_status_error) {
        fprintf(stderr, "error:cannot parse JSON input -- %s\n",
            jpw.jpw_error_msg.c_str());
        retval = EXIT_FAILURE;
    }
    else if (status == yajl_status_client_canceled) {
        fprintf(stderr, "client cancel\n");
    }

    for (json_ptr_walk::walk_list_t::iterator iter = jpw.jpw_values.begin();
         iter != jpw.jpw_values.end();
         ++iter) {
        printf("%s = %s\n", iter->wt_ptr.c_str(), iter->wt_value.c_str());
    }

    return retval;
}
