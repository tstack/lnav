.. _log_formats:

***********
Log Formats
***********

Built-in Formats
================

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

XSV Formats
-----------

In addition to the above formats, the following self-describing formats are
supported:

* The
  `Bro Network Security Monitor <https://www.bro.org/sphinx/script-reference/log-files.html>`_
  TSV log format is supported in lnav versions v0.8.3+.  The Bro log format is
  self-describing, so **lnav** will read the header to determine the shape of
  the file.
* The
  `W3C Extended Log File Format <https://www.w3.org/TR/WD-logfile.html>`_
  is supported in lnav versions v0.10.0+.  The W3C log format is
  self-describing, so **lnav** will read the header to determine the shape of
  the file.

JSON-lines
----------

Logs encoded as `JSON-lines <https://jsonlines.org>`_ can be parsed and
pretty-printed in lnav by creating a log format file.  The format file
is a bit simpler to create since it doesn't require a regular expression
to match plain text.  Instead, the format defines the relevant fields
and provides a :code:`line-format` array that specifies how the fields
in the JSON object should be displayed.

See the following formats that are built into lnav as examples:

* `cloudflare_log.json <https://github.com/tstack/lnav/blob/master/src/formats/cloudflare_log.json>`_
* `github_events_log.json <https://github.com/tstack/lnav/blob/master/src/formats/github_events_log.json>`_

logfmt
------

There is also basic support for the `logfmt <https://brandur.org/logfmt>`_
convention for formatting log messages.  Files that use this format must
have the entire line be key/value pairs and the timestamp contained in a
field named :code:`time` or :code:`ts`.  If the file you're using does not
quite follow this formatting, but wraps logfmt data with another recognized
format, you can use the :ref:`logfmt2json` SQL function to convert the data
into JSON for further analysis.

Defining a New Format
=====================

New log formats can be defined by placing JSON configuration files in
subdirectories of the :file:`/etc/lnav/formats` and :file:`~/.lnav/formats/`
directories. The directories and files can be named anything you like, but the
files must have the '.json' suffix.  A sample file containing the builtin
configuration will be written to this directory when **lnav** starts up.
You can consult that file when writing your own formats or if you need to
modify existing ones.  Format directories can also contain '.sql' and '.lnav'
script files that can be used automate log file analysis.

Creating a Format Using Regex101.com (v0.11.0+)
-----------------------------------------------

For plain-text log files, the easiest way to create a log format definition is
to create the regular expression that recognizes log messages using
https://regex101.com .  Simply copy a log line into the test string input box
on the site and then start editing a PCRE2 regular expression.  When building the
regular expression, you'll want to use named captures for the structured parts
of the log message.  Any raw message text should be matched by a captured named
"body".  Once you have a regex that matches the whole log message, you can use
**lnav**'s "management CLI" to create a skeleton format file.  The skeleton
will be populated with the regular expression from the site and the test
string, along with any unit tests, will be added to the "samples" list.  The
"regex101 import" management command is used to create the skeleton and has
the following form:

.. prompt:: bash

   lnav -m regex101 import <regex101-url> <format-name> [<regex-name>]

If the import was successful, the path to the new format file should be
printed out.  The skeleton will most likely need some changes to make it
fully functional.  For example, the :code:`kind` properties for captured values
default to :code:`string`, but you'll want to change them to the appropriate
type.

Format File Reference
---------------------

An **lnav** format file must contain a single JSON object, preferably with a
:code:`$schema` property that refers to the
`format-v1.schema <https://lnav.org/schemas/format-v1.schema.json>`_,
like so:

.. code-block:: json

   {
       "$schema": "https://lnav.org/schemas/format-v1.schema.json"
   }

Each format to be defined in the file should be a separate field in the top-level
object.  The field name should be the symbolic name of the format and consist
only of alphanumeric characters and underscores.  This value will also be used
as the SQL table name for the log.  The value for each field should be another
object with the following fields:

