#! /usr/bin/env python

import sys
import time
import datetime
import random

DATE_FMT = "%a %b %d %H:%M:%S %Y"

duration = [] + [80] * 10 + [100] * 10 + [40] * 10

diter = iter(duration)

DURATIONS = (
    40,
    40,
    40,
    40,
    40,
    40,
    40,
    40,
    40,
    40,
    40,
    40,
    40,
    50,
    50,
    50,
    50,
    75,
    75,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
    100,
)

DURATION_FUZZ = (
    0,
    0,
    0,
    0,
    0,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -2,
    -2,
    -2
)

while True:
    print ("[pid: 88186|app: 0|req: 5/19] 127.0.0.1 () {38 vars in 696 bytes} "
           "[%s] POST /update_metrics => generated 47 bytes "
           "in %s msecs (HTTP/1.1 200) 9 headers in 378 bytes (1 switches on core 60)" %
           (datetime.datetime.utcnow().strftime(DATE_FMT),
            random.choice(DURATIONS) + random.choice(DURATION_FUZZ)))
            # diter.next()))
    sys.stdout.flush()

    time.sleep(0.01)
