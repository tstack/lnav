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
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file drive_json_ptr_dump.cc
 */

#include <iostream>

#include <stdlib.h>

#include "base/lnav_log.hh"
#include "config.h"
#include "json_op.hh"
#include "json_ptr.hh"
#include "yajl/api/yajl_gen.h"
#include "yajlpp.hh"
#include "view_curses.hh"

view_colors::
view_colors()
    : vc_dyn_pairs(0)
{
}

view_colors&
view_colors::singleton()
{
    static view_colors vc;
    return vc;
}

block_elem_t
view_colors::wchar_for_icon(ui_icon_t ic) const
{
    return this->vc_icons[lnav::enums::to_underlying(ic)];
}

int
main(int argc, char* argv[])
{
    int retval = EXIT_SUCCESS;
    yajl_status status;
    json_ptr_walk jpw;

    log_argv(argc, argv);

    std::string json_input(std::istreambuf_iterator<char>(std::cin), {});

    status = jpw.parse(json_input.c_str(), json_input.size());
    if (status == yajl_status_error) {
        fprintf(stderr,
                "error:cannot parse JSON input -- %s\n",
                jpw.jpw_error_msg.c_str());
        return EXIT_FAILURE;
    }

    if (status == yajl_status_client_canceled) {
        fprintf(stderr, "client cancel\n");
    }

    status = jpw.complete_parse();
    if (status == yajl_status_error) {
        fprintf(stderr,
                "error:cannot parse JSON input -- %s\n",
                jpw.jpw_error_msg.c_str());
        return EXIT_FAILURE;
    } else if (status == yajl_status_client_canceled) {
        fprintf(stderr, "client cancel\n");
    }

    for (json_ptr_walk::walk_list_t::iterator iter = jpw.jpw_values.begin();
         iter != jpw.jpw_values.end();
         ++iter)
    {
        printf("%s = %s\n", iter->wt_ptr.c_str(), iter->wt_value.c_str());

        {
            auto_mem<yajl_handle_t> parse_handle(yajl_free);
            json_ptr jp(iter->wt_ptr.c_str());
            json_op jo(jp);
            yajlpp_gen gen;

            jo.jo_ptr_callbacks = json_op::gen_callbacks;
            jo.jo_ptr_data = gen.get_handle();
            parse_handle.reset(
                yajl_alloc(&json_op::ptr_callbacks, nullptr, &jo));

            yajl_parse(parse_handle.in(),
                       (const unsigned char*) json_input.c_str(),
                       json_input.size());
            yajl_complete_parse(parse_handle.in());

            assert(iter->wt_value == gen.to_string_fragment().to_string());
        }
    }

    return retval;
}
