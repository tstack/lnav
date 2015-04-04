#include "config.h"

#include <time.h>
#include <sys/time.h>

time_t time(time_t *loc)
{
    time_t retval = 1370546000;

    if (loc != NULL) {
        *loc = retval;
    }

    return retval;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    tv->tv_sec = 1370546000;
    tv->tv_usec = 123456;

    return 0;
}