:title: The short and human-readable name for the format.
:description: A longer description of the format.
:url: A URL to the definition of the format.

:file-pattern: A regular expression used to match log file paths.  Typically,
  every file format will be tried during the detection process.  This field
  can be used to limit which files a format is applied to in case there is
  a potential for conflicts.

.. _format_regex:

:regex: This object contains sub-objects that describe the message formats
  to match in a plain-text log file.  Each :code:`regex` MUST only match one
  type of log message.  It must not match log messages that are matched by
  other regexes in this format.  This uniqueness requirement is necessary
  because **lnav** will "lock-on" to a regex and use it to match against
  the next line in a file. So, if the regexes do not uniquely match each
  type of log message, messages can be matched by the wrong regex.  The
  "lock-on" behavior is needed to avoid the performance hit of having to
  try too many different regexes.

  .. note:: If the format allows for multiline log entries, the regex must 
     match also only the first line for automatic format detection to work 
     correctly, i.e. everything after the first line must be optional.

  .. note:: Log files that contain JSON messages should not specify this field.

  :pattern: The regular expression that should be used to match log messages.
    The `PCRE2 <http://www.pcre.org>`_ library is used by **lnav** to do all
    regular expression matching.

  :module-format: If true, this regex will only be used to parse message
    bodies for formats that can act as containers, such as syslog.  Default:
    false.

:json: True if each log line is JSON-encoded.

:converter: An object that describes how an input file can be detected and
  then converted to a form that can be interpreted by **lnav**.  For
  example, a PCAP file is in a binary format that cannot be handled natively
  by **lnav**.  However, a PCAP file can be converted by :file:`tshark`
  into JSON-lines that can be handled by **lnav**.  So, this configuration
  describes how the input file format can be detected and converted.  See
  `Automatic File Conversion`_ for more information.

  :header: An object that describes how to match the header of the input
    file.

    :expr: An object that contains SQLite expressions that can be used to
      check if the input file's header is of this type.  The property
      name is the name of the expression and the value is the expression.
      The expression is evaluated with the following variables:

        :\:header: The hex-encoded version of the header content.

        :\:filepath: The path to the input file.

    :size: The minimum size of header that is needed to do the match.

  :command: The command to execute to convert the input file.

:line-format: An array that specifies the text format for JSON-encoded
  log messages.  Log files that are JSON-encoded will have each message
  converted from the raw JSON encoding into this format.  Each element
  is either an object that defines which fields should be inserted into
  the final message string and or a string constant that should be
  inserted.  For example, the following configuration will transform each
  log message object into a string that contains the timestamp, followed
  by a space, and then the message body:

  .. code-block:: json

      [ { "field": "ts" }, " ", { "field": "msg" } ]

  .. note:: Line-feeds at the end of a value are automatically stripped.

  :field: The name or `JSON-Pointer <https://tools.ietf.org/html/rfc6901>`_
    of the message field that should be inserted at this point in the
    message.  The special :code:`__timestamp__` field name can be used to
    insert a human-readable timestamp.  The :code:`__level__` field can be
    used to insert the level name as defined by lnav.

    .. tip::

      Use a JSON-Pointer to reference nested fields.  For example, to include
      a "procname" property that is nested in a "details" object, you would
      write the field reference as :code:`/details/procname`.

  :min-width: The minimum width for the field.  If the value for the field
    in a given log message is shorter, padding will be added as needed to
    meet the minimum-width requirement. (v0.8.2+)
  :max-width: The maximum width for the field.  If the value for the field
    in a given log message is longer, the overflow algorithm will be applied
    to try and shorten the field. (v0.8.2+)
  :auto-width: Flag that indicates that the width of the field should
    automatically be set to the widest value seen. (v0.11.2)
  :align: Specifies the alignment for the field, either "left" or "right".
    If "left", padding to meet the minimum-width will be added on the right.
    If "right", padding will be added on the left. (v0.8.2+)
  :overflow: The algorithm used to shorten a field that is longer than
    "max-width".  The following algorithms are supported:

      :abbrev: Removes all but the first letter in dotted text.  For example,
        "com.example.foo" would be shortened to "c.e.foo".
      :truncate: Truncates any text past the maximum width.
      :dot-dot: Cuts out the middle of the text and replaces it with two
        dots (i.e. '..').
      :last-word: Removes all but the last word in text with dot, dash,
        forward-slash, or colon separators. For example, "com.example.foo"
        would be shortened to "foo".

    (v0.8.2+)
  :timestamp-format: The timestamp format to use when displaying the time
    for this log message. (v0.8.2+)
  :default-value: The default value to use if the field could not be found
    in the current log message.  The built-in default is "-".
  :text-transform: Transform the text in the field.  Supported options are:
    none, uppercase, lowercase, capitalize
  :prefix: Text to prepend to the value.  If the value is empty, this prefix
    will not be added.
  :suffix: Text to append to the value.  If the value is empty, this suffix
    will not be added.

