.. _sql-tab:

SQLite Tables Reference
=======================

In addition to the tables generated for each log format, **lnav** includes
the following tables/views:

* `environ`_
* `fstat(<path|pattern>)`_
* `lnav_events`_
* `lnav_file`_
* `lnav_file_metadata`_
* `lnav_file_metrics`_
* `lnav_file_value_stats`_
* `lnav_format_value_stats`_
* `lnav_format_metrics`_
* `lnav_format_values`_
* `lnav_help`_
* `lnav_log_breakpoints`_
* `lnav_user_notifications`_
* `lnav_views`_
* `lnav_views_echo`_
* `lnav_view_files`_
* `lnav_view_stack`_
* `lnav_view_filters`_
* `lnav_view_filter_stats`_
* `lnav_view_filters_and_stats`_
* `lnav_view_searches`_
* `lnav_top_view`_
* `all_logs`_
* `all_metrics`_
* `all_opids`_
* `all_thread_ids`_
* `all_thread_opids`_
* `lnav_focused_msg`_
* `http_status_codes`_
* `regexp_capture(<string>, <regex>)`_

These extra tables provide useful information and can let you manipulate
**lnav**'s internal state.  You can get a dump of the entire database schema
by executing the '.schema' SQL command, like so::

    ;.schema

.. note::
    The tables created by lnav are in the :code:`lnav_db` database.  The
    :code:`main` SQLite database is left empty for your own use.  If you
    end up creating any tables while processing your logs, you can use
    the :ref:`dot_save` SQL command to save the :code:`main` database to
    a SQLite database file.

environ
-------

The :code:`environ` table gives you access to the **lnav** process' environment
variables.  You can :code:`SELECT`, :code:`INSERT`, and :code:`UPDATE`
environment variables, like so:

.. code-block:: custsqlite

    ;SELECT * FROM environ WHERE name = 'SHELL'
     name   value
    SHELL /bin/tcsh

    ;UPDATE environ SET value = '/bin/sh' WHERE name = 'SHELL'

Environment variables can be used to store simple values or pass values
from **lnav**'s SQL environment to **lnav**'s commands.  For example, the
:code:`:open` command will do variable substitution, so you can insert a variable
named "FILENAME" and then open it in **lnav** by referencing it with
"$FILENAME":

.. code-block:: custsqlite

    ;INSERT INTO environ VALUES ('FILENAME', '/path/to/file')
    :open $FILENAME


fstat(<path|pattern>)
---------------------

The :code:`fstat` table-valued function provides access to the local
file system.  The function takes a file path or a glob pattern and
returns the results of :code:`lstat(2)` for the matching files.  If
the parameter is a pattern that matches nothing, no rows will be
returned.  If the parameter is a path for a non-existent file, a
row will be returned with the :code:`error` column set and the
stat columns as :code:`NULL`.  To read the contents of a file, you
can :code:`SELECT` the hidden :code:`data` column.


.. _table_lnav_events:

lnav_events
-----------

The :code:`lnav_events` table allows you to react to events that occur while
**lnav** is running using SQLite triggers.  For example, when a file is
opened, a row is inserted into the :code:`lnav_events` table that contains
a timestamp and a JSON object with the event ID and the path of the file.
The following columns are available in this table:

  :ts: The timestamp of the event.
  :content: A JSON object that contains the event information.  See the
            :ref:`event_reference` for more information about the types
            of events that are available.

lnav_file
---------

