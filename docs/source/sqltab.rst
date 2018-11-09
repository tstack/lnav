
.. _sql-tab:

SQLite Tables Reference
=======================

In addition to the tables generated for each log format, **lnav** includes
the following tables:

* environ
* lnav_views
* all_logs
* http_status_codes

These extra tables provide useful information and can let you manipulate
**lnav**'s internal state.  You can get a dump of the entire database schema
by executing the '.schema' SQL command, like so::

    ;.schema

environ
-------

The **environ** table gives you access to the **lnav** process' environment
variables.  You can SELECT, INSERT, and UPDATE environment variables, like
so::

    ;SELECT * FROM environ WHERE name = 'SHELL'
     name   value
    SHELL /bin/tcsh

    ;UPDATE environ SET value = '/bin/sh' WHERE name = 'SHELL'

Environment variables can be used to store simple values or pass values
from **lnav**'s SQL environment to **lnav**'s commands.  For example, the
"open" command will do variable substitution, so you can insert a variable
named "FILENAME" and then open it in **lnav** by referencing it with
"$FILENAME"::

    ;INSERT INTO environ VALUES ('FILENAME', '/path/to/file')
    :open $FILENAME

lnav_views
----------

The **lnav_views** table allows you to SELECT and UPDATE information related
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
  :search: The search string for this view.  This value can be UPDATEd to
    initiate a text search in this view.

lnav_view_stack
---------------

The **lnav_view_stack** table allows you to SELECT and DELETE from the stack of
**lnav** "views" (e.g. log, text, ...).  The following columns are available in
this table:

  :name: The name of the view.

lnav_view_filters
-----------------

The **lnav_view_filters** table allows you to manipulate the filters in the
**lnav** views.  The following columns are available in this table:

  :view_name: The name of the view.
  :enabled: Indicates whether this filter is enabled or disabled.
  :type: The type of filter, either 'in' or 'out'.
  :pattern: The regular expression to filter on.

all_logs
--------

The **all_logs** table lets you query the format derived from the **lnav**
log message parser that is used to automatically extract data, see
:ref:`data-ext` for more details.

http_status_codes
-----------------

The **http_status_codes** table is a handy reference that can be used to turn
HTTP status codes into human-readable messages.

regexp_capture(<string>, <regex>)
---------------------------------

The **regexp_capture()** table-valued function applies the regular expression
to the given string and returns detailed results for the captured portions of
the string.
