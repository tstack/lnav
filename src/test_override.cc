#include "config.h"

#include <time.h>

time_t time(time_t *loc)
{
    time_t retval = 1370546000;

    if (loc != NULL) {
        *loc = retval;
    }

    return retval;
}
