/**
 * Copyright (c) 2025, Timothy Stack
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

#include <cstdint>

#include "hasher.hh"

#if defined(__x86_64__) || defined(_M_X64)
#    include <immintrin.h>
#elif defined(__aarch64__)
#    include <arm_neon.h>
#endif

void
hasher::to_string(char out[STRING_SIZE]) const
{
#if defined(__x86_64__) || defined(_M_X64)
    // ---------- x86-64 SSE2 version ----------
    auto bytes = this->to_array();
    __m128i input
        = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes.ba_data));

    // Split into high/low nibbles
    __m128i high = _mm_and_si128(_mm_srli_epi16(input, 4), _mm_set1_epi8(0x0F));
    __m128i low = _mm_and_si128(input, _mm_set1_epi8(0x0F));

    // Interleave [h0,l0,h1,l1,...]
    __m128i lozip = _mm_unpacklo_epi8(high, low);
    __m128i hizip = _mm_unpackhi_epi8(high, low);

    // Compare nibbles >= 10
    __m128i cmp_lo = _mm_cmpgt_epi8(lozip, _mm_set1_epi8(9));
    __m128i cmp_hi = _mm_cmpgt_epi8(hizip, _mm_set1_epi8(9));

    // ASCII bases: '0' (0x30), 'a'-10 (0x57)
    __m128i base0 = _mm_set1_epi8(0x30);
    // offset = base0 + (mask ? (basea - base0) : 0)
    __m128i basediff = _mm_set1_epi8(0x57 - 0x30);

    __m128i off_lo = _mm_add_epi8(_mm_and_si128(cmp_lo, basediff), base0);
    __m128i off_hi = _mm_add_epi8(_mm_and_si128(cmp_hi, basediff), base0);

    __m128i out_lo = _mm_add_epi8(lozip, off_lo);
    __m128i out_hi = _mm_add_epi8(hizip, off_hi);

    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), out_lo);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 16), out_hi);
    out[32] = '\0';
#elif defined(__aarch64__)
    // ---------- ARM64 NEON version ----------
    auto bytes = this->to_array();
    uint8x16_t input = vld1q_u8(bytes.ba_data);

    uint8x16_t high = vshrq_n_u8(input, 4);
    uint8x16_t low = vandq_u8(input, vdupq_n_u8(0x0F));

    uint8x16x2_t zipped = vzipq_u8(high, low);
    uint8x16_t nibbles_lo = zipped.val[0];
    uint8x16_t nibbles_hi = zipped.val[1];

    uint8x16_t mask_lo = vcgtq_u8(nibbles_lo, vdupq_n_u8(9));
    uint8x16_t mask_hi = vcgtq_u8(nibbles_hi, vdupq_n_u8(9));

    uint8x16_t base0 = vdupq_n_u8(0x30);
    uint8x16_t basea = vdupq_n_u8(0x57);

    uint8x16_t off_lo = vbslq_u8(mask_lo, basea, base0);
    uint8x16_t off_hi = vbslq_u8(mask_hi, basea, base0);

    uint8x16_t out_lo = vaddq_u8(nibbles_lo, off_lo);
    uint8x16_t out_hi = vaddq_u8(nibbles_hi, off_hi);

    vst1q_u8(reinterpret_cast<uint8_t*>(out), out_lo);
    vst1q_u8(reinterpret_cast<uint8_t*>(out + 16), out_hi);
    out[32] = '\0';
#else
    static const char HEX_DIGITS[] = {'0',
                                      '1',
                                      '2',
                                      '3',
                                      '4',
                                      '5',
                                      '6',
                                      '7',
                                      '8',
                                      '9',
                                      'a',
                                      'b',
                                      'c',
                                      'd',
                                      'e',
                                      'f'};

    auto bits = this->to_array();

    out[0] = HEX_DIGITS[bits.ba_data[0] >> 4U];
    out[1] = HEX_DIGITS[bits.ba_data[0] & 0x0fU];
    out[2] = HEX_DIGITS[bits.ba_data[1] >> 4U];
    out[3] = HEX_DIGITS[bits.ba_data[1] & 0x0fU];
    out[4] = HEX_DIGITS[bits.ba_data[2] >> 4U];
    out[5] = HEX_DIGITS[bits.ba_data[2] & 0x0fU];
    out[6] = HEX_DIGITS[bits.ba_data[3] >> 4U];
    out[7] = HEX_DIGITS[bits.ba_data[3] & 0x0fU];
    out[8] = HEX_DIGITS[bits.ba_data[4] >> 4U];
    out[9] = HEX_DIGITS[bits.ba_data[4] & 0x0fU];
    out[10] = HEX_DIGITS[bits.ba_data[5] >> 4U];
    out[11] = HEX_DIGITS[bits.ba_data[5] & 0x0fU];
    out[12] = HEX_DIGITS[bits.ba_data[6] >> 4U];
    out[13] = HEX_DIGITS[bits.ba_data[6] & 0x0fU];
    out[14] = HEX_DIGITS[bits.ba_data[7] >> 4U];
    out[15] = HEX_DIGITS[bits.ba_data[7] & 0x0fU];
    out[16] = HEX_DIGITS[bits.ba_data[8] >> 4U];
    out[17] = HEX_DIGITS[bits.ba_data[8] & 0x0fU];
    out[18] = HEX_DIGITS[bits.ba_data[9] >> 4U];
    out[19] = HEX_DIGITS[bits.ba_data[9] & 0x0fU];
    out[20] = HEX_DIGITS[bits.ba_data[10] >> 4U];
    out[21] = HEX_DIGITS[bits.ba_data[10] & 0x0fU];
    out[22] = HEX_DIGITS[bits.ba_data[11] >> 4U];
    out[23] = HEX_DIGITS[bits.ba_data[11] & 0x0fU];
    out[24] = HEX_DIGITS[bits.ba_data[12] >> 4U];
    out[25] = HEX_DIGITS[bits.ba_data[12] & 0x0fU];
    out[26] = HEX_DIGITS[bits.ba_data[13] >> 4U];
    out[27] = HEX_DIGITS[bits.ba_data[13] & 0x0fU];
    out[28] = HEX_DIGITS[bits.ba_data[14] >> 4U];
    out[29] = HEX_DIGITS[bits.ba_data[14] & 0x0fU];
    out[30] = HEX_DIGITS[bits.ba_data[15] >> 4U];
    out[31] = HEX_DIGITS[bits.ba_data[15] & 0x0fU];
    out[32] = '\0';
#endif
}