The :code:`lnav_file` table allows you to examine and perform limited updates to
the metadata for the files that are currently loaded into **lnav**.  The
following columns are available in this table:

  :device: The device the file is stored on.
  :inode: The inode for the file on the device.
  :filepath: If this is a real file, it will be the absolute path.  Otherwise,
    it is a symbolic name.  If it is a symbolic name, it can be UPDATEd
    so that this file will be considered when saving and loading session
    information.
  :mimetype: The detected MIME type of the file.
  :content_id: The hash of some unique content in the file.
  :format: The log file format for the file.
  :lines: The number of lines in the file.
  :time_offset: The millisecond offset for timestamps.  This column can be
    UPDATEd to change the offset of timestamps in the file.
  :options_path: Options can be applied to files based on a path or glob
    pattern.  If this file matches a set of options, the matching path/pattern
    is available in this column and the actual options themselves are in the
    :code:`options` column.
  :options: The options that are applicable to this file.  Currently, the
    only options available are for the timezone set by the
    :ref:`:set-file-timezone<set_file_timezone>` command.
  :archive_path: For a file that was extracted from an archive, the path of
    the archive.  Otherwise, NULL.  The files from one archive have the same
    value here, so it can be used to group them, for example when several
    support bundles are open at once.
  :archive_member: For a file that was extracted from an archive, the path of
    the file within the archive, like :code:`log/monitor.json`.  Otherwise,
    NULL.  The :code:`filepath` of such a file is the two joined, but these
    columns keep their values if :code:`filepath` is UPDATEd.
  :details: A JSON object with what the details panel of the Files panel
    shows for the file, other than what has its own column here.  Times from
    the log messages are formatted the same way as :code:`log_time`.  The
    object has the following properties, the optional ones only when they
    apply:

    * :code:`actual-path` -- The path of the file that is read, which differs
      from :code:`filepath` for a file extracted from an archive or converted,
      or :code:`null` when the content is piped in.
    * :code:`glob-pattern` -- The pattern that matched the file, if it was
      opened by one.
    * :code:`child-command` -- For the output of a command, an object with the
      :code:`command`, whether it is still :code:`running`, and its
      :code:`exit-status`.
    * :code:`last-modified` -- The file's modification time.
    * :code:`size` -- The number of bytes that have been indexed.
    * :code:`compressed-size` -- For a compressed file, its size on disk.
    * :code:`time-range` -- For a log file, the :code:`begin` and :code:`end`
      times of its messages.
    * :code:`cutoff` -- The :code:`from` and :code:`to` times the file was
      opened with, if any.
    * :code:`base-time` -- For a log file, the time, in UTC, that dates
      without a year or day are relative to, and its :code:`source`:
      :code:`line` (with the :code:`line` it came from), :code:`mtime`, or
      :code:`embedded`.
    * :code:`provenance` -- How the file came to be opened, like
      :code:`user`, :code:`archive`, or :code:`remote`.
    * :code:`include-in-session` and :code:`detect-format` -- The flags the
      file was opened with.
    * :code:`initial-location` -- Where the file's view starts.
    * :code:`format-match` -- The messages from matching the file against the
      log formats, each with a :code:`level`, :code:`message`, and, when they
      have them, :code:`reason` and :code:`help`.
    * :code:`error` -- The error from decompressing the file, if there was
      one.
    * :code:`notes` -- Problems with the file, each with a :code:`kind`
      (:code:`indexing-disabled`, :code:`duplicate`, :code:`not-utf`, or
      :code:`line-limit`), a :code:`message`, and a :code:`reason`.  For
      example, to find the files that were skipped or hidden:

      .. code-block:: custsqlite

          ;SELECT filepath, value->>'$.message' AS note
             FROM lnav_file, json_each(details->'$.notes')

    * :code:`invalid-lines` -- For a log file with lines the format didn't
      handle, the :code:`count` of them and the :code:`first` few line
      numbers.

lnav_file_metadata
------------------

The :code:`lnav_file_metadata` table gives access to metadata associated with a
loaded file.  Currently,

:filepath: The path to the file.
:descriptor: A descriptor that identifies the source of the metadata.  The
  following descriptors are supported:

  :net.zlib.gzip.header: The header on a gzipped file.  The content is a
     JSON object with the following properties:

        :name: The original name of the file.
        :mtime: The last modified time of the file when it was compressed.
        :comment: A text comment associated with the file.
  :net.daringfireball.markdown.frontmatter: The frontmatter on a
      markdown file.  If the frontmatter is delimited by three dashes
      (:code:`---`), the :code:`mimetype` will be :code:`application/yaml`.
      If the frontmatter is delimited by three pluses (:code:`+++`) the
      :code:`mimetype` will be :code:`application/toml`.
:mimetype: The MIME type of the metadata.
:content: The metadata itself.


lnav_file_metrics
-----------------

The :code:`lnav_file_metrics` table has a row for each metric in each open
:ref:`metrics file<metrics_log>`: a CSV whose columns are metrics, or a log
whose format names the metric of each sample in the data.  It is made from
what **lnav** kept while indexing the files, so it is quick to query, unlike a
:code:`GROUP BY` over the `all_metrics`_ table, which reads every sample.  For
a counter, the statistics are of the changes between samples, which is what
:code:`all_metrics` shows for it.  The following columns are available in this
table:

