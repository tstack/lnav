# Architecture

This document covers the internal architecture of the Logfile Navigator (lnav),
a terminal-based tool for viewing and analyzing log files.

## Goals

The following goals drive the design and implementation of lnav:

- Don't make the user do something that can be done automatically.

  Example: Automatically detect log formats for files instead of making them
  specify the format for each file.

- Be performant on low-spec hardware.

  Example: Prefer single-threaded optimizations over trying to parallelize

- Operations should be "live" and not block the user from continuing to work.

  Example: Searches are run in the background.

- Provide context-sensitive help.

  Example: When the cursor is over a SQL keyword/function, the help text for
  that is shown above.

- Show a preview of operations so the user knows what is going to happen.

  Example: When entering a `:filter-out` command, the matched parts of the
  lines are highlighted in red.

## Overview

The whole of lnav consists of a
[log file parser](https://docs.lnav.org/en/latest/formats.html),
[text UI](https://docs.lnav.org/en/latest/ui.html),
[integrations with SQLite](https://docs.lnav.org/en/latest/sqlext.html),
[command-line interface](https://docs.lnav.org/en/latest/cli.html), and
[commands for operating on logs](https://docs.lnav.org/en/latest/commands.html).
Since the majority of lnav's operations center around logs, the core
data-structure is the combined log message index. The message index is populated
when new messages are read from log files. The text UI displays a subset of
messages from the index. The SQLite virtual-tables allow for programmatic access
to the messages and lnav's internal state.

[![lnav architecture](docs/lnav-architecture.png)](https://whimsical.com/lnav-architecture-UM594Qo4G3nt2XWaSZA1mh)

## File Monitoring

Each file being monitored by lnav has an associated [`logfile`](src/logfile.hh)
object, be they plaintext files or files with a recognized format.  These
objects are periodically polled by the main event loop to check if the file
was deleted, truncated, or new lines added.  While reading new lines, if no
log format has matched yet, each line will be passed through the log format
regular expressions to try and find a match.  Each line that is read is added
to an index

#### Why is `mmap()` not used?

Note that file contents are consumed using `pread(2)`/`read(2)` and not
`mmap(2)` since `mmap(2)` does not react well to files changing out from
underneath it.  For example, a truncated file would likely result in a
`SIGBUS`.

## Log Messages

As files are being indexed, if a matching format is found, the file is
"promoted" from a plaintext file to a log file.  When the file is promoted,
it is added to the [logfile_sub_source](src/logfile_sub_source.hh), which
collates all log messages together into a single index.

### Timestamp Parsing

Since all log messages need to have a timestamp, timestamp parsing needs to be
very efficient.  The standard `strptime()` function is quite expensive, so lnav
includes an optimized custom parser and code-generator in the
[ptimec](src/ptimec.hh) component.  The code-generator is used at compile-time
to generate parsers for several [common formats](src/time_formats.am).

## Log Formats

[log_format](src/log_format.hh) instances are used to parse lines from files
into `logline` objects. The majority of log formats are
[external_log_format](src/log_format_ext.hh) objects that are create from
[JSON format definitions](https://docs.lnav.org/en/latest/formats.html). The
built-in definitions are located in the [formats](src/formats) directory. Log
formats that cannot be handled through a simple regular expression are
implemented in the [log_format_impls.cc](src/log_format_impls.cc) file.

## User Interface

The lnav text-user-interface is built on top of
[ncurses](https://invisible-island.net/ncurses/announce.html).
However, the higher-level functionality of panels, widgets, and such is not
used.  Instead, the following custom components are built on top of the ncurses
primitives:

- [view_curses](src/view_curses.hh) - Provides the basics for text roles, which
  allows for themes to color and style text. The `mvwattrline()` function does
  all the heavy lifting of drawing ["attributed" lines](src/base/attr_line.hh),
  which are strings that have attributes associated with a given range of
  characters.
- [listview_curses](src/listview_curses.hh) - Displays a list of items that are
  provided by a source.
- [textview_curses](src/textview_curses.hh) - Builds on the list view by adding
  support for searching, filtering, bookmarks, etc...  The main panel that
  displays the logs/plaintext/help is a textview.
- [statusview_curses](src/state-extension-functions.cc) - Draws the status bars
  at the top and bottom of the TUI.
- [vt52_curses](src/vt52_curses.hh) - Adapts vt52 escape codes to the ncurses
  API.
- [readline_curses](src/readline_curses.hh) - Provides access to the readline
  library.  The readline code is executed in a child process since readline
  does not get along with ncurses.  The child process and readline is set to
  use a vt52 terminal and the vt52_curses view is uses to translate those
  escape codes to ncurses.

The following diagram shows the underlying components that make up the TUI:

[![lnav TUI](docs/lnav-tui.png)](https://whimsical.com/lnav-tui-MQjXc7Vx23BxQTHrnuNp5F)
