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

#include <algorithm>
#include <random>
#include <tuple>
#include <vector>

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/auto_fd.hh"
#include "base/string_util.hh"
#include "config.h"
#include "line_buffer.hh"

using namespace std;

int
main(int argc, char* argv[])
{
    int c, rnd_iters = 5, retval = EXIT_SUCCESS;
    vector<tuple<int, off_t, ssize_t> > index;
    auto_fd fd = auto_fd(STDIN_FILENO), fd_cmp;
    int offseti = 0;
    off_t offset = 0;
    int count = 1000;
    struct stat st;

    while ((c = getopt(argc, argv, "o:i:n:c:")) != -1) {
        switch (c) {
            case 'o':
                if (sscanf(optarg, "%d", &offseti) != 1) {
                    fprintf(stderr,
                            "error: offset is not an integer -- %s\n",
                            optarg);
                    retval = EXIT_FAILURE;
                } else {
                    offset = offseti;
                }
                break;
            case 'n':
                if (sscanf(optarg, "%d", &rnd_iters) != 1) {
                    fprintf(stderr,
                            "error: offset is not an integer -- %s\n",
                            optarg);
                    retval = EXIT_FAILURE;
                }
                break;
            case 'c':
                if (sscanf(optarg, "%d", &count) != 1) {
                    fprintf(stderr,
                            "error: count is not an integer -- %s\n",
                            optarg);
                    retval = EXIT_FAILURE;
                }
                break;
            case 'i': {
                FILE* file;

                if ((file = fopen(optarg, "r")) == NULL) {
                    perror("open");
                    retval = EXIT_FAILURE;
                } else {
                    int line_number = 1, line_offset;
                    off_t last_offset;
                    ssize_t line_size;

                    while (fscanf(file, "%d", &line_offset) == 1) {
                        if (line_number > 1) {
                            line_size = line_offset - last_offset;
                            index.emplace_back(
                                line_number - 1, last_offset, line_size);
                        }
                        last_offset = line_offset;
                        line_number += 1;
                    }
                    fclose(file);
                    file = NULL;
                }
            } break;
            default:
                retval = EXIT_FAILURE;
                break;
        }
    }

    argc -= optind;
    argv += optind;

    if (retval != EXIT_SUCCESS) {
    } else if ((argc == 0) && (index.size() > 0)) {
        fprintf(stderr, "error: cannot randomize stdin\n");
        retval = EXIT_FAILURE;
    } else if ((argc > 0) && (fd = open(argv[0], O_RDONLY)) == -1) {
        perror("open");
        retval = EXIT_FAILURE;
    } else if ((argc > 0) && (fstat(fd, &st) == -1)) {
        perror("fstat");
        retval = EXIT_FAILURE;
    } else if ((argc > 1) && (fd_cmp = open(argv[1], O_RDONLY)) == -1) {
        perror("open-cmp");
        retval = EXIT_FAILURE;
    } else if ((argc > 1) && (fstat(fd_cmp, &st) == -1)) {
        perror("fstat-cmp");
        retval = EXIT_FAILURE;
    } else {
        try {
            file_range last_range{offset};
            line_buffer lb;
            char* maddr;

            int fd2 = (argc > 1) ? fd_cmp.get() : fd.get();
            assert(fd2 >= 0);
            lb.set_fd(fd);
            if (index.size() == 0) {
                while (count) {
                    auto load_result = lb.load_next_line(last_range);

                    if (load_result.isErr()) {
                        break;
                    }

                    auto li = load_result.unwrap();

                    if (li.li_file_range.empty()) {
                        break;
                    }

                    auto read_result = lb.read_range(li.li_file_range);

                    if (read_result.isErr()) {
                        break;
                    }

                    auto sbr = read_result.unwrap();

                    if (!li.li_valid_utf) {
                        scrub_to_utf8(sbr.get_writable_data(), sbr.length());
                    }

                    printf("%.*s", (int) sbr.length(), sbr.get_data());
                    if ((off_t) (li.li_file_range.fr_offset
                                 + li.li_file_range.fr_size)
                        < offset) {
                        printf("\n");
                    }
                    last_range = li.li_file_range;
                    count -= 1;
                }
            } else if ((maddr = (char*) mmap(NULL,
                                             st.st_size,
                                             PROT_READ,
                                             MAP_FILE | MAP_PRIVATE,
                                             fd2,
                                             0))
                       == MAP_FAILED)
            {
                perror("mmap");
                retval = EXIT_FAILURE;
            } else {
                file_range range;

                while (true) {
                    auto load_result = lb.load_next_line(range);

                    if (load_result.isErr()) {
                        return EXIT_FAILURE;
                    }

                    auto li = load_result.unwrap();

                    range = li.li_file_range;

                    if (range.empty()) {
                        break;
                    }
                }
                do {
                    size_t lpc;

                    std::random_device rd;
                    std::mt19937 g(rd());
                    std::shuffle(index.begin(), index.end(), g);
                    for (lpc = 0; lpc < index.size(); lpc++) {
                        const auto& index_tuple = index[lpc];

                        auto read_result = lb.read_range(
                            {get<1>(index_tuple), get<2>(index_tuple)});

                        assert(read_result.isOk());

                        auto sbr = read_result.unwrap();

                        assert(memcmp(sbr.get_data(),
                                      &maddr[get<1>(index_tuple)],
                                      sbr.length())
                               == 0);
                    }

                    rnd_iters -= 1;
                } while (rnd_iters);

                printf("All done\n");
            }
        } catch (line_buffer::error& e) {
            fprintf(stderr, "error: %s\n", strerror(e.e_err));
            retval = EXIT_FAILURE;
        }
    }

    return retval;
}
