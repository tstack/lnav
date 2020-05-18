
.. include:: kbd.rst

.. _sql-ext:

SQLite Interface
================

Log analysis in **lnav** can be done using the SQLite interface.  Log messages
can be accessed via `virtual tables <https://www.sqlite.org/vtab.html>`_ that
are created for each file format.  The tables have the same name as the log
format and each message is its own row in the table.  For example, given the
following log message from an Apache access log:

.. code-block::

    127.0.0.1 - frank [10/Oct/2000:13:55:36 -0700] "GET /apache_pb.gif HTTP/1.0" 200 2326

These columns would be available for its row in the :code:`access_log` table:

.. csv-table::
    :class: query-results
    :header-rows: 1

    log_line,log_part,log_time,log_idle_msecs,log_level,log_mark,log_comment,log_tags,log_filters,c_ip,cs_method,cs_referer,cs_uri_query,cs_uri_stem,cs_user_agent,cs_username,cs_version,sc_bytes,sc_status
    0,<NULL>,2000-10-10 13:55:36.000,0,info,1,<NULL>,<NULL>,<NULL>,127.0.0.1,GET,<NULL>,<NULL>,/apache_pb.gif,<NULL>,frank,HTTP/1.0,2326,200

.. note:: Some columns are hidden by default to reduce the amount of noise in
   results, but they can still be accessed when explicitly used.  The hidden
   columns are: :code:`log_path`, :code:`log_text`, and :code:`log_body`.

You can activate the SQL prompt by pressing the |ks| ; |ke| key.  At the
prompt, you can start typing in the desired SQL statement and/or double-tap
|ks| TAB |ke| to activate auto-completion.  A help window will appear above
the prompt to guide you in the usage of SQL keywords and functions.

.. figure:: sql-help.png
   :align: center

   Screenshot of the online help for the SQL prompt.

.. figure:: group_concat-help.png
   :align: center

   Screenshot of the online help for the :code:`group_concat()` function.

A simple query to perform on an Apache access log might be to get the average
and maximum number of bytes returned by the server, grouped by IP address:

.. code-block:: sql

    ;SELECT c_ip, avg(sc_bytes), max(sc_bytes) FROM access_log GROUP BY c_ip

After pressing |ks| Enter |ke|, SQLite will execute the query using **lnav**'s
virtual table implementation to extract the data directly from the log files.
Once the query has finished, the main window will switch to the DB view to
show the results.  Press |ks| q |ke| to return to the log view and press |ks|
v |ke| to return to the log view.  If the SQL results contain a
:code:`log_line` column, you can press to |ks| Shift |ke| + |ks| V |ke| to
switch between the DB view and the log

.. figure:: query-results.png
   :align: center

   Screenshot of the SQL results view.

The DB view has the following display features:

* Column headers stick to the top of the view when scrolling.
* A stacked bar chart of the numeric column values is displayed underneath the
  rows.  Pressing |ks| TAB |ke| will cycle through displaying no columns, each
  individual column, or all columns.
* JSON columns in the top row can be pretty-printed by pressing |ks| p |ke|.
  The display will show the value and JSON-Pointer path that can be passed to
  the `jget`_ function.

Extensions
----------

To make it easier to analyze log data from within **lnav**, there are several
built-in extensions that provide extra functions and collators beyond those
`provided by SQLite <http://www.sqlite.org/lang_corefunc.html>`_.  The majority
of the functions are from the
`extensions-functions.c <http://www.sqlite.org/contrib>`_ file available from
the `sqlite.org <http://sqlite.org>`_ web site.

.. tip:: You can include a SQLite database file on the command-line and use
  **lnav**'s interface to perform queries.  The database will be attached with
  a name based on the database file name.

Taking Notes
------------

A few of the columns in the log tables can be updated on a row-by-row basis to
allow you to take notes.  The majority of the columns in a log table are
read-only since they are backed by the log files themselves.  However, the
following columns can be changed by an :code:`UPDATE` statement:

* **log_part** - The "partition" the log message belongs to.  This column can
  also be changed by the :ref:`:partition-name<partition_name>` command.
* **log_mark** - Indicates whether the line has been bookmarked.
* **log_comment** - A free-form text field for storing commentary.  This
  column can also be changed by the :ref:`:comment<comment>` command.
* **log_tags** - A JSON list of tags associated with the log message.  This
  column can also be changed by the :ref:`:tag<tag>` command.

While these columns can be updated by through other means, using the SQL
interface allows you to make changes automatically and en masse.  For example,
to bookmark all lines that have the text "something interesting" in the log
message body, you can execute:

.. code-block::

   :UPDATE all_logs SET log_mark = 1 WHERE log_body LIKE '%something interesting%'

As a more advanced example of the power afforded by SQL and **lnav**'s virtual
tables, we will tag log messages where the IP address bound by dhclient has
changed.  For example, if dhclient reports "bound to 10.0.0.1" initially and
then reports "bound to 10.0.0.2", we want to tag only the messages where the
IP address was different from the previous message.  While this can be done
with a single SQL statement [#]_, we will break things down into a few steps for
this example.  First, we will use the :ref:`:create-search-table<create_search_table>`
command to match the dhclient message and extract the IP address:

.. code-block::

   :create-search-table dhclient_ip bound to (?<ip>[^ ]+)

The above command will create a new table named :code:`dhclient_ip` with the
standard log columns and an :code:`ip` column that contains the IP address.
Next, we will create a view over the :code:`dhclient_ip` table that returns
the log message line number, the IP address from the current row and the IP
address from the previous row:

.. code-block:: sql

   ;CREATE VIEW IF NOT EXISTS dhclient_ip_changes AS SELECT log_line, ip, lag(ip) OVER (ORDER BY log_line) AS prev_ip FROM dhclient_ip

Finally, the following :code:`UPDATE` statement will concatenate the tag
"#ipchanged" onto the :code:`log_tags` column for any rows in the view where
the current IP is different from the previous IP:

.. code-block:: sql

   ;UPDATE syslog_log SET log_tags = json_concat(log_tags, '#ipchanged') WHERE log_line IN (SELECT log_line FROM dhclient_ip_changes WHERE ip != prev_ip)

Since the above can be a lot to type out interactively, you can put these
commands into a :ref:`script<scripts>` and execute that script with the
|ks| \| |ke| hotkey.

.. [#] The expression :code:`regexp_match('bound to ([^ ]+)', log_body) as ip`
   can be used to extract the IP address from the log message body.

Commands
--------

A SQL command is an internal macro implemented by lnav.

* .schema - Open the schema view.  This view contains a dump of the schema
  for the internal tables and any tables in attached databases.
* .msgformats - Executes a canned query that groups and counts log messages by
  the format of their message bodies.  This command can be useful for quickly
  finding out the types of messages that are most common in a log file.

Variables
---------

The following variables are available in SQL statements:

* $LINES - The number of lines in the terminal window.
* $COLS - The number of columns in the terminal window.

Environment
-----------

Environment variables can be accessed in queries using the usual syntax of
:code:`$VAR_NAME`.  For example, to read the value of the "USER" variable, you
can write:

.. code-block:: sql

    SELECT $USER

.. _collators:

Collators
---------

* **naturalcase** - Compare strings "naturally" so that number values in the
  string are compared based on their numeric value and not their character
  values.  For example, "foo10" would be considered greater than "foo2".
* **naturalnocase** - The same as naturalcase, but case-insensitive.
* **ipaddress** - Compare IPv4/IPv6 addresses.

Reference
---------

The following is a reference of the SQL syntax and functions that are available:

.. include:: ../../src/internals/sql-ref.rst