:filepath: The path to the file.
:format: The name of the file's log format.
:source: The file's stem, as in :code:`all_metrics`.
:metric: The name of the metric, as in :code:`all_metrics`.
:kind: :code:`counter` if the metric's samples are running totals,
  otherwise :code:`gauge`.
:count: The number of values: the samples of a gauge, or the changes of a
  counter.
:min: The smallest value.
:max: The largest value.
:mean: The mean of the values.
:p50: The estimated median, for a metric that is a column of a CSV, else NULL.
:p90: The estimated 90th percentile, for a metric that is a column, else NULL.
:p99: The estimated 99th percentile, for a metric that is a column, else NULL.
:earliest: The time of the metric's first sample.
:latest: The time of the metric's last sample.
:earliest_usecs: (hidden) The :code:`earliest` column, in microseconds from
  the epoch.
:latest_usecs: (hidden) The :code:`latest` column, in microseconds from the
  epoch.
:unit_suffix: (hidden) The suffix the values are shown with, e.g. :code:`s`
  or :code:`B`, or NULL if the format gives none.  For a log whose format
  names the metrics, it is the same as the :code:`unit_suffix` column of
  `all_metrics`_.  The cells of a CSV carry their own units, so it is NULL
  for a metric that is a column of a CSV, while `all_metrics`_ has the
  unit of each cell.
:unit_divisor: (hidden) What the values are divided by to be in the unit of
  the suffix, e.g. :code:`1000` for milliseconds shown as seconds, or
  :code:`1` if they are not divided.  The other numbers in the table are in
  the units of the values, not of the suffix.
:range_min: (hidden) The lowest value the format says the metric can have,
  from the :code:`min` of its display entry or, if that leaves it out, of
  the first matching entry with a :code:`name_pattern`, or NULL.  The
  :code:`min` in `lnav_format_metrics`_ is in the unit of the suffix, while
  this is multiplied by the :code:`unit_divisor` to be in the units of the
  values, so it can be compared with the other numbers.
:range_max: (hidden) The highest value the format says the metric can have,
  found the same way as :code:`range_min`, or NULL.

For example, to list the metrics that were sampled in the last hour:

.. code-block:: custsqlite

   ;SELECT source, metric, kind, min, max FROM lnav_file_metrics
      WHERE latest >= datetime('now', '-1 hour')

lnav_file_value_stats
---------------------

The :code:`lnav_file_value_stats` table contains the statistics that are
collected for the values in each open log file.  They are the same numbers that
are shown in the details overlay for a message.  The numbers are raw values,
on the same scale as the columns in the log tables.  Divide them by the
:code:`unit_divisor` from the `lnav_format_values`_ table to get them in the
base unit for the value.  The following columns are available in this table:

:filepath: The path to the file.
:format: The name of the file's log format.
:name: The name of the value.
:count: The number of numeric values seen.
:text_count: The number of non-numeric values seen.
:min: The smallest numeric value, or NULL if there were none.
:max: The largest numeric value, or NULL if there were none.
:mean: The mean of the numeric values, or NULL if there were none.
:p50: The estimated median of the numeric values.
:p90: The estimated 90th percentile of the numeric values.
:p99: The estimated 99th percentile of the numeric values.
:distinct_estimate: The estimated number of distinct non-numeric values,
  or NULL if there were none.

For example, to get the 99th percentile of each value with a unit in the base
unit:

.. code-block:: custsqlite

   ;SELECT s.filepath, s.name, s.p99 / v.unit_divisor AS p99, v.unit_suffix
      FROM lnav_file_value_stats AS s
      JOIN lnav_format_values AS v USING (format, name)
     WHERE v.unit_suffix IS NOT NULL

lnav_format_value_stats
-----------------------

The :code:`lnav_format_value_stats` table has the same statistics as the
`lnav_file_value_stats`_ table, but combined across the files with that log
format that are visible in the LOG view, so there is one row for each value of
each format.  The percentiles and distinct estimates are combined from the
underlying sketches, so they are not the same as averaging or adding up the
numbers from the per-file table.  Files hidden with
:ref:`:hide-file<hide_file>` are left out, but the statistics are collected
when a file is indexed, so they still include the messages that are hidden by
filters or by :ref:`:hide-lines-before<hide_lines_before>` and
:ref:`:hide-lines-after<hide_lines_after>`.  The columns are the same as the
per-file table, except that :code:`filepath` is replaced by:

