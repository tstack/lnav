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
 *
 * @file ptimec.c
 */

#include <optional>
#include <vector>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* PRELUDE = R"(
#include <time.h>
#include <sys/types.h>
#include "ptimec.hh"
#include "ptimec_spec.hh"

)";

template<std::size_t N>
static bool
startswith(const char* fmt, const char (&pat)[N])
{
    return strncmp(fmt, pat, N - 1) == 0;
}

char*
escape_char(char ch)
{
    static char charstr[4];

    if (ch == '\'') {
        strcpy(charstr, "\\'");
    } else {
        charstr[0] = ch;
        charstr[1] = '\0';
    }

    return charstr;
}

static std::optional<size_t>
spec_fixed_width(char spec)
{
    switch (spec) {
        case 'd':
        case 'H':
        case 'M':
        case 'S':
            return 2;
        case 'b':
            return 3;
        case 'Y':
            return 4;
        default:
            return std::nullopt;
    }
}

static std::optional<size_t>
spec_min_width(char spec)
{
    switch (spec) {
        case 'j':
        case 'm':
        case 'z':
            return 1;
        case 'f':
            return 0;
        case 'd':
        case 'H':
        case 'M':
        case 'S':
        case 'y':
            return 2;
        case 'b':
            return 3;
        case 'Y':
            return 4;
        default:
            return std::nullopt;
    }
}

/**
 * Check if a "%b" at the given index can have its decoding put off until the
 * rest of the format has matched.  The literal that follows gives us the
 * extent of the month name, so the name itself does not need to be looked at
 * until we know the rest of the format matched.
 */
static bool
can_defer_b(const char* fmt, int index)
{
    return fmt[index] == '%' && fmt[index + 1] == 'b'
        && fmt[index + 2] != '\0' && fmt[index + 2] != '%';
}

static bool
has_deferred_b(const char* fmt)
{
    for (int index = 0; fmt[index]; index++) {
        if (fmt[index] != '%') {
            continue;
        }
        if (can_defer_b(fmt, index)) {
            return true;
        }
        index += 1;
    }

    return false;
}

