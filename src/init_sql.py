#! /usr/bin/env python

import httplib

TABLES = """
-- This file was created by init_sql.py --

CREATE TABLE IF NOT EXISTS http_status_code (
    status integer PRIMARY KEY,
    message text
);
"""

print TABLES

for item in httplib.responses.iteritems():
    print "INSERT INTO http_status_code VALUES (%d, \"%s\");" % item
