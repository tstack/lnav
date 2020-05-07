

Introduction
============

The Log File Navigator, **lnav**, is an advanced log file viewer for the
terminal.  It provides an :ref:`easy-to-use interface<ui>` for monitoring and
analyzing your log files with little to no setup.  Simply point **lnav** at
your log files and it will automatically detect the :ref:`log-formats`, index
their contents, and display a combined view of all log messages.  A variety of
:ref:`hotkeys<hotkeys>` allow you to quickly navigate through the logs by
message level or time.  :ref:`Commands<commands>` give you additional
control over **lnav**'s behavior for doing things like applying filters,
tagging messages, and much more.  And, the :ref:`sql-ext` allows you to analyze
your log messages by executing queries over the log messages.

Dependencies
------------

When compiling from source, the following dependencies are required:

* `NCurses <http://www.gnu.org/s/ncurses/>`_
* `PCRE <http://www.pcre.org>`_ -- Versions greater than 8.20 give better
  performance since the PCRE JIT will be leveraged.
* `SQLite <http://www.sqlite.org>`_
* `ZLib <http://wwww.zlib.net>`_
* `Bzip2 <http://www.bzip.org>`_
* `Readline <http://www.gnu.org/s/readline>`_

Installation
------------

Check the `downloads page <http://lnav.org/downloads>`_ to see if there are
packages for your operating system.  Compiling from source is just a matter of
doing:

.. prompt:: bash

   ./configure
   make
   sudo make install

Viewing Logs
------------

The arguments to **lnav** are the log files, directories, or URLs to be viewed.
For example, to view all of the CUPS logs on your system:

.. prompt:: bash

   lnav /var/log/cups

The formats of the logs are determined automatically and indexed on-the-fly.
See :ref:`log-formats` for a listing of the predefined formats and how to
define your own.

If no arguments are given, **lnav** will try to open the syslog file on your
system:

.. prompt:: bash

   lnav