int
main(int argc, char* argv[])
{
    std::vector<char> leading_conversions;
    int retval = EXIT_SUCCESS;

    fputs(PRELUDE, stdout);
    for (int lpc = 1; lpc < argc; lpc++) {
        const char* arg = argv[lpc];
        char leading_conversion = 0;

        printf(
            "// %s\n"
            "int32_t ptime_f%d(struct exttm *dst, const char *str, off_t "
            "&off_inout, "
            "ssize_t len) {\n"
            "    dst->et_flags = 0;\n"
            "    // log_debug(\"ptime_f%d\");\n",
            arg,
            lpc,
            lpc);

        size_t min_width = 0;
        for (int index = 0; arg[index]; index++) {
            if (arg[index] == '%') {
                auto fixed_width_opt = spec_min_width(arg[index + 1]);
                if (fixed_width_opt.has_value()) {
                    min_width += fixed_width_opt.value();
                } else {
                    break;
                }
                index += 1;
            } else {
                min_width += 1;
            }
        }

        if (min_width > 0) {
            printf(
                "    if (len - off_inout < %lu) {\n"
                "        return PTIME_TOO_SHORT;\n"
                "    }\n",
                min_width);
        }

        auto deferred_b_index = std::optional<int>();
        if (has_deferred_b(arg)) {
            printf("    off_t b_start = 0, b_end = 0;\n");
        }

        auto checked_pos = std::optional<size_t>(0);
        for (int index = 0; arg[index]; index++) {
            if (startswith(&arg[index], "%Y-%m-%dT%H:%M")) {
                printf(
                    "    {\n"
                    "        auto rc = ptime_YmdTHM(dst, str, off_inout, "
                    "len);\n"
                    "        if (rc != PTIME_MATCHED) {\n");
                if (index == 0) {
                    leading_conversion = 'Y';
                    printf("            return rc;\n");
                } else {
                    // the index reported by ptime_YmdTHM is relative to the
                    // start of the prefix it matches
                    printf("            return rc < 0 ? rc : rc + %d;\n",
                           index);
                }
                printf(
                    "        }\n"
                    "    }\n");
                index += 13;
                checked_pos = std::nullopt;
            } else if (arg[index] == '%') {
                std::optional<size_t> fixed_width_opt;

                if (checked_pos.has_value()) {
                    fixed_width_opt = spec_fixed_width(arg[index + 1]);
                    if (!fixed_width_opt.has_value()) {
                        printf("    off_inout += %lu;\n", checked_pos.value());
                    }
                }

                // "%b" is left out of the leading conversion cache.  A
                // deferred "%b" reports a failure at index zero when the
                // literal that delimits the month name is missing, which is a
                // property of the format rather than of the input, so it is
                // not something other formats can be skipped on.
                if (index == 0 && arg[index + 1] != 'b') {
                    leading_conversion = arg[index + 1];
                }
                switch (arg[index + 1]) {
                    case 'a':
                        if (arg[index + 2]) {
                            printf(
                                "    if (!ptime_upto('%s', str, off_inout, "
                                "len)) "
                                "return %d;\n",
                                escape_char(arg[index + 2]),
                                index);
                        } else {
                            printf(
                                "    if (!ptime_upto_end(dst, str, "
                                "off_inout, "
                                "len)) "
                                "return %d;\n",
                                index);
                        }
                        index += 1;
                        break;
                    case 'Z':
                        if (arg[index + 2]) {
                            printf(
                                "    if (!ptime_Z_upto(dst, str, off_inout, "
                                "len, "
                                "'%s')) "
                                "return %d;\n",
                                escape_char(arg[index + 2]),
                                index);
                        } else {
                            printf(
                                "    if (!ptime_Z_upto_end(dst, str, "
                                "off_inout, "
                                "len)) "
                                "return %d;\n",
                                index);
                        }
                        index += 1;
                        break;
                    case '@':
                        printf(
                            "    if (!ptime_at(dst, str, off_inout, len)) "
                            "return %d;\n",
                            index);
                        index += 1;
                        break;
                    case 'b':
                        if (!deferred_b_index.has_value()
                            && can_defer_b(arg, index))
                        {
                            // the literal after the "%b" delimits the month
                            // name, so we can find the end of the name now and
                            // decode it once the rest of the format matches.
                            deferred_b_index = index;
                            if (fixed_width_opt) {
                                printf(
                                    "    PTIME_LOCATE_b(dst, str, off_inout + "
                                    "%lu, '%s', b_start, b_end, %d);\n",
                                    checked_pos.value(),
                                    escape_char(arg[index + 2]),
                                    index);
                                if (min_width > 0) {
                                    // locating the name may have moved
                                    // off_inout past the width that was
                                    // checked on entry.
                                    printf(
                                        "    if (len - off_inout < %lu) {\n"
                                        "        return PTIME_TOO_SHORT;\n"
                                        "    }\n",
                                        min_width);
                                }
                            } else {
                                printf(
                                    "    if (!ptime_b_locate(dst, str, "
                                    "off_inout, len, '%s', b_start, b_end)) "
                                    "return %d;\n"
                                    "    off_inout = b_end;\n",
                                    escape_char(arg[index + 2]),
                                    index);
                            }
                            index += 1;
                            break;
                        }
                        // FALLTHROUGH
                    default:
                        if (fixed_width_opt) {
                            printf(
                                "    PTIME_CHECK_%c(dst, str, off_inout + "
                                "%lu, %d);\n",
                                arg[index + 1],
                                checked_pos.value(),
                                index);
                        } else {
                            printf(
                                "    if (!ptime_%c(dst, str, off_inout, len)) "
                                "return %d;\n",
                                arg[index + 1],
                                index);
                        }
                        index += 1;
                        break;
                }
                if (checked_pos) {
                    if (fixed_width_opt.has_value()) {
                        checked_pos
                            = checked_pos.value() + fixed_width_opt.value();
                    } else {
                        checked_pos = std::nullopt;
                    }
                }
            } else {
                if (checked_pos) {
                    printf(
                        "    PTIME_CHECK_CHAR('%s', str[off_inout + %lu], %d);\n",
                        escape_char(arg[index]),
                        checked_pos.value(),
                        index);
                    checked_pos = checked_pos.value() + 1;
                } else {
                    printf(
                        "    if (!ptime_char('%s', str, off_inout, len)) "
                        "return %d;\n",
                        escape_char(arg[index]),
                        index);
                }
            }
        }
        if (deferred_b_index.has_value()) {
            printf("    PTIME_FINISH_b(dst, str, b_start, b_end, %d);\n",
                   deferred_b_index.value());
        }
        if (checked_pos.has_value()) {
            printf("    off_inout += %lu;\n", min_width);
        }
        printf("    return PTIME_MATCHED;\n");
        printf("}\n\n");

        if (leading_conversion != 0
            && (leading_conversion < '@' || leading_conversion > 'z'))
        {
            fprintf(stderr,
                    "error: leading conversion '%%%c' of '%s' is outside the "
                    "range that ptime_leading_conversion_flag() can encode\n",
                    leading_conversion,
                    arg);
            retval = EXIT_FAILURE;
        }
        leading_conversions.emplace_back(leading_conversion);
    }
    for (int lpc = 1; lpc < argc; lpc++) {
        const char* arg = argv[lpc];

        printf(
            "void ftime_f%d(char *dst, off_t &off_inout, size_t len, const "
            "struct exttm &tm) {\n",
            lpc);
        for (int index = 0; arg[index]; arg++) {
            if (arg[index] == '%') {
                switch (arg[index + 1]) {
                    case '@':
                        printf("    ftime_at(dst, off_inout, len, tm);\n");
                        arg += 1;
                        break;
                    default:
                        printf("    ftime_%c(dst, off_inout, len, tm);\n",
                               arg[index + 1]);
                        arg += 1;
                        break;
                }
            } else {
                printf("    ftime_char(dst, off_inout, len, '%s');\n",
                       escape_char(arg[index]));
            }
        }
        printf("    dst[off_inout] = '\\0';\n");
        printf("}\n\n");
    }

    size_t default_format_index = 0;
    printf("struct ptime_fmt PTIMEC_FORMATS[] = {\n");
    for (int lpc = 1; lpc < argc; lpc++) {
        if (strcmp(argv[lpc], "%Y-%m-%dT%H:%M:%S") == 0) {
            default_format_index = lpc - 1;
        }
        printf("    { \"%s\", ptime_f%d, ftime_f%d, 0x%x },\n",
               argv[lpc],
               lpc,
               lpc,
               leading_conversions[lpc - 1]);
    }
    printf("\n");
    printf("    { nullptr, nullptr, nullptr, 0 }\n");
    printf("};\n");

    printf("const char *PTIMEC_FORMAT_STR[] = {\n");
    for (int lpc = 1; lpc < argc; lpc++) {
        printf("    \"%s\",\n", argv[lpc]);
    }
    printf("\n");
    printf("    nullptr\n");
    printf("};\n");

    printf("\n");
    printf("size_t PTIMEC_DEFAULT_FMT_INDEX = %zu;\n", default_format_index);

    return retval;
}
