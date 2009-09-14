/**
 * @file lnav_util.hh
 *
 * Dumping ground for useful functions with no other home.
 */

#ifndef __lnav_util_hh
#define __lnav_util_hh

#include <sys/types.h>

/**
 * Round down a number based on a given granularity.
 *
 * @param
 * @param step The granularity.
 */
inline int rounddown(size_t size, int step)
{
    return (size - (size % step));
}

inline int rounddown_offset(size_t size, int step, int offset)
{
    return (size - ((size - offset) % step));
}

inline int roundup(size_t size, int step)
{
    int retval = size + step;

    retval -= (retval % step);
    
    return retval;
}

inline time_t day_num(time_t ti)
{
    return ti / (24 * 60 * 60);
}

inline time_t hour_num(time_t ti)
{
    return ti / (60 * 60);
}

#if SIZEOF_OFF_T == 8
#define FORMAT_OFF_T "%qd"
#elif SIZEOF_OFF_T == 4
#define FORMAT_OFF_T "%ld"
#else
#error "off_t has unhandled size..."
#endif

#endif
