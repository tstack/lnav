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

#include <string>
#include <vector>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "line_buffer.hh"

using namespace std;

int
main(int argc, char* argv[])
{
    int retval = EXIT_SUCCESS;
    vector<file_range> index;
    auto_fd fd;

    if (argc < 2) {
        fprintf(stderr, "error: expecting file argument\n");
        retval = EXIT_FAILURE;
    } else if ((fd = open(argv[1], O_RDONLY)) == -1) {
        perror("open");
        retval = EXIT_FAILURE;
    } else {
        int line_number, start, end;
        line_buffer lb;
        file_range range;

        lb.set_fd(fd);
        while (true) {
            auto load_result = lb.load_next_line(range);

            if (load_result.isErr()) {
                return EXIT_FAILURE;
            }

            auto li = load_result.unwrap();

            if (li.li_file_range.empty()) {
                break;
            }

            index.emplace_back(li.li_file_range);

            range = li.li_file_range;
        }

        try {
            while (scanf("%d:%d:%d", &line_number, &start, &end) == 3) {
                range = index[line_number];
                auto read_result = lb.read_range(range);

                if (read_result.isErr()) {
                    return EXIT_FAILURE;
                }

                auto str = to_string(read_result.unwrap());

                str = str.substr(start, end - start);
                printf("%s\n", str.c_str());
            }
        } catch (line_buffer::error& e) {
            fprintf(stderr, "error: line buffer %s\n", strerror(e.e_err));
        }
    }
    return retval;
}
