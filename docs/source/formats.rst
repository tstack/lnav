
.. _log-formats:

Log Formats
===========

Log files loaded into **lnav** are parsed based on formats defined in
configuration files.  Many
formats are already built in to the **lnav** binary and you can define your own
using a JSON file.  When loading files, each format is checked to see if it can
parse the first few lines in the file.  Once a match is found, that format will
be considered that files format and used to parse the remaining lines in the
file.  If no match is found, the file is considered to be plain text and can
be viewed in the "text" view that is accessed with the **t** key.

The following log formats are built into **lnav**:

.. csv-table::
   :header: "Name", "Table Name", "Description"
   :widths: 8 5 20
   :file: format-table.csv

Defining a New Format
---------------------

New log formats can be defined by placing JSON configuration files in
subdirectories of the :file:`~/.lnav/formats/` directory.  The directories and
files can be named anything you like, but the files must have the '.json' suffix.  A
sample file containing the builtin configuration will be written to this
directory when **lnav** starts up.  You can consult that file when writing your
own formats or if you need to modify existing ones.  Any '.sql' files found in
the directories will also be executed on startup, allowing you to create views
or other tables that might make it easier to analyze a log format.

The contents of the format configuration should be a JSON object with a field
for each format defined by the file.  Each field name should be the symbolic
name of the format.  This value will also be used as the SQL table name for
the log.  The value for each field should be another object with the following
fields:

  :title: The short and human-readable name for the format.
  :description: A longer description of the format.
  :url: A URL to the definition of the format.

  :file-pattern: A regular expression used to match log file paths.  Typically,
    every file format will be tried during the detection process.  This field
    can be used to limit which files a format is applied to in case there is
    a potential for conflicts.

  :regex: This object contains sub-objects that describe the message formats
    to match in a plain log file.  Log files that contain JSON messages should
    not specify this field.

    :pattern: The regular expression that should be used to match log messages.
      The `PCRE <http://www.pcre.org>`_ library is used by **lnav** to do all
      regular expression matching.

    :module-format: If true, this regex will only be used to parse message
      bodies for formats that can act as containers, such as syslog.  Default:
      false.

  :json: True if each log line is JSON-encoded.

  :line-format: An array that specifies the text format for JSON-encoded
    log messages.  Log files that are JSON-encoded will have each message
    converted from the raw JSON encoding into this format.  Each element
    is either an object that defines which fields should be inserted into
    the final message string and or a string constant that should be
    inserted.  For example, the following configuration will tranform each
    log message object into a string that contains the timestamp, followed
    by a space, and then the message body::

    [ { "field": "ts" }, " ", { "field": "msg" } ]

    :field: The name of the message field that should be inserted at this
      point in the message.
    :default-value: The default value to use if the field could not be found
      in the current log message.  The built-in default is "-".

  :timestamp-field: The name of the field that contains the log message
    timestamp.  Defaults to "timestamp".

  :timestamp-format: An array of timestamp formats using a subset of the
    strftime conversion specification.  The following conversions are
    supported: %a, %b, %L, %M, %H, %I, %d, %e, %k, %l, %m, %p, %y, %Y, %S, %s,
    %Z, %z.  In addition, you can also use '%i' for milliseconds from the
    epoch.

  :level-field: The name of the regex capture group that contains the log
    message level.  Defaults to "level".

  :body-field: The name of the field that contains the main body of the
    message.  Defaults to "body".

  :opid-field: The name of the field that contains the "operation ID" of the
    message.  An "operation ID" establishes a thread of messages that might
    correspond to a particular operation/request/transaction.  The user can
    press the 'o' or 'Shift+O' hotkeys to move forward/backward through the
    list of messages that have the same operation ID.  Note: For JSON-encoded
    logs, the opid field can be a path (e.g. "foo/bar/opid") if the field is
    nested in an object and it MUST be included in the "line-format" for the
    'o' hotkeys to work.

  :module-field: The name of the field that contains the module identifier
    that distinguishes messages from one log source from another.  This field
    should be used if this message format can act as a container for other
    types of log messages.  For example, an Apache access log can be sent to
    syslog instead of written to a file.  In this case, **lnav** will parse
    the syslog message and then separately parse the body of the message to
    determine the "sub" format.  This module identifier is used to help
    **lnav** quickly identify the format to use when parsing message bodies.

  :level: A mapping of error levels to regular expressions.  During scanning
    the contents of the capture group specified by *level-field* will be
    checked against each of these regexes.  Once a match is found, the log
    message level will set to the corresponding level.  The available levels,
    in order of severity, are: **fatal**, **critical**, **error**,
    **warning**, **stats**, **info**, **debug**, **debug2-5**, **trace**.

  :multiline: If false, **lnav** will consider any log lines that do not
    match one of the message patterns to be in error when checking files with
    the '-C' option.  This flag will not affect normal viewing operation.
    Default: true.

  :value: This object contains the definitions for the values captured by the
    regexes.

    :kind: The type of data that was captured **string**, **integer**,
      **float**, **json**, **quoted**.
    :collate: The collation function for this value.
    :identifier: A boolean that indicates whether or not this field represents
      an identifier and should be syntax colored.
    :foreign-key: A boolean that indicates that this field is a key and should
      not be graphed.  This should only need to be set for integer fields.

  :sample: A list of objects that contain sample log messages.  All formats
    must include at least one sample and it must be matched by one of the
    included regexes.  Each object must contain the following field:

    :line: The sample message.

