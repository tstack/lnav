/**
 * Copyright (c) 2021, Timothy Stack
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

#ifndef __COSMOPOLITAN__
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#endif

#include "sha-256.h"
#include "tailer.h"

ssize_t send_packet(int fd,
                    tailer_packet_type_t tpt,
                    tailer_packet_payload_type_t payload_type,
                    ...)
{
    va_list args;
    int done = 0;

    va_start(args, payload_type);
    write(fd, &tpt, sizeof(tpt));
    do {
        write(fd, &payload_type, sizeof(payload_type));
        switch (payload_type) {
            case TPPT_STRING: {
                char *str = va_arg(args, char *);
                uint32_t length = strlen(str);

                write(fd, &length, sizeof(length));
                write(fd, str, length);
                break;
            }
            case TPPT_HASH: {
                const char *hash = va_arg(args, const char *);

                write(fd, hash, SHA256_BLOCK_SIZE);
                break;
            }
            case TPPT_INT64: {
                int64_t i = va_arg(args, int64_t);

                write(fd, &i, sizeof(i));
                break;
            }
            case TPPT_BITS: {
                int32_t length = va_arg(args, int32_t);
                const char *bits = va_arg(args, const char *);

                write(fd, &length, sizeof(length));
                write(fd, bits, length);
                break;
            }
            case TPPT_DONE: {
                done = 1;
                break;
            }
            default: {
                assert(0);
                break;
            }
        }

        if (!done) {
            payload_type = va_arg(args, tailer_packet_payload_type_t);
        }
    } while (!done);
    va_end(args);

    return 0;
}
