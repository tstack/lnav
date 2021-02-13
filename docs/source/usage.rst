
.. _usage:

Usage
=====

This chapter contains an overview of how to use **lnav**.


Basic Controls
--------------

Like most file viewers, scrolling through files can be done with the usual
:ref:`hotkeys<hotkeys>`.  For non-trivial operations, you can enter the
:ref:`command<commands>` prompt by pressing :kbd:`:`.  To analyze data in a
log file, you can enter the :ref:`SQL prompt<sql-ext>` by pressing :kbd:`;`.

.. tip::

  Check the bottom right corner of the screen for tips on hotkeys that might
  be useful in the current context.

  .. figure:: hotkey-tips.png
     :align: center

     When **lnav** is first open, it suggests using :kbd:`e` and
     :kbd:`Shift` + :kbd:`e` to jump to error messages.


Viewing Files
-------------

The files to view in **lnav** can be given on the command-line or passed to the
:ref:`:open<open>` command.  A
`glob pattern <https://en.wikipedia.org/wiki/Glob_(programming)>`_ can be given
to watch for files with a common name.  If the path is a directory, all of the
files in the directory will be opened and the directory will be monitored for
files to be added or removed from the view.  If the path is an archive or
compressed file (and lnav was built with libarchive), the archive will be
extracted to a temporary location and the files within will be loaded.  The
files that are found will be scanned to identify their file format.  Files
that match a log format will be collated by time and displayed in the LOG
view.  Plain text files can be viewed in the TEXT view, which can be accessed
by pressing :kbd:`t`.


Archive Support
^^^^^^^^^^^^^^^

If **lnav** is compiled with `libarchive <https://www.libarchive.org>`_,
any files to be opened will be examined to see if they are a supported archive
type.  If so, the contents of the archive will be extracted to the
:code:`$TMPDIR/lnav-${UID}-archives/` directory.  Once extracted, the files
within will be loaded into lnav.  To speed up opening large amounts of files,
any file that meets the following conditions will be automatically hidden and
not indexed:

* Binary files
* Plain text files that are larger than 128KB
* Duplicate log files

The unpacked files will be left in the temporary directory after exiting
**lnav** so that opening the same archive again will be faster.  Unpacked
archives that have not been accessed in the past two days will be automatically
deleted the next time **lnav** is started.


Searching
---------

Any log messages that are loaded into **lnav** are indexed by time and log
level (e.g. error, warning) to make searching quick and easy with
:ref:`hotkeys<hotkeys>`.  For example, pressing :kbd:`e` will jump to the
next error in the file and pressing :kbd:`Shift` + :kbd:`e` will jump to
the previous error.  Plain text searches can be done by pressing :kbd:`/`
to enter the search prompt.  A regular expression can be entered into the
prompt to start a search through the current view.


.. _filtering:

Filtering
---------

To reduce the amount of noise in a log file, **lnav** can hide log messages
that match certain criteria.  The following sub-sections explain ways to go
about that.


Regular Expression Match
^^^^^^^^^^^^^^^^^^^^^^^^

If there are log messages that you are not interested in, you can do a
"filter out" to hide messages that match a pattern.  A filter can be created
using the interactive editor, the :ref:`:filter-out<filter_out>` command, or
by doing an :code:`INSERT` into the
:ref:`lnav_view_filters<table_lnav_view_filters>` table.

If there are log messages that you are only interested in, you can do a
"filter in" to only show messages that match a pattern.  The filter can be
created using the interactive editor, the :ref:`:filter-in<filter_in>` command,
or by doing an :code:`INSERT` into the
:ref:`lnav_view_filters<table_lnav_view_filters>` table.


SQLite Expression
^^^^^^^^^^^^^^^^^

Complex filtering can be done by passing a SQLite expression to the
:ref:`:filter-expr<filter_expr>` command.  The expression will be executed for
every log message and if it returns true, the line will be shown in the log
view.


Time
^^^^

To limit log messages to a given time frame, the
:ref:`:hide-lines-before<hide_lines_before>` and
:ref:`:hide-lines-after<hide_lines_after>` commands can be used to specify
the beginning and end of the time frame.


Log level
^^^^^^^^^

To hide messages below a certain log level, you can use the
:ref:`:set-min-log-level<set_min_log_level>`.

.. _search_tables:

Search Tables
-------------

TBD