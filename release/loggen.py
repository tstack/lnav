#! /usr/bin/env python

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

TEST_ADDRESSES = [
    "192.0.2.55",
    #"192.0.2.123",
    "192.0.2.33",
    #"192.0.2.2",
]

TEST_USERNAMES = [
    "bob@example.com",
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
]

def access_log_msgs():
    while True:
        yield '%s - %s [%s +0000] "%s %s %s" %s %s "%s" "%s"\n' % (
            random.choice(TEST_ADDRESSES),
            random.choice(TEST_USERNAMES),
            datetime.datetime.now().strftime(ACCESS_LOG_DATE_FMT),
            random.choice(TEST_METHODS),
            random.choice(TEST_URLS),
            random.choice(TEST_VERSIONS),
            random.choice(TEST_STATUS),
            random.randint(16, 1024 * 1024),
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
    while True:
        yield '%s frontend3 %s: %s\n' % (
            datetime.datetime.now().strftime(SYSLOG_DATE_FMT),
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

while True:
    for fname, gen in FILES:
        for i in range(random.randrange(0, 4)):
            with open(fname, "a+") as fp:
                fp.write(gen.next())
            time.sleep(random.uniform(0.00, 0.001))
            if random.uniform(0.0, 1.0) < 0.001:
                os.remove(fname)