:files: The number of files the statistics were combined from.

lnav_format_values
------------------

The :code:`lnav_format_values` table lists the values that are defined by the
loaded log formats, whether or not a file with that format is open.  Each value
is a column of the format's own table, which is named after the format and has
the messages of that format (e.g. the :code:`sc_status` value of
:code:`access_log` is :code:`access_log.sc_status`).  The
:code:`log_format` column of the :code:`all_logs` table says which formats
have messages.  The following columns are available in this table:

:format: The name of the log format.
:name: The name of the value.
:kind: The kind of value, using the same names as the :code:`kind` property
  in a format file (e.g. :code:`string`, :code:`integer`).
:unit_suffix: The suffix used when humanizing the value (e.g. :code:`s` or
  :code:`B`), or NULL if the value has no unit.
:unit_divisor: What the raw value is divided by to get the base unit implied
  by the suffix.  For example, a value in milliseconds with a suffix of
  :code:`s` has a divisor of 1000.
:identifier: 1 when the value names something, like an ID, so grouping by
  it makes sense.
:roles: A JSON array of what the format uses the value for, from its
  :code:`*-field` properties and, for :code:`subid`, the :code:`subid`
  property of :code:`opid`: :code:`timestamp`, :code:`start-timestamp`,
  :code:`subsecond`, :code:`time`, :code:`level`, :code:`body`, :code:`opid`,
  :code:`subid`, :code:`thread-id`, :code:`src-file`, :code:`src-line`,
  :code:`src-location`, :code:`duration`, :code:`metric-name`,
  :code:`metric-value`, or :code:`metric-kind`.  The :code:`metric-*` roles
  come from the :code:`metric` section of a format whose metrics are named in
  the data, and are the fields that its samples in `all_metrics`_ are taken
  from.  A value can have more than one
  role, e.g. :code:`["opid","thread-id"]`, or none, :code:`[]`.  Only the
  values declared in the format's :code:`value` section have rows, so a field
  the format uses without declaring it, such as a :code:`level-field` that is
  not in :code:`value`, does not appear in this table.
:description: What the value is, from the :code:`description` property of
  the value in the format file, or NULL if it has none.

.. _table_lnav_format_metrics:

lnav_format_metrics
-------------------

The :code:`lnav_format_metrics` table lists what the loaded log formats whose
metrics are named in the data say about showing those metrics, whether or not
a file with that format is open.  There is a row for each entry in the
:code:`display` list of the format's :code:`metric` section, in order,
followed by a row for each metric the user showed with
:ref:`:show-fields<show_fields>` or hid with :ref:`:hide-fields<hide_fields>`
that the list does not have.  The samples themselves are in the `all_metrics`_
table.  The following columns are available in this table:

:format: The name of the log format.
:name: The name of the metric, or NULL for an entry with a
  :code:`name_pattern`.  It also matches a metric whose name is this
  followed by labels that start with :code:`[`, :code:`{`, :code:`;`, or
  :code:`,`.
:name_pattern: The regular expression for the names of the metrics the entry
  is for, without their labels, or NULL for an entry with a :code:`name`.
:short_name: What is shown for the metric on the line, or NULL if it is the
  name.
:min: The lowest value the format says the metric can have, e.g. 0 for a
  percentage, or NULL.
:max: The highest value the format says the metric can have, e.g. 100 for a
  percentage, or NULL.
:unit_suffix: The suffix the metric's values are humanized with, e.g.
  :code:`B` for bytes, or NULL if the entry does not give one.  A metric
  that an entry with a :code:`name` leaves it out for gets it from the first
  matching entry with a :code:`name_pattern` that has it, or else from the
  format's value field.  The :code:`unit_suffix` column of
  `all_metrics`_ has the one each sample ends up with.
:unit_divisor: What the metric's values are divided by to be in the unit of
  the suffix, or NULL if the entry does not give one, which comes from the
  other entries the same way.  The :code:`min` and :code:`max` are in that
  unit, while the values in `all_metrics`_ are not divided.
:hidden: 1 if the format leaves the metric off the line, 0 if it shows it,
  or NULL if the format does not list it.
:shown: 1 if the metric is on the line now, after any
  :ref:`:hide-fields<hide_fields>` or :ref:`:show-fields<show_fields>`.

