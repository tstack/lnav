/* -*- mode: c; c-file-style: "k&r" -*-

  strnatcmp.c -- Perform 'natural order' comparisons of strings in C.
  Copyright (C) 2000, 2004 by Martin Pool <mbp sourcefrog net>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/


/* partial change history:
 *
 * 2004-10-10 mbp: Lift out character type dependencies into macros.
 *
 * Eric Sosman pointed out that ctype functions take a parameter whose
 * value must be that of an unsigned int, even on platforms that have
 * negative chars in their default char type.
 */

#include <assert.h>
#include <ctype.h>

#include "strnatcmp.h"


/* These are defined as macros to make it easier to adapt this code to
 * different characters types or comparison functions. */
static inline int
nat_isdigit(nat_char a)
{
	return isdigit((unsigned char) a);
}


static inline int
nat_isspace(nat_char a)
{
	return isspace((unsigned char) a);
}


static inline nat_char
nat_toupper(nat_char a)
{
	return toupper((unsigned char) a);
}



static int
compare_right(int a_len, nat_char const *a, int b_len, nat_char const *b, int *len_out)
{
	int bias = 0;

	/* The longest run of digits wins.  That aside, the greatest
	value wins, but we can't know that it will until we've scanned
	both numbers to know that they have the same magnitude, so we
	remember it in BIAS. */
	for (;; a++, b++, a_len--, b_len--, (*len_out)++) {
		if (a_len == 0 && b_len == 0)
			return bias;
		if (a_len == 0)
			return -1;
		if (b_len == 0)
			return 1;
		if (!nat_isdigit(*a) && !nat_isdigit(*b))
			return bias;
		else if (!nat_isdigit(*a))
			return -1;
		else if (!nat_isdigit(*b))
			return +1;
		else if (*a < *b) {
			if (!bias)
				bias = -1;
		} else if (*a > *b) {
			if (!bias)
				bias = +1;
		} else if (!*a && !*b)
			return bias;
	}

	return 0;
}

static int
compare_left(int a_len, nat_char const *a, int b_len, nat_char const *b, int *len_out)
{
     /* Compare two left-aligned numbers: the first to have a
        different value wins. */
     for (;; a++, b++, a_len--, b_len--, (*len_out)++) {
     	  if (a_len == 0 && b_len == 0)
     	  	return 0;
     	  if (a_len == 0)
     	  	return -1;
     	  if (b_len == 0)
     	  	return 1;
	  if (!nat_isdigit(*a)  &&  !nat_isdigit(*b))
	       return 0;
	  else if (!nat_isdigit(*a))
	       return -1;
	  else if (!nat_isdigit(*b))
	       return +1;
	  else if (*a < *b)
	       return -1;
	  else if (*a > *b)
	       return +1;
     }
	  
     return 0;
}

static int strnatcmp0(int a_len, nat_char const *a,
                      int b_len, nat_char const *b,
                      int fold_case)
{
	int ai, bi;
	nat_char ca, cb;
	int fractional, result;

	assert(a && b);
	ai = bi = 0;
	while (1) {
		if (ai >= a_len)
			ca = 0;
		else
			ca = a[ai];
		if (bi >= b_len)
			cb = 0;
		else
			cb = b[bi];

		/* skip over leading spaces or zeros */
		while (nat_isspace(ca)) {
			ai += 1;
			if (ai >= a_len)
				ca = 0;
			else
				ca = a[ai];
		}

		while (nat_isspace(cb)) {
			bi += 1;
			if (bi >= b_len)
				cb = 0;
			else
				cb = b[bi];
		}

		/* process run of digits */
		if (nat_isdigit(ca) && nat_isdigit(cb)) {
            int num_len = 0;

			fractional = (ca == '0' || cb == '0');

			if (fractional) {
				if ((result = compare_left(a_len - ai, a + ai, b_len - bi,
										   b + bi, &num_len)) != 0) {
                    return result;
                }
			} else {
				if ((result = compare_right(a_len - ai, a + ai, b_len - bi,
											b + bi, &num_len)) != 0) {
                    return result;
                }
			}

            ai += num_len;
            bi += num_len;
            continue;
		}

		if (!ca && !cb) {
			/* The strings compare the same.  Perhaps the caller
                   will want to call strcmp to break the tie. */
			return 0;
		}

		if (fold_case) {
			ca = nat_toupper(ca);
			cb = nat_toupper(cb);
		}

		if (ca < cb)
			return -1;
		else if (ca > cb)
			return +1;

		++ai;
		++bi;
	}
}

int ipv4cmp(int a_len, nat_char const *a,
            int b_len, nat_char const *b,
            int *res_out)
{
    int ai, bi;
    nat_char ca, cb;
    int fractional, result = 0;

    assert(a && b);
    ai = bi = 0;
    while (result == 0) {
        if (ai >= a_len)
            ca = 0;
        else
            ca = a[ai];
        if (bi >= b_len)
            cb = 0;
        else
            cb = b[bi];

        /* skip over leading spaces or zeros */
        while (nat_isspace(ca)) {
            ai += 1;
            if (ai >= a_len)
                ca = 0;
            else
                ca = a[ai];
        }

        while (nat_isspace(cb)) {
            bi += 1;
            if (bi >= b_len)
                cb = 0;
            else
                cb = b[bi];
        }

        /* process run of digits */
        if (nat_isdigit(ca) && nat_isdigit(cb)) {
            int num_len = 0;

            fractional = (ca == '0' || cb == '0');

            if (fractional) {
                result = compare_left(a_len - ai, a + ai, b_len - bi,
                                      b + bi, &num_len);
            } else {
                result = compare_right(a_len - ai, a + ai, b_len - bi,
                                       b + bi, &num_len);
            }

            ai += num_len;
            bi += num_len;
            continue;
        }

        if (!ca && !cb) {
            /* The strings compare the same.  Perhaps the caller
                   will want to call strcmp to break the tie. */
            *res_out = result;
            return 1;
        }

        if (ca != '.' || cb != '.') {
            return 0;
        }

        ++ai;
        ++bi;
    }

    for (; ai < a_len; ai++) {
        if (!isdigit((unsigned char)a[ai]) && a[ai] != '.') {
            return 0;
        }
    }

    for (; bi < b_len; bi++) {
        if (!isdigit((unsigned char)b[bi]) && b[bi] != '.') {
            return 0;
        }
    }

    *res_out = result;
    return 1;
}

int strnatcmp(int a_len, nat_char const *a, int b_len, nat_char const *b)
{
	return strnatcmp0(a_len, a, b_len, b, 0);
}

/* Compare, recognizing numeric string and ignoring case. */
int strnatcasecmp(int a_len, nat_char const *a, int b_len, nat_char const *b)
{
	return strnatcmp0(a_len, a, b_len, b, 1);
}
