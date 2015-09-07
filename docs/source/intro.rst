
Introduction
============

The Log File Navigator, **lnav**, is an enhanced log file viewer that
takes advantage of any semantic information that can be gleaned from
the files being viewed, such as timestamps and log levels.  Using this
extra semantic information, lnav can do things like interleaving
messages from different files, generate histograms of messages over
time, and providing hotkeys for navigating through the file.  It is
hoped that these features will allow the user to quickly and
efficiently zero in on problems.

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
doing::

   $ ./configure
   $ make
   $ sudo make install

Viewing Logs
------------

The arguments to **lnav** are the log files, directories, or URLs to be viewed.
For example, to view all of the CUPS logs on your system::

   $ lnav /var/log/cups

The formats of the logs are determined automatically and indexed on-the-fly.
See :ref:`log-formats` for a listing of the predefined formats and how to
define your own.

If no arguments are given, **lnav** will try to open the syslog file on your
system::

   $ lnav