For example, to get each sample of a percentage as a fraction of its range:

.. code-block:: custsqlite

   ;SELECT m.log_time, m.metric, (m.value - f.min) / (f.max - f.min) AS frac
      FROM all_metrics AS m
      JOIN lnav_format_metrics AS f
        ON m.metric = f.name OR m.metric GLOB f.name || '[[{;,]*'
     WHERE f.min IS NOT NULL AND f.max IS NOT NULL

.. _table_lnav_log_breakpoints:

.. _table_lnav_help:

lnav_help
---------

The :code:`lnav_help` table has the reference for the following in
this build of **lnav**:

* the commands
* SQL functions and keywords
* PRQL transforms
* SQLite virtual tables

It holds the same text as the :ref:`:help<help>` command and this
documentation, so a script, or a program driving **lnav** through the
:ref:`external-access API<ExternalAccess>`, can look up what is available by
name or by topic.  The columns in the table are as follows:

:kind: The kind of entry: :code:`command`, :code:`table`,
  :code:`sql_function`, :code:`sql_aggregate_function`,
  :code:`sql_table_function`, :code:`sql_command`, :code:`sql_keyword`,
  :code:`sql_infix`, :code:`prql_transform`, or :code:`prql_function`.
:name: The name to call it by.  Command names start with a colon
  (e.g. :code:`:filter-out`).
:effect: For a command or SQL dot-command (like :code:`.dump`), what
  running it can change, so a program running commands on someone's
  behalf can tell which ones need their approval.  An
  :ref:`external-access<ExternalAccess>` request can limit the effects
  its script may have with an :code:`X-Lnav-Allow-Effects` header.
  :code:`NULL` for other kinds of entries.

  :read: Nothing, though it may print to the script's output.
    :code:`:eval` is :code:`read`, since the commands it runs are
    checked against their own effects.
  :output: Writes to the file it's given or, given :code:`-`, to the
    script's output instead.
  :view: **lnav**'s state for this session: views, filters, marks,
    highlights, the files shown, and tables.  It can be undone in
    **lnav**.
  :config: Configuration or session files, which outlast this
    **lnav**.
  :external: Runs programs, reads or writes files other than
    **lnav**'s own, or opens connections.  Writing to a file
    instead of the script's output also counts as :code:`external`.
  :exit: Quits **lnav**.
  :unknown: Not declared.  Treat it like :code:`external`.
:aliases: A JSON array of other names for the same entry.
:summary: A one-line summary.
:synopsis: How to call it, as plain text.  For a table, this is its
  :code:`CREATE TABLE` statement with a comment for each column, which
  the :code:`sqlite_master` table does not have for **lnav**'s tables.
:description: Further detail, when there is more than the summary.
:parameters: A JSON array of the parameters, in order, each described
  along the lines of a JSON schema: its :code:`name`,
  :code:`description`, whether it is :code:`required`, and its
  :code:`type` (:code:`string`, :code:`integer`, :code:`number`, or
  :code:`boolean` for a flag without a value).  A string can have a
  :code:`format` saying how to write it, such as :code:`regex`,
  :code:`sql`, or :code:`filename`, and :code:`enum` values.  A
  parameter that can be given more than once has the type
  :code:`array`, with the type of each value in :code:`items`, and a
  :code:`minItems` of 1 when it must be given at least once.  When they
  apply, a parameter also has the :code:`flag` keyword written before
  its value, a :code:`default`, and nested :code:`parameters` for the
  parts of a compound parameter.
:examples: A JSON array of examples, each an object with a
  :code:`description`, the :code:`code` to run, and its
  :code:`language` (:code:`default` or :code:`prql`).
:tags: A JSON array of the topics the entry belongs to, such as
  :code:`filtering`, :code:`navigation`, or :code:`datetime`.  Tables
  have them too, so looking up a topic finds the tables for it along
  with the commands and functions.
:opposites: A JSON array of the names of the entries that undo this
  one.
:prql_path: For PRQL functions, the dotted module path
  (e.g. :code:`text.regexp_match`).

For example, to find the commands for filtering and how to undo them:

.. code-block:: custsqlite

    ;SELECT name, synopsis, summary, opposites FROM lnav_help
         WHERE json_contains(tags, 'filtering')

To list the commands that only change what **lnav** shows:

