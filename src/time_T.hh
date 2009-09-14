
#ifndef __time_t_hh
#define __time_t_hh

#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#define timeit(_block) { \
    struct timeval _st, _en, _diff; \
\
    gettimeofday(&_st, NULL); \
    { _block; } \
    gettimeofday(&_en, NULL); \
    timersub(&_en, &_st, &_diff); \
    fprintf(stderr, \
	    "%s %d.%06d\n", \
	    #_block, _diff.tv_sec, _diff.tv_usec); \
    fflush(stderr); \
}

#endif
