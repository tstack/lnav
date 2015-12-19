
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
* delete-filter <regex> - Delete the filter.
* set-min-log-level <level> - Only display log lines with the given log level
  or higher.
* hide-lines-before <abs-time|rel-time> - Hide lines before the given time.
* hide-lines-after <abs-time|rel-time> - Hide lines after the given time.
* show-lines-before-and-after - Show lines that were hidden by the "hide-lines" commands.

Navigation
----------

* goto <line#|N%|abs-time|relative-time> - Go to the given line number, N
  percent into the file, the given timestamp in the log view, or by the
  relative time (e.g. 'a minute ago').
* relative-goto <line#|N%> - Move the current view up or down by the given
  amount.
* next-mark error|warning|search|user|file|partition - Move to the next
  bookmark of the given type in the current view.
* prev-mark error|warning|search|user|file|partition - Move to the previous
  bookmark of the given type in the current view.

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

* zoom-to <zoom-level> - Set the zoom level for the histogram view.

* redraw - Redraw the window to correct any corruption.


SQL
---

* create-logline-table <table-name> - Create an SQL table using the top line
  of the log view as a template.  See the :ref:`data-ext` section for more information.

* delete-logline-table <table-name> - Delete a table created by create-logline-table.

* create-search-table <table-name> [regex] - Create an SQL table that
  extracts information from logs using the provided regular expression or the
  last search that was done.  Any captures in the expression will be used as
  columns in the SQL table.  If the capture is named, that name will be used as
  the column name, otherwise the column name will be of the form 'col_N'.
* delete-search-table <table-name> - Delete a table that was created with create-search-table.


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


Miscellaneous
-------------

* echo [-n] <msg> - Display the given message in the command prompt.  Useful
  for scripts to display messages to the user.  The '-n' option leaves out the
  new line at the end of the message.
* eval <cmd> - Evaluate the given command or SQL query after performing
  environment variable substitution.  The argument to *eval* must start with a
  colon, semi-colon, or pipe character to signify whether the argument is a
  command, SQL query, or a script to be executed, respectively.