:timestamp-field: The name of the field that contains the log message
  timestamp.
  Internally, timestamps are stored with microsecond precision.
  Defaults to "timestamp".

:timestamp-format: An array of timestamp formats using a subset of the
  strftime conversion specification.  The following conversions are
  supported: %a, %b, %L, %M, %H, %I, %d, %e, %j, %k, %l, %m, %p, %y, %Y, %S, %s,
  %Z, %z.  In addition, you can also use the following:

  :%L: Milliseconds as a decimal number (range 000 to 999).
  :%f: Microseconds as a decimal number (range 000000 to 999999).
  :%N: Nanoseconds as a decimal number (range 000000000 to 999999999).
  :%q: Seconds from the epoch as a hexidecimal number.
  :%i: Milliseconds from the epoch.
  :%6: Microseconds from the epoch.
  :%9: Nanoseconds from the epoch.

:timestamp-divisor: For JSON logs with numeric timestamps, this value is used
  to divide the timestamp by to get the number of seconds and fractional
  seconds.

:subsecond-field: (v0.11.1+) The path to the property in a JSON-lines log
  message that contains the sub-second time value

:subsecond-units: (v0.11.1+) The units of the subsecond-field property value.
  The following values are supported:

  :milli: for milliseconds
  :micro: for microseconds
  :nano: for nanoseconds

:ordered-by-time: (v0.8.3+) Indicates that the order of messages in the file
  is time-based.  Files that are not naturally ordered by time will be sorted
  in order to display them in the correct order.  Note that this sorting can
  incur a performance penalty when tailing logs.

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

:hide-extra: A boolean for JSON logs that indicates whether fields not
  present in the line-format should be displayed on their own lines.

:level: A mapping of error levels to regular expressions.  During scanning
  the contents of the capture group specified by *level-field* will be
  checked against each of these regexes.  Once a match is found, the log
  message level will set to the corresponding level.  The available levels,
  in order of severity, are: **fatal**, **critical**, **error**,
  **warning**, **stats**, **info**, **debug**, **debug2-5**, **trace**.
  For JSON logs with exact numeric levels, the number for the corresponding
  level can be supplied.  If the JSON log format uses numeric ranges instead
  of exact numbers, you can supply a pattern and the number found in the log
  will be converted to a string for pattern-matching.

  .. note:: The regular expression is not anchored to the start of the
     string by default, so an expression like :code:`1` will match
     :code:`-1`.  If you want to exactly match :code:`1`, you would
     use :code:`^1$` as the expression.

:multiline: If false, **lnav** will consider any log lines that do not
  match one of the message patterns to be in error when checking files with
  the '-C' option.  This flag will not affect normal viewing operation.
  Default: true.

