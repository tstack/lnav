.. include:: kbd.rst

.. _usage:

Usage
=====

This chapter contains an overview of how to use **lnav**.


Basic Controls
--------------

Like most file viewers, scrolling through files can be done with the usual
:ref:`hotkeys<hotkeys>`.  For non-trivial operations, you can enter the
:ref:`command<commands>` prompt by pressing |ks| : |ke|.  To analyze data in a
log file, you can enter the :ref:`SQL prompt<sql-ext>` by pressing |ks| ; |ke|.

.. tip::

  Check the bottom right corner of the screen for tips on hotkeys that might
  be useful in the current context.

  .. figure:: hotkey-tips.png
     :align: center

     When **lnav** is first open, it suggests using |ks| e |ke| and
     |ks| Shift |ke| + |ks| e |ke| to jump to error messages.


Viewing Files
-------------

The files to view in **lnav** can be given on the command-line or passed to
the :ref:`:open<open>` command.  If the path is a directory, all of the files
in the directory will be opened and the directory will be monitored for files
to be added or removed from the view.  A
`glob pattern <https://en.wikipedia.org/wiki/Glob_(programming)>`_ can be
given to watch for files with a common name.  The files that are found will be
scanned to identify their file format.  Files that match a log format will be
collated by time and displayed in the LOG view.  Plain text files can be viewed
in the TEXT view, which can be accessed by pressing |ks| t |ke|.


Searching
---------

Any log messages that are loaded into **lnav** are indexed by time and log
level (e.g. error, warning) to make searching quick and easy with
:ref:`hotkeys<hotkeys>`.  For example, pressing |ks| e |ke| will jump to the
next error in the file and pressing |ks| Shift |ke| + |ks| e |ke| will jump to
the previous error.  Plain text searches can be done by pressing |ks| / |ke|
to enter the search prompt.  A regular expression can be entered into the
prompt to start a search through the current view.


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


Search Tables
-------------

TBD