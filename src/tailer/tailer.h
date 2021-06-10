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

#ifndef lnav_tailer_h
#define lnav_tailer_h

#ifndef __COSMOPOLITAN__
#include <sys/types.h>
#endif

typedef enum {
    TPPT_DONE,
    TPPT_STRING,
    TPPT_HASH,
    TPPT_INT64,
    TPPT_BITS,
} tailer_packet_payload_type_t;

typedef enum {
    TPT_ERROR,
    TPT_OPEN_PATH,
    TPT_CLOSE_PATH,
    TPT_OFFER_BLOCK,
    TPT_NEED_BLOCK,
    TPT_ACK_BLOCK,
    TPT_TAIL_BLOCK,
    TPT_LINK_BLOCK,
    TPT_SYNCED,
    TPT_LOG,
    TPT_LOAD_PREVIEW,
    TPT_PREVIEW_ERROR,
    TPT_PREVIEW_DATA,
    TPT_COMPLETE_PATH,
    TPT_POSSIBLE_PATH,
    TPT_ANNOUNCE,
} tailer_packet_type_t;

#ifdef __cplusplus
extern "C" {
#endif

ssize_t send_packet(int fd,
                    tailer_packet_type_t tpt,
                    tailer_packet_payload_type_t payload_type,
                    ...);

#ifdef __cplusplus
};
#endif

#endif
