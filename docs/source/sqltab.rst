.. _sql-tab:

SQLite Tables Reference
=======================

In addition to the tables generated for each log format, **lnav** includes
the following tables/views:

* `environ`_
* `lnav_events`_
* `lnav_file`_
* `lnav_user_notifications`_
* `lnav_views`_
* `lnav_views_echo`_
* `lnav_view_stack`_
* `lnav_view_filters`_
* `lnav_view_filter_stats`_
* `lnav_view_filters_and_stats`_
* `all_logs`_
* `http_status_codes`_
* `regexp_capture(<string>, <regex>)`_

These extra tables provide useful information and can let you manipulate
**lnav**'s internal state.  You can get a dump of the entire database schema
by executing the '.schema' SQL command, like so::

    ;.schema

environ
-------

The **environ** table gives you access to the **lnav** process' environment
variables.  You can SELECT, INSERT, and UPDATE environment variables, like
so:

.. code-block:: custsqlite

    ;SELECT * FROM environ WHERE name = 'SHELL'
     name   value
    SHELL /bin/tcsh

    ;UPDATE environ SET value = '/bin/sh' WHERE name = 'SHELL'

Environment variables can be used to store simple values or pass values
from **lnav**'s SQL environment to **lnav**'s commands.  For example, the
"open" command will do variable substitution, so you can insert a variable
named "FILENAME" and then open it in **lnav** by referencing it with
"$FILENAME":

.. code-block:: custsqlite

    ;INSERT INTO environ VALUES ('FILENAME', '/path/to/file')
    :open $FILENAME


.. _table_lnav_events:

lnav_events
-----------

The **lnav_events** table allows you to react to events that occur while
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

The **lnav_file** table allows you to examine and perform limited updates to
the metadata for the files that are currently loaded into **lnav**.  The
following columns are available in this table:

  :device: The device the file is stored on.
  :inode: The inode for the file on the device.
  :filepath: If this is a real file, it will be the absolute path.  Otherwise,
    it is a symbolic name.  If it is a symbolic name, it can be UPDATEd so that
    this file will be considered when saving and loading session information.
  :format: The log file format for the file.
  :lines: The number of lines in the file.
  :time_offset: The millisecond offset for timestamps.  This column can be
    UPDATEd to change the offset of timestamps in the file.

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
:paused: Indicates if the view is paused and will not load new data.
:search: The search string for this view.  This value can be UPDATEd to
  initiate a text search in this view.
:filtering: Indicates if the view is applying filters.
:movement: The movement mode, either 'top' or 'cursor'.

lnav_views_echo
---------------

The :code:`lnav_views_echo` table is a real SQLite table that you can create
TRIGGERs on in order to react to users moving around in a view.

.. note::

    The table is periodically updated to reflect the current state of the views.
    The changes are *not* performed immediately after the user action.

lnav_view_stack
---------------

The **lnav_view_stack** table allows you to SELECT and DELETE from the stack of
**lnav** "views" (e.g. log, text, ...).  The following columns are available in
this table:

  :name: The name of the view.

.. _table_lnav_view_filters:

lnav_view_filters
-----------------

The **lnav_view_filters** table allows you to manipulate the filters in the
**lnav** views.  The following columns are available in this table:

  :view_name: The name of the view the filter is applied to.
  :filter_id: The filter identifier.  This will be assigned on insertion.
  :enabled: Indicates whether this filter is enabled or disabled.
  :type: The type of filter, either 'in' or 'out'.
  :pattern: The regular expression to filter on.

This table supports SELECT, INSERT, UPDATE, and DELETE on the table rows to
read, create, update, and delete filters for the views.

lnav_view_filter_stats
----------------------

The **lnav_view_filter_stats** table allows you to get information about how
many lines matched a given filter.  The following columns are available in
this table:

  :view_name: The name of the view.
  :filter_id: The filter identifier.
  :hits: The number of lines that matched this filter.

This table is read-only.

lnav_view_filters_and_stats
---------------------------

The **lnav_view_filters_and_stats** view joins the **lnav_view_filters** table
with the **lnav_view_filter_stats** table into a single view for ease of use.

all_logs
--------

.. f0:sql.tables.all_logs

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