.. code-block:: custsqlite

    ;SELECT name, summary FROM lnav_help WHERE effect = 'view'

.. note::

    Like other JSON values, the JSON columns lose their JSON type when a
    query sorts its rows, so :code:`:write-json-to` writes them as
    strings after an :code:`ORDER BY`.

lnav_log_breakpoints
--------------------

The :code:`lnav_log_breakpoints` table allows you to view and manage
breakpoints set on log messages.  Breakpoints mark log messages that
share a particular source file location or message schema, making it
easy to navigate between related log lines using the :kbd:`F7` and
:kbd:`F8` keys.

You can :code:`SELECT`, :code:`INSERT`, :code:`UPDATE`, and
:code:`DELETE` breakpoints through this table.  The columns in the
table are as follows:

:schema_id: The schema identifier for the breakpoint.  This value
  matches the :code:`log_msg_schema` column in the :code:`all_logs`
  table.
:description: A human-readable description of the breakpoint
  (e.g. :code:`format_name:file.cc:42`).
:type: The source of the schema ID, either :code:`src_location` or
  :code:`message_schema`.
:enabled: Indicates whether the breakpoint is active (1 or 0).

.. code-block:: custsqlite

    ;SELECT * FROM lnav_log_breakpoints

    ;UPDATE lnav_log_breakpoints SET enabled = 0 WHERE description LIKE '%main.cc%'

    ;DELETE FROM lnav_log_breakpoints WHERE description LIKE '%test%'


.. _table_lnav_user_notifications:

lnav_user_notifications
-----------------------

The :code:`lnav_user_notifications` table allows you to display a custom message
in the top-right corner of the UI.  For example, to display "Hello, World!",
you can enter:

.. code-block:: custsqlite

    ;REPLACE INTO lnav_user_notifications (message) VALUES ('Hello, World!')

There are additional columns to have finer control of what is displayed and
when:

  :id: The unique ID for the message, defaults to "org.lnav.user".  This is
    the primary key for the table, so more than one type of message is not
    allowed.
  :priority: The priority of the message.  Higher priority messages will be
    displayed until they are cleared or are expired.
  :created: The time the message was created.
  :expiration: The time when the message should expire or NULL if it should
    not automatically expire.
  :views: A JSON array of view names where the message is applicable or NULL
    if the message should be shown in all views.
  :message: The message itself.

This table will most likely be used in combination with :ref:`Events` and the
`lnav_views_echo`_ table.

lnav_views
----------

The :code:`lnav_views` table allows you to SELECT and UPDATE information related
to **lnav**'s "views" (e.g. log, text, ...).  The following columns are
available in this table:

:name: The name of the view.
:top: The line number at the top of the view.  This value can be UPDATEd to
  move the view to the given line.
:left: The left-most column number to display.  This value can be UPDATEd to
  move the view left or right.
:height: The number of lines that are displayed on the screen.
:inner_height: The number of lines of content being displayed.
:top_time: The timestamp of the top line in the view or NULL if the view is
  not time-based.  This value can be UPDATEd to move the view to the given
  time.
:top_file: The file the top line in the view is from.
:paused: Indicates if the view is paused and will not load new data.
:search: The search string for this view.  This value can be UPDATEd to
  initiate a text search in this view.
:filtering: Indicates if the view is applying filters.
:movement: The movement mode, either 'top' or 'cursor'.
:top_meta: A JSON object that contains metadata related to the top line
  in the view.
:selection: The number of the line that is focused for selection.
:options: A JSON object that contains optional settings for this view.
  Besides the settings for the details overlay, time offsets, hidden fields,
  and word wrap, the following can be read and UPDATEd:

  :filter-context: For a view that supports filtering, an object with the
    :code:`before` and :code:`after` number of lines of context to show
    around the lines that pass the filters, as set by
    :ref:`:filter-context<filter_context>`.
  :row-types: For the TIMELINE view, an object with a property for each
    type of row (:code:`logfile`, :code:`thread`, :code:`opid`, :code:`tag`,
    :code:`partition`, and :code:`search`) that is either :code:`show` or
    :code:`hide`, as set by :ref:`:hide-in-timeline<hide_in_timeline>` and
    :ref:`:show-in-timeline<show_in_timeline>`.  A type that is left out
    is not changed.

  For example, to show two lines of context in the LOG view:

  .. code-block:: custsqlite

     ;UPDATE lnav_views
        SET options = json_set(options, '$.filter-context.before', 2,
                                        '$.filter-context.after', 2)
      WHERE name = 'log'

