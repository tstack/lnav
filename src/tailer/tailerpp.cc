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

#include "tailerpp.hh"

namespace tailer {

int readall(int sock, void *buf, size_t len)
{
    char *cbuf = (char *) buf;
    off_t offset = 0;

    while (len > 0) {
        ssize_t rc = read(sock, &cbuf[offset], len);

        if (rc == -1) {
            switch (errno) {
                case EAGAIN:
                case EINTR:
                    break;
                default:
                    return -1;
            }
        } else if (rc == 0) {
            errno = EIO;
            return -1;
        } else {
            len -= rc;
            offset += rc;
        }
    }

    return 0;
}

Result<packet, std::string> read_packet(int fd)
{
    tailer_packet_type_t type;

    if (readall(fd, &type, sizeof(type)) == -1) {
        return Ok(packet{packet_eof{}});
    }
    switch (type) {
        case TPT_ERROR: {
            packet_error pe;

            TRY(read_payloads_into(fd, pe.pe_path, pe.pe_msg));
            return Ok(packet{pe});
        }
        case TPT_OFFER_BLOCK: {
            packet_offer_block pob;

            TRY(read_payloads_into(fd,
                                   pob.pob_path,
                                   pob.pob_offset,
                                   pob.pob_length,
                                   pob.pob_hash));
            return Ok(packet{pob});
        }
        case TPT_TAIL_BLOCK: {
            packet_tail_block ptb;

            TRY(read_payloads_into(fd,
                                   ptb.ptb_path,
                                   ptb.ptb_offset,
                                   ptb.ptb_bits));
            return Ok(packet{ptb});
        }
        default:
            assert(0);
            break;
    }
}


}