:value: This object contains the definitions for the values captured by the
  regexes.

  :kind: The type of data that was captured **string**, **integer**,
    **float**, **json**, **quoted**.
  :collate: The name of the SQLite collation function for this value.
    The standard SQLite collation functions can be used as well as the
    ones defined by lnav, as described in :ref:`collators`.
  :identifier: A boolean that indicates whether or not this field represents
    an identifier and should be syntax colored.
  :foreign-key: A boolean that indicates that this field is a key and should
    not be graphed.  This should only need to be set for integer fields.
  :hidden: A boolean for log fields that indicates whether they should
    be displayed.  The behavior is slightly different for JSON logs and text
    logs.  For a JSON log, this property determines whether an extra line
    will be added with the key/value pair.  For text logs, this property
    controls whether the value should be displayed by default or replaced
    with an ellipsis.
  :rewriter: A command to rewrite this field when pretty-printing log
    messages containing this value.  The command must start with ':', ';',
    or '|' to signify whether it is a regular command, SQL query, or a script
    to be executed.  The other fields in the line are accessible in SQL by
    using the ':' prefix.  The text value of this field will then be replaced
    with the result of the command when pretty-printing.  For example, the
    HTTP access log format will rewrite the status code field to include the
    textual version (e.g. 200 (OK)) using the following SQL query:

    .. code-block:: sql

        ;SELECT :sc_status || ' (' || (
            SELECT message FROM http_status_codes
                WHERE status = :sc_status) || ') '

:tags: This object contains the tags that should automatically be added to
  log messages.

  :pattern: The regular expression evaluated over a line in the log file as
    it is read in.  If there is a match, the log message the line is a part
    of will have this tag added to it.
  :paths: This array contains objects that define restrictions on the file
    paths that the tags will be applied to.  The objects in this array can
    contain:

    :glob: A glob pattern to check against the log files read by lnav.

:partitions: This object contains a description of partitions that should
  automatically be created in the log view.

  :pattern: The regular expression evaluated over a line in the log file as
    it is read in.  If there is a match, the log message the line is a part
    of will be used as the start of the partition.  The name of the
    partition will be taken from any captures in the regex.
  :paths: This array contains objects that define restrictions on the file
    paths in which partitions will be created.  The objects in this array
    can contain:

    :glob: A glob pattern to check against the log files read by lnav.

.. _format_sample:

:sample: A list of objects that contain sample log messages.  All formats
  must include at least one sample and it must be matched by one of the
  included regexes.  Each object must contain the following field:

  :line: The sample message.
  :level: The expected error level.  An error will be raised if this level
    does not match the level parsed by lnav for this sample message.

