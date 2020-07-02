
.. include:: kbd.rst

.. _usage:

Usage
=====

This chapter contains an overview of how to make use of **lnav**.

Viewing Files
-------------

The files to view in **lnav** can be listed on the command-line or passed to the
:ref:`:open<open>` command.  If the path is a directory, all of the files in the
directory will be opened and the directory will be monitored for files to be added
or removed.  A
`glob pattern <https://en.wikipedia.org/wiki/Glob_(programming)>`_ can also be
used to watch for files that match the pattern.  The files that are found will be
scanned to identify their file format.  Files that match a log format will be
collated and displayed in the LOG view.  Plain text files can be viewed in the
TEXT view, which can be accessed by pressing |ks| t |ke|.

Log message parsing is the base that many of the other features mentioned in this
chapter are built upon.  Messages are matched against regular expressions defined
in log format definitions.  When **lnav** first opens a file, it will cycle through
all of the known formats and try to match the beginning lines in the file against
the patterns.  When a match is found, 


Searching
---------




Filtering
---------

To reduce the amount of noise in a log file, **lnav** can filter out log messages
based on the following criteria:

* Regular expression match
* Time
* Log level

Search Tables
-------------


