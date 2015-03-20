
.. _commands:

Command Reference
=================

This reference covers the commands used to control **lnav**.  Consult the
`built-in help <https://github.com/tstack/lnav/blob/master/src/help.txt>`_ in
**lnav** for a more detailed explanation of each command.

Note that almost all commands support TAB-completion for their arguments, so
if you are in doubt as to what to type for an argument, you can double tap the
TAB key to get suggestions.

Filtering
---------

The set of log messages that are displayed in the log view can be controlled
with the following commands:

* filter-in <regex> - Only display log lines that match a regex.
* filter-out <regex> - Do not display log lines that match a regex.
* disable-filter <regex> - Disable the given filter.
* enable-filter <regex> - Enable the given filter.
* set-min-log-level <level> - Only display log lines with the given log level
  or higher.

Navigation
----------

* goto <line#|N%|time> - Go to the given line number, N percent into the
  file, or the given timestamp in the log view.

Time
----

* adjust-log-time <date> - Change the timestamps for a log file.
* unix-time <secs-or-date> - Convert a unix-timestamp in seconds to a
  human-readable form or vice-versa.
* current-time - Print the current time in human-readable form and as
  a unix-timestamp.

Display
-------

* help - Display the built-in help text.

* disable-word-wrap - Disable word wrapping in the log and text file views.
* enable-word-wrap - Enable word wrapping in the log and text file views.

* highlight <regex> - Colorize text that matches the given regex.
* clear-highlight <regex> - Clear a previous highlight.

* switch-to-view <name> - Switch to the given view name (e.g. log, text, ...)

* redraw - Redraw the window to correct any corruption.

Output
------

* append-to <file> - Append any bookmarked lines in the current view to the
  given file.
* write-to <file> - Overwrite the given file with any bookmarked lines in
  the current view.  Use '-' to write the lines to the terminal.
* write-csv-to <file> - Write SQL query results to the given file in CSV format.
  Use '-' to write the lines to the terminal.
* write-json-to <file> - Write SQL query results to the given file in JSON
  format.  Use '-' to write the lines to the terminal.
* pipe-to <shell-cmd> - Pipe the bookmarked lines in the current view to a
  shell command and open the output in lnav.
* pipe-line-to <shell-cmd> - Pipe the top line in the current view to a shell
  command and open the output in lnav.
