---
layout: post
title: "Support for JSON-encoded logs in v0.6.1"
date:   2013-09-10 00:00:00
excerpt: Turning JSON barf into something readable.
---

Making logs easily digestible by machines is becoming a concern as tools like
elasticsearch become more popular. One of the popular strategies is to encode
the whole log message in JSON and then write that as a single line to a file.
For example:

```json
{"time": "2013-09-04T23:55:09.274041Z", "level" : "INFO", "body" : "Hello, World!" }
{"time": "2013-09-04T23:56:00.285224Z", "level" : "ERROR", "body" : "Something terrible has happened!", "tb": "  foo.c:12\n bar.y:33" }
```

Unfortunately, what is good for a machine is not so great for a human. To try to
improve the situation, the latest release of lnav includes support for parsing
JSON log messages and transforming them on-the-fly. The display format is
specified in a log format configuration and can specify which fields should be
displayed on the main message line. Any unused fields that are found in the
message will be displayed below the main field so you don't miss anything.

The above log lines can be transformed using the following format configuration:

```json
{
    "json_ex_log": {
        "title": "Example JSON Log",
        "description": "An example log format configuration for JSON logs",
        "json": true,
        "file-pattern": "test-log\\.json.*",
        "level-field": "level",
        "line-format": [
            {
                "field": "time"
            },
            " ",
            {
                "field": "body"
            }
        ]
    }
}
```

After copying this config to `~/.lnav/formats/test/format.json`, the log messages
will look like this when viewed in lnav:

```
2013-09-04T23:55:09.274041Z Hello, World!
2013-09-04T23:56:00.285224Z Something terrible has happened!
  tb:   foo.c:12
  tb:   bar.y:33
```
