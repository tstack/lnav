[![Build Status](https://travis-ci.org/tstack/lnav.png)](https://travis-ci.org/tstack/lnav)
[![Build status](https://ci.appveyor.com/api/projects/status/24wskehb7j7a65ro?svg=true)](https://ci.appveyor.com/project/tstack/lnav)
[![Bounties](https://img.shields.io/bountysource/team/lnav/activity.svg)](https://www.bountysource.com/teams/lnav)
[![LoC](https://tokei.rs/b1/github/tstack/lnav)](https://github.com/tstack/lnav).

_This is the source repository for **lnav**, visit [http://lnav.org](http://lnav.org) for a high level overview._

# LNAV -- The Logfile Navigator

The log file navigator, lnav, is an enhanced log file viewer that
takes advantage of any semantic information that can be gleaned from
the files being viewed, such as timestamps and log levels.  Using this
extra semantic information, lnav can do things like interleaving
messages from different files, generate histograms of messages over
time, and providing hotkeys for navigating through the file.  It is
hoped that these features will allow the user to quickly and
efficiently zero in on problems.


## Prerequisites

The following software packages are required to build lnav:

  * gcc/clang - A C++14-compatible compiler.
  * libpcre   - The Perl Compatible Regular Expression (PCRE) library.
  * sqlite    - The SQLite database engine.  Version 3.9.0 or higher is required.
  * ncurses   - The ncurses text UI library.
  * readline  - The readline line editing library.
  * zlib      - The zlib compression library.
  * bz2       - The bzip2 compression library.
  * libcurl   - The cURL library for downloading files from URLs.  Version 7.23.0 or higher is required.


## Installation

Lnav follows the usual GNU style for configuring and installing software:

    $ ./configure
    $ make
    $ sudo make install

__Run `./autogen.sh` before running any of the above commands when
compiling from a cloned repository.__


## Cygwin users

It should compile fine in Cygwin.

Alternatively, you can get the generated binary from [AppVeyor](https://ci.appveyor.com/project/tstack/lnav) artifacts.

Remember that you still need the lnav dependencies under Cygwin, here is a quick way to do it:

    setup-x86_64.exe -q -P libpcre1 -P libpcrecpp0 -P libsqlite3_0 -P libstdc++6

Currently, the x64 version seems to be working better than the x86 one.


## Usage

The only file installed is the executable, `lnav`.  You can execute it
with no arguments to view the default set of files:

    $ lnav

You can view all the syslog messages by running:

    $ lnav /var/log/messages*

### Usage with `systemd-journald`

On systems running `systemd-journald`, you can use `lnav` as the pager:

    $ journalctl | lnav

or in follow mode:

    $ journalctl -f | lnav

Since `journalctl`'s default output format omits the year, if you are
viewing logs which span multiple years you will need to change the
output format to include the year, otherwise `lnav` gets confused:

    $ journalctl -o short-iso | lnav

It is also possible to use `journalctl`'s json output format and `lnav`
will make use of additional fields such as PRIORITY and _SYSTEMD_UNIT:

    $ journalctl -o json | lnav

In case some MESSAGE fields contain special characters such as
ANSI color codes which are considered as unprintable by journalctl,
specifying `journalctl`'s `-a` option might be preferable in order
to output those messages still in a non binary representation:

    $ journalctl -a -o json | lnav

If using systemd v236 or newer, the output fields can be limited to
the ones actually recognized by `lnav` for increased efficiency:

    $ journalctl -o json --output-fields=MESSAGE,PRIORITY,_PID,SYSLOG_IDENTIFIER,_SYSTEMD_UNIT | lnav

If your system has been running for a long time, for increased
efficiency you may want to limit the number of log lines fed into
`lnav`, e.g. via `journalctl`'s `-n` or `--since=...` options.

In case of a persistent journal, you may want to limit the number
of log lines fed into `lnav` via `journalctl`'s `-b` option.

## Screenshot

The following screenshot shows a syslog file. Log lines are displayed with
highlights. Errors are red and warnings are yellow.

[![Screenshot](http://tstack.github.io/lnav/lnav-syslog-thumb.png)](http://tstack.github.io/lnav/lnav-syslog.png)


See Also
--------

The lnav website can be found at:

> [http://lnav.org](http://lnav.org)