lnav_views_echo
---------------

The :code:`lnav_views_echo` table is a real SQLite table that you can create
TRIGGERs on in order to react to users moving around in a view.

.. note::

    The table is periodically updated to reflect the current state of the views.
    The changes are *not* performed immediately after the user action.

lnav_view_files
---------------

The :code:`lnav_view_files` table provides access to details about the files
displayed in a particular view.  The main purpose of this table is to allow
you to programmatically control which files are shown / hidden in the view.
The following columns are available in this table:

:view_name: The name of the view.
:filepath: The file's path.
:visible: Determines whether the file is visible in the view.  This column
  can be changed using an :code:`UPDATE` statement to hide or show the file.

lnav_view_stack
---------------

The :code:`lnav_view_stack` table allows you to :code:`SELECT` and :code:`DELETE`
from the stack of **lnav** "views" (e.g. log, text, ...).  The following columns
are available in this table:

  :name: The name of the view.

.. _table_lnav_view_filters:

lnav_view_filters
-----------------

The :code:`lnav_view_filters` table allows you to manipulate the filters in the
**lnav** views.  The following columns are available in this table:

  :view_name: The name of the view the filter is applied to.
  :filter_id: The filter identifier.  This will be assigned on insertion.
  :enabled: Indicates whether this filter is enabled or disabled.
  :type: The type of filter, either 'in' or 'out'.
  :language: What the pattern is: 'regex' for a regular expression, 'sql'
    for a SQL expression (see :ref:`:filter-expr<filter_expr>`), or
    'statement' for a log statement (see
    :ref:`:filter-in-statement<filter_in_statement>`).
  :pattern: The regular expression, SQL expression, or statement to filter
    on.  A statement is its source file and line, with an identifier that
    finds it again after the file changes (:code:`path:line@id`); an
    :code:`INSERT` can leave the identifier out.

This table supports :code:`SELECT`, :code:`INSERT`, :code:`UPDATE`, and
:code:`DELETE` on the table rows to read, create, update, and delete
filters for the views.

lnav_view_filter_stats
----------------------

The :code:`lnav_view_filter_stats` table allows you to get information about how
many lines matched a given filter.  The following columns are available in
this table:

  :view_name: The name of the view.
  :filter_id: The filter identifier.
  :hits: The number of lines that matched this filter.

This table is read-only.

lnav_view_filters_and_stats
---------------------------

The :code:`lnav_view_filters_and_stats` view joins the :code:`lnav_view_filters`
table with the :code:`lnav_view_filter_stats` table into a single view for ease of use.

.. _table_lnav_view_searches:

lnav_view_searches
------------------

The :code:`lnav_view_searches` table allows you to manipulate the
:ref:`named searches<named_searches>` in the **lnav** views.  The following
columns are available in this table:

  :view_name: The name of the view the search is applied to.
  :enabled: Indicates whether this search is enabled or disabled.
  :name: The name of the search.
  :pattern: The regular expression being searched for.
  :hits: The number of lines that matched this search.

This table supports :code:`SELECT`, :code:`INSERT`, and :code:`DELETE` on the
table rows to read, create, and delete named searches for the views.  Only the
:code:`enabled` column can be changed by an :code:`UPDATE`, since changing a
pattern means re-scanning the view for it: do a :code:`DELETE` followed by an
:code:`INSERT` instead.

lnav_top_view
-------------

The :code:`lnav_top_view` view returns the row for the top view on the view stack.

all_logs
--------

.. f0:sql.tables.all_logs

The :code:`all_logs` table lets you query the format derived from the **lnav**
log message parser that is used to automatically extract data, see
:ref:`data-ext` for more details.

.. _table_all_metrics:

all_metrics
-----------

The :code:`all_metrics` table is a long-format view over every metrics file
that **lnav** currently has open: the :ref:`metric CSVs<metrics_log>` and the
logs whose format has a :ref:`metric<format_metric>` section.  Each sample is
one row, whether it is a cell of a CSV or a message of a log, so a single
query can scan metric values across multiple files without having to know
which file each metric came from.

The columns are as follows:

:log_line: The line number in the LOG view of the sample row.  The
  value is shared across all metrics at that timestamp, including
  sibling files whose rows were folded into the visible line.
