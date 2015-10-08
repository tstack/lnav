[![Build Status](https://travis-ci.org/tstack/lnav.png)](https://travis-ci.org/tstack/lnav)
[![Bounties](https://img.shields.io/bountysource/team/lnav/activity.svg)](https://www.bountysource.com/teams/lnav)

_This is the source repository for **lnav**, visit [http://lnav.org](http://lnav.org) for a high level overview._

LNAV -- The Logfile Navigator
=============================

The log file navigator, lnav, is an enhanced log file viewer that
takes advantage of any semantic information that can be gleaned from
the files being viewed, such as timestamps and log levels.  Using this
extra semantic information, lnav can do things like interleaving
messages from different files, generate histograms of messages over
time, and providing hotkeys for navigating through the file.  It is
hoped that these features will allow the user to quickly and
efficiently zero in on problems.


Prerequisites
-------------

Lnav requires the following software packages:

  * libpcre   - The Perl Compatible Regular Expression (PCRE) library.
  * sqlite    - The SQLite database engine.
  * ncurses   - The ncurses text UI library.
  * readline  - The readline line editing library.
  * zlib      - The zlib compression library.
  * bz2       - The bzip2 compression library.


Installation
------------

Lnav follows the usual GNU style for configuring and installing software:

    $ ./configure
    $ make
    $ sudo make install

__Run ```./autogen.sh``` before running any of the above commands when
compiling from a cloned repository.__

Using
-----

The only file installed is the executable, "lnav".  You can execute it
with no arguments to view the default set of files:

    $ lnav

You can view all the syslog messages by running:

    $ lnav /var/log/messages*


Screenshot
----------

The following screenshot shows a syslog file. Log lines are displayed with
highlights. Errors are red and warnings are yellow.

[![Screenshot](http://tstack.github.io/lnav/lnav-syslog-thumb.png)](http://tstack.github.io/lnav/lnav-syslog.png)

See Also
--------

The lnav website can be found at:

> [http://lnav.org](http://lnav.org)
