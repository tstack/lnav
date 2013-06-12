#! /usr/bin/env python

import httplib

TABLES = """
-- This file was created by init_sql.py --

CREATE TABLE IF NOT EXISTS http_status_codes (
    status integer PRIMARY KEY,
    message text,

    FOREIGN KEY(status) REFERENCES access_log(sc_status)
);
"""

print TABLES

for item in httplib.responses.iteritems():
    print "INSERT INTO http_status_codes VALUES (%d, \"%s\");" % item
