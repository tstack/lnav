
.. _sql-ext:

SQLite Interface
================

Log analysis in **lnav** can be done using the SQLite interface.  Log messages
can be accessed via `virtual tables <https://www.sqlite.org/vtab.html>`_ that
are created for each file format.  The tables have the same name as the log
format and each message is its own row in the table.  For example, given the
following log message from an Apache access log::

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

You can activate the SQL prompt by pressing the :kbd:`;` key.  At the
prompt, you can start typing in the desired SQL statement and/or double-tap
:kbd:`TAB` to activate auto-completion.  A help window will appear above
the prompt to guide you in the usage of SQL keywords and functions.

.. figure:: sql-help.png
   :align: center

   Screenshot of the online help for the SQL prompt.

.. figure:: group_concat-help.png
   :align: center

   Screenshot of the online help for the :code:`group_concat()` function.

A simple query to perform on an Apache access log might be to get the average
and maximum number of bytes returned by the server, grouped by IP address:

.. code-block:: custsqlite

    ;SELECT c_ip, avg(sc_bytes), max(sc_bytes) FROM access_log GROUP BY c_ip

After pressing :kbd:`Enter`, SQLite will execute the query using **lnav**'s
virtual table implementation to extract the data directly from the log files.
Once the query has finished, the main window will switch to the DB view to
show the results.  Press :kbd:`q` to return to the log view and press :kbd:`v`
to return to the log view.  If the SQL results contain a
:code:`log_line` column, you can press to :kbd:`Shift` + :kbd:`V` to
switch between the DB view and the log

.. figure:: query-results.png
   :align: center

   Screenshot of the SQL results view.

The DB view has the following display features:

* Column headers stick to the top of the view when scrolling.
* A stacked bar chart of the numeric column values is displayed underneath the
  rows.  Pressing :kbd:`TAB` will cycle through displaying no columns, each
  individual column, or all columns.
* JSON columns in the top row can be pretty-printed by pressing :kbd:`p`.
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

.. code-block:: custsqlite

    ;SELECT $USER

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
