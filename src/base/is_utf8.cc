/*
 * is_utf8 is distributed under the following terms:
 *
 * Copyright (c) 2013 Palard Julien. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "is_utf8.hh"

#include "config.h"

/*
  Check if the given unsigned char * is a valid utf-8 sequence.

  Return value :
  If the string is valid utf-8, 0 is returned.
  Else the position, starting from 1, is returned.

  Source:
   http://www.unicode.org/versions/Unicode7.0.0/UnicodeStandard-7.0.pdf
   page 124, 3.9 "Unicode Encoding Forms", "UTF-8"


  Table 3-7. Well-Formed UTF-8 Byte Sequences
  -----------------------------------------------------------------------------
  |  Code Points        | First Byte | Second Byte | Third Byte | Fourth Byte |
  |  U+0000..U+007F     |     00..7F |             |            |             |
  |  U+0080..U+07FF     |     C2..DF |      80..BF |            |             |
  |  U+0800..U+0FFF     |         E0 |      A0..BF |     80..BF |             |
  |  U+1000..U+CFFF     |     E1..EC |      80..BF |     80..BF |             |
  |  U+D000..U+D7FF     |         ED |      80..9F |     80..BF |             |
  |  U+E000..U+FFFF     |     EE..EF |      80..BF |     80..BF |             |
  |  U+10000..U+3FFFF   |         F0 |      90..BF |     80..BF |      80..BF |
  |  U+40000..U+FFFFF   |     F1..F3 |      80..BF |     80..BF |      80..BF |
  |  U+100000..U+10FFFF |         F4 |      80..8F |     80..BF |      80..BF |
  -----------------------------------------------------------------------------

  Returns the first erroneous byte position, and give in
  `faulty_bytes` the number of actually existing bytes taking part in this
  error.
*/
utf8_scan_result
is_utf8(const unsigned char* str,
        size_t len,
        const char** message,
        int* faulty_bytes,
        nonstd::optional<unsigned char> terminator)
{
    bool has_ansi = false;
    ssize_t i = 0;

    *message = nullptr;
    *faulty_bytes = 0;
    while (i < len) {
        if (str[i] == '\x1b') {
            has_ansi = true;
        }

        if (terminator && str[i] == terminator.value()) {
            *message = nullptr;
            return {i, has_ansi};
        }

        if (str[i] <= 0x7F) /* 00..7F */ {
            i += 1;
        } else if (str[i] >= 0xC2 && str[i] <= 0xDF) /* C2..DF 80..BF */ {
            if (i + 1 < len) /* Expect a 2nd byte */ {
                if (str[i + 1] < 0x80 || str[i + 1] > 0xBF) {
                    *message
                        = "After a first byte between C2 and DF, expecting a "
                          "2nd byte between 80 and BF";
                    *faulty_bytes = 2;
                    return {i, has_ansi};
                }
            } else {
                *message
                    = "After a first byte between C2 and DF, expecting a 2nd "
                      "byte.";
                *faulty_bytes = 1;
                return {i, has_ansi};
            }
            i += 2;
        } else if (str[i] == 0xE0) /* E0 A0..BF 80..BF */ {
            if (i + 2 < len) /* Expect a 2nd and 3rd byte */ {
                if (str[i + 1] < 0xA0 || str[i + 1] > 0xBF) {
                    *message
                        = "After a first byte of E0, expecting a 2nd byte "
                          "between A0 and BF.";
                    *faulty_bytes = 2;
                    return {i, has_ansi};
                }
                if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
                    *message
                        = "After a first byte of E0, expecting a 3nd byte "
                          "between 80 and BF.";
                    *faulty_bytes = 3;
                    return {i, has_ansi};
                }
            } else {
                *message
                    = "After a first byte of E0, expecting two following "
                      "bytes.";
                *faulty_bytes = 1;
                return {i, has_ansi};
            }
            i += 3;
        } else if (str[i] >= 0xE1 && str[i] <= 0xEC) /* E1..EC 80..BF 80..BF */
        {
            if (i + 2 < len) /* Expect a 2nd and 3rd byte */ {
                if (str[i + 1] < 0x80 || str[i + 1] > 0xBF) {
                    *message
                        = "After a first byte between E1 and EC, expecting the "
                          "2nd byte between 80 and BF.";
                    *faulty_bytes = 2;
                    return {i, has_ansi};
                }
                if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
                    *message
                        = "After a first byte between E1 and EC, expecting the "
                          "3rd byte between 80 and BF.";
                    *faulty_bytes = 3;
                    return {i, has_ansi};
                }
            } else {
                *message
                    = "After a first byte between E1 and EC, expecting two "
                      "following bytes.";
                *faulty_bytes = 1;
                return {i, has_ansi};
            }
            i += 3;
        } else if (str[i] == 0xED) /* ED 80..9F 80..BF */ {
            if (i + 2 < len) /* Expect a 2nd and 3rd byte */ {
                if (str[i + 1] < 0x80 || str[i + 1] > 0x9F) {
                    *message
                        = "After a first byte of ED, expecting 2nd byte "
                          "between 80 and 9F.";
                    *faulty_bytes = 2;
                    return {i, has_ansi};
                }
                if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
                    *message
                        = "After a first byte of ED, expecting 3rd byte "
                          "between 80 and BF.";
                    *faulty_bytes = 3;
                    return {i, has_ansi};
                }
            } else {
                *message
                    = "After a first byte of ED, expecting two following "
                      "bytes.";
                *faulty_bytes = 1;
                return {i, has_ansi};
            }
            i += 3;
        } else if (str[i] >= 0xEE && str[i] <= 0xEF) /* EE..EF 80..BF 80..BF */
        {
            if (i + 2 < len) /* Expect a 2nd and 3rd byte */ {
                if (str[i + 1] < 0x80 || str[i + 1] > 0xBF) {
                    *message
                        = "After a first byte between EE and EF, expecting 2nd "
                          "byte between 80 and BF.";
                    *faulty_bytes = 2;
                    return {i, has_ansi};
                }
                if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
                    *message
                        = "After a first byte between EE and EF, expecting 3rd "
                          "byte between 80 and BF.";
                    *faulty_bytes = 3;
                    return {i, has_ansi};
                }
            } else {
                *message
                    = "After a first byte between EE and EF, two following "
                      "bytes.";
                *faulty_bytes = 1;
                return {i, has_ansi};
            }
            i += 3;
        } else if (str[i] == 0xF0) /* F0 90..BF 80..BF 80..BF */ {
            if (i + 3 < len) /* Expect a 2nd, 3rd 3th byte */ {
                if (str[i + 1] < 0x90 || str[i + 1] > 0xBF) {
                    *message
                        = "After a first byte of F0, expecting 2nd byte "
                          "between 90 and BF.";
                    *faulty_bytes = 2;
                    return {i, has_ansi};
                }
                if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
                    *message
                        = "After a first byte of F0, expecting 3rd byte "
                          "between 80 and BF.";
                    *faulty_bytes = 3;
                    return {i, has_ansi};
                }
                if (str[i + 3] < 0x80 || str[i + 3] > 0xBF) {
                    *message
                        = "After a first byte of F0, expecting 4th byte "
                          "between 80 and BF.";
                    *faulty_bytes = 4;
                    return {i, has_ansi};
                }
            } else {
                *message
                    = "After a first byte of F0, expecting three following "
                      "bytes.";
                *faulty_bytes = 1;
                return {i, has_ansi};
            }
            i += 4;
        } else if (str[i] >= 0xF1
                   && str[i] <= 0xF3) /* F1..F3 80..BF 80..BF 80..BF */
        {
            if (i + 3 < len) /* Expect a 2nd, 3rd 3th byte */ {
                if (str[i + 1] < 0x80 || str[i + 1] > 0xBF) {
                    *message
                        = "After a first byte of F1, F2, or F3, expecting a "
                          "2nd byte between 80 and BF.";
                    *faulty_bytes = 2;
                    return {i, has_ansi};
                }
                if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
                    *message
                        = "After a first byte of F1, F2, or F3, expecting a "
                          "3rd byte between 80 and BF.";
                    *faulty_bytes = 3;
                    return {i, has_ansi};
                }
                if (str[i + 3] < 0x80 || str[i + 3] > 0xBF) {
                    *message
                        = "After a first byte of F1, F2, or F3, expecting a "
                          "4th byte between 80 and BF.";
                    *faulty_bytes = 4;
                    return {i, has_ansi};
                }
            } else {
                *message
                    = "After a first byte of F1, F2, or F3, expecting three "
                      "following bytes.";
                *faulty_bytes = 1;
                return {i, has_ansi};
            }
            i += 4;
        } else if (str[i] == 0xF4) /* F4 80..8F 80..BF 80..BF */ {
            if (i + 3 < len) /* Expect a 2nd, 3rd 3th byte */ {
                if (str[i + 1] < 0x80 || str[i + 1] > 0x8F) {
                    *message
                        = "After a first byte of F4, expecting 2nd byte "
                          "between 80 and 8F.";
                    *faulty_bytes = 2;
                    return {i, has_ansi};
                }
                if (str[i + 2] < 0x80 || str[i + 2] > 0xBF) {
                    *message
                        = "After a first byte of F4, expecting 3rd byte "
                          "between 80 and BF.";
                    *faulty_bytes = 3;
                    return {i, has_ansi};
                }
                if (str[i + 3] < 0x80 || str[i + 3] > 0xBF) {
                    *message
                        = "After a first byte of F4, expecting 4th byte "
                          "between 80 and BF.";
                    *faulty_bytes = 4;
                    return {i, has_ansi};
                }
            } else {
                *message
                    = "After a first byte of F4, expecting three following "
                      "bytes.";
                *faulty_bytes = 1;
                return {i, has_ansi};
            }
            i += 4;
        } else {
            *message
                = "Expecting bytes in the following ranges: 00..7F C2..F4.";
            *faulty_bytes = 1;
            return {i, has_ansi};
        }
    }
    return {-1, has_ansi};
}