Example format::

    {
        "example_log" : {
            "title" : "Example Log Format",
            "description" : "Log format used in the documentation example.",
            "url" : "http://example.com/log-format.html",
            "regex" : {
                "basic" : {
                    "pattern" : "^(?<timestamp>\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3}Z)>>(?<level>\\w+)>>(?<component>\\w+)>>(?<body>.*)$"
                }
            },
            "level-field" : "level",
            "level" : {
                "error" : "ERROR",
                "warning" : "WARNING"
            },
            "value" : {
                "component" : {
                    "kind" : "string",
                    "identifier" : true
                }
            },
            "sample" : [
                {
                    "line" : "2011-04-01T15:14:34.203Z>>ERROR>>core>>Shit's on fire yo!"
                }
            ]
        }
    }

Modifying an Existing Format
----------------------------

When loading log formats from files, **lnav** will overlay any new data over
previously loaded data.  This feature allows you to override existing value or
append new ones to the format configurations.  For example, you can separately
add a new regex to the example log format given above by creating another file
with the following contents::

    {
        "example_log" : {
            "regex" : {
                "custom1" : {
                    "pattern" : "^(?<timestamp>\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3}Z)<<(?<level>\\w+)--(?<component>\\w+)>>(?<body>.*)$"
                }
            },
            "sample" : [
                {
                    "line" : "2011-04-01T15:14:34.203Z<<ERROR--core>>Shit's on fire yo!"
                }
            ]
        }
    }

Installing Formats
------------------

File formats are loaded from subdirectories in :file:`/etc/lnav/formats` and
:file:`~/.lnav/formats/`.  You can manually create these subdirectories and
copy the format files into there.  Or, you can pass the '-i' option to **lnav**
to automatically install formats from the command-line.  For example::

    $ lnav -i myformat.json
    info: installed: /home/example/.lnav/formats/installed/myformat_log.json

Format files installed using this method will be placed in the :file:`installed`
subdirectory and named based on the first format name found in the file.

You can also install formats from git repositories by passing the repository's
clone URL.  A standard set of repositories is maintained at
(https://github.com/tstack/lnav-config) and can be installed by passing 'extra'
on the command line, like so:

    $ lnav -i extra

These repositories can be updated by running **lnav** with the '-u' flag.

Format files can also be made executable by adding a shebang (#!) line to the
top of the file, like so::

    #! /usr/bin/env lnav -i
    {
        "myformat_log" : ...
    }

Executing the format file should then install it automatically::

    $ chmod ugo+rx myformat.json
    $ ./myformat.json
    info: installed: /home/example/.lnav/formats/installed/myformat_log.json


Format Order When Scanning a File
---------------------------------

When **lnav** loads a file, it tries each log format against the first ~1000
lines of the file trying to find a match.  When a match is found, that log
format will be locked in and used for the rest of the lines in that file.
Since there may be overlap between formats, **lnav** performs a test on
startup to determine which formats match each others sample lines.  Using
this information it will create an ordering of the formats so that the more
specific formats are tried before the more generic ones.  For example, a
format that matches certain syslog messages will match its own sample lines,
but not the ones in the syslog samples.  On the other hand, the syslog format
will match its own samples and those in the more specific format.  You can
see the order of the format by enabling debugging and checking the **lnav**
log file for the "Format order" message::

    $ lnav -d /tmp/lnav.log