:highlights: This object contains the definitions for patterns to be
  highlighted in a log message.  Each entry should have a name and a
  definition with the following fields:

  :pattern: The regular expression to match in the log message body.
  :color: The foreground color to use when highlighting the part of the
    message that matched the pattern.  If no color is specified, one will be
    picked automatically.  Colors can be specified using hexadecimal notation
    by starting with a hash (e.g. #aabbcc) or using a color name as found
    at http://jonasjacek.github.io/colors/.
  :background-color: The background color to use when highlighting the part
    of the message that matched the pattern.  If no background color is
    specified, black will be used.  The background color is only considered
    if a foreground color is specified.
  :underline: If true, underline the part of the message that matched the
    pattern.
  :blink: If true, blink the part of the message that matched the pattern.

Example format:

.. code-block:: json

    {
        "$schema": "https://lnav.org/schemas/format-v1.schema.json",
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

.. _patch_format:

Patching an Existing Format
---------------------------

When loading log formats from files, **lnav** will overlay any new data over
previously loaded data.  This feature allows you to override existing value or
append new ones to the format configurations.  For example, you can separately
add a new regex to the example log format given above by creating another file
with the following contents:

.. code-block:: json

    {
        "$schema": "https://lnav.org/schemas/format-v1.schema.json",
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


This example overrides the default `syslog_log <https://github.com/tstack/lnav/blob/master/src/formats/syslog_log.json>`_
error detection regex to **not** match the :code:`errors=` string.

.. code-block:: json

  {
    "syslog_log": {
        "level": {
            "error": "(?:(?:(?<![a-zA-Z]))(?:(?i)error(?:s)?(?!=))(?:(?![a-zA-Z]))|failed|failure)"
        }
    }
  }

.. _installing_format_files:

Installing Format Files
-----------------------

File formats are loaded from subdirectories in :file:`/etc/lnav/formats` and
:file:`~/.lnav/formats/`.  You can manually create these subdirectories and
copy the format files into there.  Or, you can pass the :option:`-i` option to
**lnav** to automatically install formats from the command-line.  For example:

.. code-block:: bash

    $ lnav -i myformat.json
    info: installed: /home/example/.lnav/formats/installed/myformat_log.json

Format files installed using this method will be placed in the :file:`installed`
subdirectory and named based on the first format name found in the file.

The :option:`-i` option can also be used to install :file:`.sql` and
:file:`.lnav` script files.  The SQL files are executed on startup to
create any helper tables or views and the '.lnav' script files can be executed
using the pipe hotkey :kbd:`|`.

You can also install formats from git repositories by passing the repository's
clone URL.  A standard set of repositories is maintained at
(https://github.com/tstack/lnav-config) and can be installed by passing 'extra'
on the command line, like so:

.. prompt:: bash

    lnav -i extra

These repositories can be updated by running **lnav** with the '-u' flag.

Format files can also be made executable by adding a shebang (#!) line to the
top of the file, like so::

    #! /usr/bin/env lnav -i
    {
        "myformat_log" : ...
    }

Executing the format file should then install it automatically:

.. code-block:: bash

    $ chmod ugo+rx myformat.json
    $ ./myformat.json
    info: installed: /home/example/.lnav/formats/installed/myformat_log.json

.. _format_order:

Format Order When Scanning a File
=================================

When **lnav** loads a file, it tries each log format against the first 15,000
lines [#]_ of the file trying to find a match.  When a match is found, that log
format will be locked in and used for the rest of the lines in that file.
Since there may be overlap between formats, **lnav** performs a test on
startup to determine which formats match each others sample lines.  Using
this information it will create an ordering of the formats so that the more
specific formats are tried before the more generic ones.  For example, a
format that matches certain syslog messages will match its own sample lines,
but not the ones in the syslog samples.  On the other hand, the syslog format
will match its own samples and those in the more specific format.  You can
see the order of the format by enabling debugging and checking the **lnav**
log file for the "Format order" message:

.. prompt:: bash

    lnav -d /tmp/lnav.log

For JSON-lines log files, the log message must have the timestamp property
specified in the format in order to match.  If multiple formats match a
message, the format that has the most matching :code:`line-format` elements
will win.

.. [#] The maximum number of lines to check can be configured.  See the
       :ref:`tuning` section for more details.

Automatic File Conversion
=========================

File formats that are not naturally understood by **lnav** can be
automatically detected and converted to a usable form using the
:code:`converter` property.  For example, PCAP files can be
detected and converted to a JSON-lines form using :code:`tshark`.
The conversion process works as follows:

#. The first 1024 bytes of the file are read, if available.
#. This header is converted into a hex string.
#. For each log format that has defined a :code:`converter`,
   every "header expression" is evaluated to see if there is a
   match.  The header expressions are SQLite expressions where
   the following variables are defined:

   :\:header: A string containing the header as a hex string.

   :\:filepath: The path to the file.

#. If a match is found, the converter script defined in the
   log format will be invoked and passed the format name and
   path to the file as arguments.  The script should write
   the converted form of the input file on its standard output.
   Any errors should be written to the standard error.
#. The log format will be associated with the original file will
   be used to interpret the converted file.
