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

#include <algorithm>
#include <map>
#include <optional>
#include <vector>

#include <limits.h>

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
    // The formats are grouped by what they lead with, so that a lead which
    // fails rules out every format sharing it.  The key is the conversion
    // character paired with the literal that follows it, since a conversion
    // that scans up to a delimiter fails when that delimiter is missing, which
    // says nothing about a format looking for a different one.  Zero means the
    // format leads with something that cannot be grouped on.
    std::vector<int> leading_conversions;
    int retval = EXIT_SUCCESS;

    fputs(PRELUDE, stdout);
    for (int lpc = 1; lpc < argc; lpc++) {
        const char* arg = argv[lpc];
        int leading_conversion = 0;

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
        // A leading "%a" scans up to a delimiter without looking at what it
        // skipped, so the format does not start telling inputs apart until the
        // element after it.  Reporting the failures in that shared prefix as
        // index zero lets scan() drop the whole group of formats that start
        // this way, since they all begin with the same thing.
        auto fail_is_lead = arg[0] == '%' && arg[1] == 'a';
        for (int index = 0; arg[index]; index++) {
            const auto fail_index = fail_is_lead ? 0 : index;

            if (startswith(&arg[index], "%Y-%m-%dT%H:%M")) {
                printf(
                    "    {\n"
                    "        auto rc = ptime_YmdTHM(dst, str, off_inout, "
                    "len);\n"
                    "        if (rc != PTIME_MATCHED) {\n");
                if (index == 0) {
                    leading_conversion = 'Y' << 8;
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

                const auto conv_ch = arg[index + 1];

                if (index == 0) {
                    auto delim = 0;

                    // These scan up to a literal instead of consuming a fixed
                    // width, so the literal they are looking for is part of
                    // what they lead with.
                    if (conv_ch == 'a' || conv_ch == 'b' || conv_ch == 'Z') {
                        delim = (unsigned char) arg[index + 2];
                    }
                    leading_conversion = (conv_ch << 8) | delim;
                }

                switch (conv_ch) {
                    case 'a':
                        if (arg[index + 2]) {
                            printf(
                                "    if (!ptime_a_upto('%s', str, off_inout, "
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
                            deferred_b_index = fail_index;
                            if (fixed_width_opt) {
                                printf(
                                    "    PTIME_LOCATE_b(dst, str, off_inout + "
                                    "%lu, '%s', b_start, b_end, %d);\n",
                                    checked_pos.value(),
                                    escape_char(arg[index + 2]),
                                    fail_index);
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
                                    fail_index);
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
                                fail_index);
                        } else {
                            printf(
                                "    if (!ptime_%c(dst, str, off_inout, len)) "
                                "return %d;\n",
                                arg[index + 1],
                                fail_index);
                        }
                        index += 1;
                        break;
                }
                if (conv_ch != 'a') {
                    fail_is_lead = false;
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
                        fail_index);
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
            && ((leading_conversion >> 8) < '@' || (leading_conversion >> 8) > 'z'))
        {
            fprintf(stderr,
                    "error: leading conversion '%%%c' of '%s' is outside the "
                    "range that pf_leading_conversion can hold\n",
                    leading_conversion >> 8,
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

    // Group the formats by their leading conversion so that a conversion
    // appears in exactly one run.  scan() can then step over a whole run when
    // that conversion fails, instead of looking at every format sharing it.
    // The sort is stable, so formats with the same conversion keep the order
    // they have in the list and a longer variant still comes before the prefix
    // it extends.  A zero conversion sorts first, which leaves the epoch
    // format at index 0, where scan() expects to find it.
    std::vector<int> order;

    for (int lpc = 1; lpc < argc; lpc++) {
        order.emplace_back(lpc);
    }

    // A group takes the place of its first format, so the groups stay in the
    // order the list puts them in.  Ordering by the conversion character
    // instead would arrange them by ASCII value, which says nothing about the
    // precedence the list was written with.
    std::map<int, int> first_appearance;
    for (int lpc = 1; lpc < argc; lpc++) {
        const auto conv = leading_conversions[lpc - 1];

        if (conv != 0) {
            first_appearance.emplace(conv, lpc);
        }
    }

    // A format with no leading conversion is never skipped, so gathering those
    // together buys nothing and would only move them past formats they
    // currently precede.  Leaving them where the list has them also keeps the
    // epoch format at index 0, where scan() looks for it.
    auto sort_key = [&leading_conversions, &first_appearance](int idx) {
        const auto conv = leading_conversions[idx - 1];

        return conv == 0 ? idx : first_appearance.find(conv)->second;
    };
    std::stable_sort(order.begin(),
                     order.end(),
                     [&sort_key](int lhs, int rhs) {
                         return sort_key(lhs) < sort_key(rhs);
                     });

    std::vector<size_t> next_group(order.size());
    for (size_t lpc = 0; lpc < order.size(); lpc++) {
        const auto conv = leading_conversions[order[lpc] - 1];
        auto next = lpc + 1;

        // A format with no leading conversion is never skipped, so the group
        // it is in is just itself.
        if (conv != 0) {
            while (next < order.size()
                   && leading_conversions[order[next] - 1] == conv)
            {
                next += 1;
            }
        }
        next_group[lpc] = next;
    }

    size_t default_format_index = 0;
    printf("struct ptime_fmt PTIMEC_FORMATS[] = {\n");
    for (size_t lpc = 0; lpc < order.size(); lpc++) {
        const auto src = order[lpc];

        if (strcmp(argv[src], "%Y-%m-%dT%H:%M:%S") == 0) {
            default_format_index = lpc;
        }
        printf("    { \"%s\", ptime_f%d, ftime_f%d, 0x%x, %zu },\n",
               argv[src],
               src,
               src,
               leading_conversions[src - 1] >> 8,
               next_group[lpc]);
    }
    printf("\n");
    printf("    { nullptr, nullptr, nullptr, 0, 0 }\n");
    printf("};\n");

    printf("const char *PTIMEC_FORMAT_STR[] = {\n");
    for (size_t lpc = 0; lpc < order.size(); lpc++) {
        printf("    \"%s\",\n", argv[order[lpc]]);
    }
    printf("\n");
    printf("    nullptr\n");
    printf("};\n");

    printf("\n");
    printf("size_t PTIMEC_DEFAULT_FMT_INDEX = %zu;\n", default_format_index);

    return retval;
}