:log_time: The timestamp for the metric sample.
:log_path: The path of the file the sample came from.
:source: The file stem shown above the sample's column in the
  focused-row overlay.
:metric: The column name from the source file, or the name of the metric
  in the message, with any labels.
:value: The parsed numeric value.  For a counter, this is the running total
  from the file, see :code:`change`.  Integers pass through as INTEGER;
  floats and unit-suffixed cells (e.g. :code:`20.0KB`) are REAL.
:raw_value: (hidden) The original cell text from the file.  Collated
  with :code:`measure_with_units`, so :code:`ORDER BY raw_value`
  sorts by magnitude rather than lexicographically.
:log_mark: (hidden) True when the sample's visible row is
  user-marked.  Marking a metric row fans the mark out to every
  sibling sample at that timestamp.
:kind: (hidden) :code:`counter` if the metric's samples are running
  totals, otherwise :code:`gauge`.
:change: (hidden) For a counter, the change since the previous sample
  of the metric, or NULL for its first sample.  NULL for a gauge.
:log_time_usecs: (hidden) The sample's time as the number of microseconds
  from the epoch, the same as the :code:`log_time_usecs` column of the log
  tables.
:unit_suffix: (hidden) The suffix the value is shown with, e.g. :code:`s`
  or :code:`B`, or NULL if it has none.  It comes from the format, including
  its metric :code:`display` entries, or from the cell text of a CSV file.
:unit_divisor: (hidden) What the value is divided by to be in the unit of
  the suffix, e.g. :code:`1000` for milliseconds shown as seconds, or
  :code:`1` if it isn't divided.

Constraints on :code:`log_time` (:code:`=`, :code:`<`, :code:`<=`,
:code:`>`, :code:`>=`, :code:`BETWEEN`) narrow the scan to that time range,
as long as the time is written the same way as the column, e.g.
:code:`2026-11-17T06:46:00`.  Constraints on :code:`log_time_usecs` narrow
it the same way, with any number.  A :code:`metric = ...` or
:code:`metric IN (...)` constraint skips the lines of other metrics without
parsing them.  To list the metrics, use the `lnav_file_metrics`_ table, which
doesn't have to read every sample.

.. code-block:: custsqlite

    ;SELECT metric, count(*) AS n, min(value) AS lo, max(value) AS hi
         FROM all_metrics
         GROUP BY metric

    ;SELECT log_time, source, metric, value
         FROM all_metrics
         WHERE metric = 'cpu_pct'
         ORDER BY log_time

all_opids
---------

The :code:`all_opids` table contains information about all opids that were
found in the log files or set via the :code:`log_opid` column on the log
vtables.  The information in this table is the same as available through the
:ref:`TIMELINE<timeline>` view.  The :code:`description` column can be
:code:`SET` in an :code:`UPDATE` statement to customize the description
shown in the timeline.

all_thread_ids
--------------

The :code:`all_thread_ids` table contains information about all the thread
identifiers that were found in logs.  Log formats can specify which field
is a thread identifier with the :code:`thread-id-field` property.

all_thread_opids
----------------

The :code:`all_thread_opids` table has a row for each thread and operation ID
pair found in the logs: the operations each thread logged messages for, with the
first and last time it did and how many messages, errors, and warnings it
logged in each.  A thread's messages without an operation ID have a row with a
:code:`NULL` opid.  It's built from the thread and operation IDs recorded when
the logs were indexed, so it's quick to query even for large logs, and the
rows for given threads can be looked up with :code:`thread_id = ?` or
:code:`thread_id IN (...)`.  Like :code:`all_thread_ids`, it doesn't follow
the filters.  For example, to find the operations a thread took part in:

.. code-block:: custsqlite

    ;SELECT opid, earliest, duration, total FROM all_thread_opids
         WHERE thread_id = 'worker-1' ORDER BY earliest

lnav_focused_msg
----------------

The :code:`lnav_focused_msg` view returns the row for the focused log
message from the :code:`all_logs` table.

http_status_codes
-----------------

The :code:`http_status_codes` table is a handy reference that can be used to turn
HTTP status codes into human-readable messages.

regexp_capture(<string>, <regex>)
---------------------------------

The :code:`regexp_capture()` table-valued function applies the regular expression
to the given string and returns detailed results for the captured portions of
the string.
