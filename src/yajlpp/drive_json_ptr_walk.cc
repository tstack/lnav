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

int
main(int argc, char* argv[])
{
    int retval = EXIT_SUCCESS;
    log_argv(argc, argv);

    std::string json_input(std::istreambuf_iterator<char>(std::cin), {});

    auto parse_res = json_walk_collector::parse_fully(json_input);
    if (parse_res.isErr()) {
        fprintf(stderr, "error: %s\n", parse_res.unwrapErr().c_str());
        return EXIT_FAILURE;
    }

    auto jwc = parse_res.unwrap();
    for (const auto& [ptr, value] : jwc.jwc_values) {
        auto value_str = value.is<null_value_t>() ? "null"
                                                  : fmt::to_string(value);
        printf("%s = %s\n", ptr.c_str(), value_str.c_str());

        yajlpp_gen gen;
        extract_json_from(gen, json_input, ptr.c_str()).unwrap();
        assert(value_str == gen.to_string_fragment());
    }

    return retval;
}
