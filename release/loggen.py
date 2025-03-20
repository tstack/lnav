#! /usr/bin/env python3

import os
import sys
import time
import uuid
import shutil
import random
import datetime

SYSLOG_DATE_FMT = "%b %d %H:%M:%S"
ACCESS_LOG_DATE_FMT = "%d/%b/%Y:%H:%M:%S"
GENERIC_DATE_FMT = "%Y-%m-%dT%H:%M:%S.%%s"

try:
    shutil.rmtree("/tmp/demo")
except OSError:
    pass

try:
    os.makedirs("/tmp/demo")
except OSError:
    pass

TEST_ADDRESSES = (
        ["192.0.2.55"] * 20 +
        ["192.0.2.44"] * 20 +
        ["192.0.2.3"] * 40 +
        ["192.0.2.100"] * 10 +
        ["192.0.2.122"] * 30 +
        ["192.0.2.42"] * 100
)

TEST_USERNAMES = [
    "bob@example.com",
    "bob@example.com",
    "bob@example.com",
    "combatcarl@example.com",
    "combatcarl@example.com",
    "combatcarl@example.com",
    "combatcarl@example.com",
    "combatcarl@example.com",
    "combatcarl@example.com",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
]

TEST_METHODS = [
    "GET",
    "GET",
    "GET",
    "GET",
    "GET",
    "GET",
    "PUT",
]

PATH_COMPS = "Lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod tempor incididunt ut labore et dolore magna aliqua".split()

TEST_URLS = [
    "/index.html",
    "/index.html",
    "/index.html",
    "/features.html",
    "/images/compass.jpg",
    "/obj/1234",
    "/obj/1235?foo=bar",
    "/obj/1236?search=demo&start=1",
]

for index in range(0, 50):
    path_list = []
    for count in range(random.randint(2, 8)):
        path_list.append(random.choice(PATH_COMPS))
    TEST_URLS.append("/".join(path_list))

TEST_VERSIONS = [
    "HTTP/1.0",
    "HTTP/1.0",
    "HTTP/1.1",
]

TEST_STATUS = [
    200,
    200,
    200,
    200,
    200,
    200,
    200,
    200,
    200,
    200,
    404,
    404,
    404,
    404,
    403,
    403,
    403,
    500
]

TEST_REFERRERS = [
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "http://lnav.org/download.html",
]

TEST_AGENTS = [
    "-",
    "-",
    "-",
    "-",
    "Apache-HttpClient/4.2.3 (java 1.5)",
    "Apache-HttpClient/4.2.3 (java 1.5)",
    "Apache-HttpClient/4.2.3 (java 1.5)",
    "Apache-HttpClient/4.2.3 (java 1.5)",
    "Apache-HttpClient/4.2.3 (java 1.5)",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36",
    "Roku4640X/DVP-7.70 (297.70E04154A)",
    "Roku4640X/DVP-7.70 (297.70E04154A)",
    "Roku4640X/DVP-7.70 (297.70E04154A)",
]

START_TIME = datetime.datetime.fromtimestamp(1692700000)
ACCESS_LOG_CURR_TIME = START_TIME
SYSLOG_LOG_CURR_TIME = START_TIME


def access_log_msgs():
    global ACCESS_LOG_CURR_TIME
    while True:
        ACCESS_LOG_CURR_TIME += datetime.timedelta(seconds=random.randrange(1, 5))
        yield '%s - %s [%s +0000] "%s %s %s" %s %s "%s" "%s"\n' % (
            random.choice(TEST_ADDRESSES),
            random.choice(TEST_USERNAMES),
            ACCESS_LOG_CURR_TIME.strftime(ACCESS_LOG_DATE_FMT),
            random.choice(TEST_METHODS),
            random.choice(TEST_URLS),
            random.choice(TEST_VERSIONS),
            random.choice(TEST_STATUS),
            int(random.lognormvariate(1, 1) * 1000),
            random.choice(TEST_REFERRERS),
            random.choice(TEST_AGENTS)
        )


TEST_PROCS = [
    "server[123]",
    "server[123]",
    "server[123]",
    "server[121]",
    "server[124]",
    "server[123]",
    "worker[61456]",
    "worker[61456]",
    "worker[61457]",
]

TEST_MSGS = [
    "Handling request %s" % uuid.uuid4(),
    "Handling request %s" % uuid.uuid4(),
    "Handling request %s" % uuid.uuid4(),
    "Successfully started helper",
    "Reading from device: /dev/hda",
    "Received packet from %s" % random.choice(TEST_ADDRESSES),
    "Received packet from %s" % random.choice(TEST_ADDRESSES),
    "Received packet from %s" % random.choice(TEST_ADDRESSES),
]


def syslog_msgs():
    global SYSLOG_LOG_CURR_TIME
    while True:
        SYSLOG_LOG_CURR_TIME += datetime.timedelta(seconds=random.randrange(1, 5))
        yield '%s frontend3 %s: %s\n' % (
            SYSLOG_LOG_CURR_TIME.strftime(SYSLOG_DATE_FMT),
            random.choice(TEST_PROCS),
            random.choice(TEST_MSGS),
        )


try:
    shutil.rmtree("/tmp/demo")
    os.makedirs("/tmp/demo")
except OSError:
    pass

FILES = [
    ("/tmp/demo/access_log", access_log_msgs()),
    ("/tmp/demo/messages", syslog_msgs()),
]

COUNTER = 0
while COUNTER < 5000:
    loop_inc = datetime.timedelta(seconds=random.weibullvariate(1, 1.5) * 500)
    ACCESS_LOG_CURR_TIME += loop_inc
    SYSLOG_LOG_CURR_TIME += loop_inc
    for fname, gen in FILES:
        for i in range(random.randrange(4, 8)):
            COUNTER += 1
            with open(fname, "a+") as fp:
                if random.uniform(0.0, 1.0) < 0.01:
                    line = next(gen)
                    prefix = line[:50]
                    suffix = line[50:]
                    fp.write(prefix)
                    # time.sleep(random.uniform(0.5, 0.6))
                    fp.write(suffix)
                else:
                    fp.write(next(gen))
                # if random.uniform(0.0, 1.0) < 0.010:
                #    fp.truncate(0)
            time.sleep(random.uniform(0.01, 0.02))
            # if random.uniform(0.0, 1.0) < 0.001:
            #    os.remove(fname)
