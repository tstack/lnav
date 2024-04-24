# lnav

A fancy log file viewer for the terminal.

## Overview

The Logfile Navigator, **lnav**, is an enhanced log file viewer that
takes advantage of any semantic information that can be gleaned from
the files being viewed, such as timestamps and log levels. Using this
extra semantic information, lnav can do things like interleaving
messages from different files, generate histograms of messages over
time, and providing hotkeys for navigating through the file. It is
hoped that these features will allow the user to quickly and
efficiently zero in on problems.

## Opening Paths/URLs

The main arguments to lnav are the local/remote files, directories,
glob patterns, or URLs to be viewed. If no arguments are given, the
default syslog file for your system will be opened. These arguments
will be polled periodically so that any new data or files will be
automatically loaded. If a previously loaded file is removed or
replaced, it will be closed and the replacement opened.

Note: When opening SFTP URLs, if the password is not provided for the
host, the SSH agent can be used to do authentication.

## Options

Lnav takes a list of files to view and/or you can use the flag
arguments to load well-known log files, such as the syslog log
files. The flag arguments are:

* `-a` Load all of the most recent log file types.
* `-r` Recursively load files from the given directory hierarchies.
* `-R` Load older rotated log files as well.

When using the flag arguments, lnav will look for the files relative
to the current directory and its parent directories. In other words,
if you are working within a directory that has the well-known log
files, those will be preferred over any others.

If you do not want the default syslog file to be loaded when
no files are specified, you can pass the `-N` flag.

Any files given on the command-line are scanned to determine their log
file format and to create an index for each line in the file. You do
not have to manually specify the log file format. The currently
supported formats are: syslog, apache, strace, tcsh history, and
generic log files with timestamps.

Lnav will also display data piped in on the standard input.

To automatically execute queries or lnav commands after the files
have been loaded, you can use the following options:

* `-c cmd` A command, query, or file to execute. The first character
  determines the type of operation: a colon (`:`) is used for the
  built-in commands; a semi-colon (`;`) for SQL/PRQL queries; and a
  pipe symbol (`|`) for executing a file containing other
  commands. For example, to open the file "foo.log" and go
  to the tenth line in the file, you can do:

  ```shell
  lnav -c ':goto 10' foo.log
  ```

  This option can be given multiple times to execute multiple
  operations in sequence.
* `-f file` A file that contains commands, queries, or files to execute.
  This option is a shortcut for `-c '|file'`. You can use a dash
  (`-`) to execute commands from the standard input.

To execute commands/queries without opening the interactive text UI,
you can pass the `-n` option. This combination of options allows you to
write scripts for processing logs with lnav. For example, to get a list
of IP addresses that dhclient has bound to in CSV format:

```lnav
#! /usr/bin/lnav -nf

# Usage: dhcp_ip.lnav /var/log/messages
# Only include lines that look like:
# Apr 29 00:31:56 example-centos5 dhclient: bound to 10.1.10.103 -- renewal in 9938 seconds.

:filter-in dhclient: bound to

# The log message parser will extract the IP address
# as col_0, so we select that and alias it to "dhcp_ip".
;SELECT DISTINCT col_0 AS dhcp_ip FROM logline;

# Finally, write the results of the query to stdout.
:write-csv-to -
```

## Display

The main part of the display shows the log lines from the files interleaved
based on time-of-day. New lines are automatically loaded as they are appended
to the files and, if you are viewing the bottom of the files, lnav will scroll
down to display the new lines, much like `tail -f`.

On color displays, the lines will be highlighted as follows:

* Errors will be colored in <span class="-lnav_log-level-styles_error">red</span>;
* warnings will be <span class="-lnav_log-level-styles_warning">yellow</span>;
* boundaries between days will be ${ansi_underline}underlined${ansi_norm}; and
* various color highlights will be applied to: IP addresses, SQL keywords,
  XML tags, file and line numbers in Java backtraces, and quoted strings.

To give you an idea of where you are spatially, the right side of the
display has a proportionally sized 'scroll bar' that indicates your
current position in the files. The scroll bar will also show areas of
the file where warnings or errors are detected by coloring the bar
yellow or red, respectively. Tick marks will also be added to the
left and right-hand side of the bar, for search hits and bookmarks.

The bar on the left side indicates the file the log message is from. A
break in the bar means that the next log message comes from a different
file. The color of the bar is derived from the file name. Pressing the
left-arrow or `h` will reveal the source file names for each message and
pressing again will show the full paths.

Above and below the main body are status lines that display a variety
of information. The top line displays:

* The current time, configurable by the `/ui/clock-format` property.
* The highest priority message from the `lnav_user_notifications` table.
  You can insert rows into this table to display your own status messages.
  The default message displayed on startup explains how to focus on the
  next status line at the top, which is an interactive breadcrumb bar.

The second status line at the top display breadcrumbs for the top line
in the main view. Pressing `ENTER` will focus input on the breadcrumb
bar, the cursor keys can be used to select a breadcrumb. The common
breadcrumbs are:

* The name of the current view.
* In the log view, the timestamp of the top log message.
* In the log view, the format of the log file the top log message is from.
* The name of the file the top line was pulled from.
* If the top line is within a larger chunk of structured data, the path to
  the value in the top line will be shown.

Notes:

1. Pressing `CTRL-A`/`CTRL-E` will select the first/last breadcrumb.
1. Typing text while a breadcrumb is selected will perform a fuzzy
   search on the possibilities.

The bottom status bar displays:

* The line number for the top line in the display.
* The current search hit, the total number of hits, and the search term.

If the view supports filtering, there will be a status line showing the
following:

* The number of enabled filters and the total number of filters.
* The number of lines not displayed because of filtering.

To edit the filters, you can press TAB to change the focus from the main
view to the filter editor. The editor allows you to create, enable/disable,
and delete filters easily.

Along with filters, a "Files" panel will also be available for viewing
and controlling the files that lnav is currently monitoring.

Finally, the last line on the display is where you can enter search
patterns and execute internal commands, such as converting a
unix-timestamp into a human-readable date. The command-line is
implemented using the readline library, so the usual set of keyboard
shortcuts are available. Most commands and searches also support
tab-completion.

The body of the display is also used to display other content, such
as: the help file, histograms of the log messages over time, and
SQL results. The views are organized into a stack so that any time
you activate a new view with a key press or command, the new view
is pushed onto the stack. Pressing the same key again will pop the
view off of the stack and return you to the previous view. Note
that you can always use `q` to pop the top view off of the stack.

## Default Key Bindings

### Views

| Key(s) | Action                                                                                                                                                                                                                            |
| ------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **?**  | View/leave this help message.                                                                                                                                                                                                     |
| **q**  | Leave the current view or quit the program when in the log file view.                                                                                                                                                             |
| Q      | Similar to `q`, except it will try to sync the top time between the current and former views. For example, when leaving the spectrogram view with `Q`, the top time in that view will be matched to the top time in the log view. |
| TAB    | Toggle focusing on the filter editor or the main view.                                                                                                                                                                            |
| ENTER  | Focus on the breadcrumb bar.                                                                                                                                                                                                      |
| a/A    | Restore the view that was previously popped with `q`/`Q`. The `A` hotkey will try to match the top times between the two views.                                                                                                   |
| X      | Close the current text file or log file.                                                                                                                                                                                          |

### Spatial Navigation

| Key(s)               | Action                                                                                                                                                                                                                                                                                                                                                          |
|----------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| g/Home               | Move to the top of the file.                                                                                                                                                                                                                                                                                                                                    |
| G/End                | Move to the end of the file. If the view is already at the end, it will move to the last line.                                                                                                                                                                                                                                                                  |
| SPACE/PgDn           | Move down a page.                                                                                                                                                                                                                                                                                                                                               |
| CTRL+d               | Move down by half a page.                                                                                                                                                                                                                                                                                                                                       |
| b/PgUp               | Move up a page.                                                                                                                                                                                                                                                                                                                                                 |
| CTRL+u               | Move up by half a page.                                                                                                                                                                                                                                                                                                                                         |
| j/&DownArrow;        | Move down a line.                                                                                                                                                                                                                                                                                                                                               |
| k/&UpArrow;          | Move up a line.                                                                                                                                                                                                                                                                                                                                                 |
| h/&LeftArrow;        | Move to the left. In the log view, moving left will reveal the source log file names for each line. Pressing again will reveal the full path.                                                                                                                                                                                                                   |
| l/&RightArrow;       | Move to the right.                                                                                                                                                                                                                                                                                                                                              |
| H/Shift &LeftArrow;  | Move to the left by a smaller increment.                                                                                                                                                                                                                                                                                                                        |
| L/Shift &RightArrow; | Move to the right by a smaller increment.                                                                                                                                                                                                                                                                                                                       |
| e/E                  | Move to the next/previous error.                                                                                                                                                                                                                                                                                                                                |
| w/W                  | Move to the next/previous warning.                                                                                                                                                                                                                                                                                                                              |
| n/N                  | Move to the next/previous search hit. When pressed repeatedly within a short time, the view will move at least a full page at a time instead of moving to the next hit.                                                                                                                                                                                         |
| f/F                  | Move to the next/previous file. In the log view, this moves to the next line from a different file. In the text view, this rotates the view to the next file.                                                                                                                                                                                                   |
| &gt;/&lt;            | Move horizontally to the next/previous search hit.                                                                                                                                                                                                                                                                                                              |
| o/O                  | Move forward/backward to the log message with a matching 'operation ID' (opid) field.                                                                                                                                                                                                                                                                           |
| u/U                  | Move forward/backward through any user bookmarks you have added using the 'm' key. This hotkey will also jump to the start of any log partitions that have been created with the 'partition-name' command.                                                                                                                                                      |
| s/S                  | Move to the next/previous "slow down" in the log message rate. A slow down is detected by measuring how quickly the message rate has changed over the previous several messages. For example, if one message is logged every second for five seconds and then the last message arrives five seconds later, the last message will be highlighted as a slow down. |
| {/}                  | Move to the previous/next section in the view.  In the LOG view, this moves through partitions.  In other views, it moves through sections of documents.                                                                                                                                                                                                        |

### Chronological Navigation

| Key(s)        | Action                                                                                                                                                                                                                                                                                                                               |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| d/D           | Move forward/backward 24 hours from the current position in the log file.                                                                                                                                                                                                                                                            |
| 1-6/Shift 1-6 | Move to the next/previous n'th ten minute of the hour. For example, '4' would move to the first log line in the fortieth minute of the current hour in the log. And, '6' would move to the next hour boundary.                                                                                                                       |
| 7/8           | Move to the previous/next minute.                                                                                                                                                                                                                                                                                                    |
| 0/Shift 0     | Move to the next/previous day boundary.                                                                                                                                                                                                                                                                                              |
| r/R           | Move forward/backward based on the relative time that was last used with the 'goto' command. For example, executing ':goto a minute later' will move the log view forward a minute and then pressing 'r' will move it forward a minute again. Pressing 'R' will then move the view in the opposite direction, so backwards a minute. |

### Bookmarks

| Key(s) | Action                                                                                                                                                                                                      |
| ------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| m      | Mark/unmark the line at the top of the display. The line will be highlighted with reverse video to indicate that it is a user bookmark. You can use the `u` hotkey to iterate through marks you have added. |
| M      | Mark/unmark all the lines between the top of the display and the last line marked/unmarked.                                                                                                                 |
| J      | Mark/unmark the next line after the previously marked line.                                                                                                                                                 |
| K      | Like `J` except it toggles the mark on the previous line.                                                                                                                                                   |
| c      | Copy the marked text to the X11 selection buffer or OS X clipboard.                                                                                                                                         |
| C      | Clear all marked lines.                                                                                                                                                                                     |

### Display options

| Key(s)        | Action                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| P             | Switch to/from the pretty-printed view of the log or text files currently displayed. In this view, structured data, such as XML, will be reformatted to make it easier to read.                                                                                                                                                                                                                                                                                                                                              |
| t             | Switch to/from the text file view. The text file view is for any files that are not recognized as log files.                                                                                                                                                                                                                                                                                                                                                                                                                 |
| =             | Pause/unpause loading of new file data.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| Ctrl-L        | (Lo-fi mode) Exit screen-mode and write the displayed log lines in plain text to the terminal until a key is pressed. Useful for copying long lines from the terminal without picking up any of the extra decorations.                                                                                                                                                                                                                                                                                                       |
| T             | Toggle the display of the "elapsed time" column that shows the time elapsed since the beginning of the logs or the offset from the previous bookmark. Sharp changes in the message rate are highlighted by coloring the separator between the time column and the log message. A red highlight means the message rate has slowed down and green means it has sped up. You can use the "s/S" hotkeys to scan through the slow downs.                                                                                          |
| i             | View/leave a histogram of the log messages over time. The histogram counts the number of displayed log lines for each bucket of time. The bars are layed out horizontally with colored segments representing the different log levels. You can use the `z` hotkey to change the size of the time buckets (e.g. ten minutes, one hour, one day).                                                                                                                                                                              |
| I             | Switch between the log and histogram views while keeping the time displayed at the top of each view in sync. For example, if the top line in the log view is "11:40", hitting `I` will switch to the histogram view and scrolled to display "11:00" at the top (if the zoom level is hours).                                                                                                                                                                                                                                 |
| z/Shift Z     | Zoom in or out one step in the histogram view.                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| v             | Switch to/from the SQL result view.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| V             | Switch between the log and SQL result views while keeping the top line number in the log view in sync with the log_line column in the SQL view. For example, doing a query that selects for "log_idle_msecs" and "log_line", you can move the top of the SQL view to a line and hit 'V' to switch to the log view and move to the line number that was selected in the "log_line" column. If there is no "log_line" column, lnav will find the first column with a timestamp and move to corresponding time in the log view. |
| TAB/Shift TAB | In the SQL result view, cycle through the columns that are graphed. Initially, all number values are displayed in a stacked graph. Pressing TAB will change the display to only graph the first column. Repeatedly pressing TAB will cycle through the columns until they are all graphed again.                                                                                                                                                                                                                             |
| p             | In the log view: enable or disable the display of the fields that the log message parser knows about or has discovered. This overlay is temporarily enabled when the semicolon key (;) is pressed so that it is easier to write queries.                                                                                                                                                                                                                                                                                     |
|               | In the DB view: enable or disable the display of values in columns containing JSON-encoded values in the top row. The overlay will display the JSON-Pointer reference and value for all fields in the JSON data.                                                                                                                                                                                                                                                                                                             |
| CTRL-W        | Toggle word-wrapping.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| CTRL-P        | Show/hide the data preview panel that may be opened when entering commands or SQL queries.                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| CTRL-F        | Toggle the enabled/disabled state of all filters in the current view.                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| x             | Toggle the hiding of log message fields. The hidden fields will be replaced with three bullets and highlighted in yellow.                                                                                                                                                                                                                                                                                                                                                                                                    |
| CTRL-X        | Toggle the cursor mode. Allows moving the selected line instead of keeping it fixed at the top of the current screen.                                                                                                                                                                                                                                                                                                                                                                                                        |
| F2            | Toggle mouse support.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |

### Query

| Key(s)                                             | Action                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| -------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **/**_regexp_                                      | Start a search for the given regular expression. The search is live, so when there is a pause in typing, the currently running search will be canceled and a new one started. The first ten lines that match the search will be displayed in the preview window at the bottom of the view. History is maintained for your searches so you can rerun them easily. Words that are currently displayed are also available for tab-completion, so you can easily search for values without needing to copy-and-paste the string. If there is an error encountered while trying to interpret the expression, the error will be displayed in red on the status line. While the search is active, the 'hits' field in the status line will be green, when finished it will turn back to black. |
| **:**&lt;command&gt;                               | Execute an internal command. The commands are listed below. History is also supported in this context as well as tab-completion for commands and some arguments. The result of the command replaces the command you typed.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| **;**&lt;sql&gt;                                   | Execute an SQL query. Most supported log file formats provide a sqlite virtual table backend that can be used in queries. See the SQL section below for more information.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| **&VerticalLine;**&lt;script&gt; [arg1...] | Execute an lnav script contained in a format directory (e.g. \~/.lnav/formats/default). The script can contain lines starting with `:`, `;`, or `\|` to execute commands, SQL queries or execute other files in lnav. Any values after the script name are treated as arguments can be referenced in the script using `\$1`, `\$2`, and so on, like in a shell script.                                                                                                                                                                                                                                                                                                                                                                                                                  |
| CTRL+], ESCAPE                                     | Abort command-line entry started with `/`, `:`, `;`, or `\|`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |

> **Note**: The regular expression format used by lnav is
> [PCRE](http://perldoc.perl.org/perlre.html)
> (Perl-Compatible Regular Expressions).
>
> If the search string is not valid PCRE, a search
> is done for the exact string instead of doing a
> regex search.

## Session

| Key(s) | Action                                                                                                                                   |
| ------ | ---------------------------------------------------------------------------------------------------------------------------------------- |
| CTRL-R | Reset the session state. This will save the current session state (filters, highlights) and then reset the state to the factory default. |

## Filter Editor

The following hotkeys are only available when the focus is on the filter
editor. You can change the focus by pressing TAB.

| Key(s)        | Action                                                              |
| ------------- | ------------------------------------------------------------------- |
| q             | Switch the focus back to the main view.                             |
| j/&DownArrow; | Select the next filter.                                             |
| k/&UpArrow;   | Select the previous filter.                                         |
| o             | Create a new "out" filter.                                          |
| i             | Create a new "in" filter .                                          |
| SPACE         | Toggle the enabled/disabled state of the currently selected filter. |
| t             | Toggle the type of filter between "in" and "out".                   |
| ENTER         | Edit the selected filter.                                           |
| D             | Delete the selected filter.                                         |

## Mouse Support (experimental)

If you are using Xterm, or a compatible terminal, you can use the mouse to
mark lines of text and move the view by grabbing the scrollbar.

NOTE: You need to manually enable this feature by setting the LNAV_EXP
environment variable to "mouse". `F2` toggles mouse support.

## Log Analysis

Lnav has support for performing SQL queries on log files using the
SQLite3 "virtual" table feature. For all supported log file types,
lnav will create tables that can be queried using the subset of SQL
that is supported by SQLite3. For example, to get the top ten URLs
being accessed in any loaded Apache log files, you can execute:

```lnav
;SELECT cs_uri_stem, count(*) AS total FROM access_log
   GROUP BY cs_uri_stem ORDER BY total DESC LIMIT 10;
```

The query result view shows the results and graphs any numeric
values found in the result, much like the histogram view.

The builtin set of log tables are listed below. Note that only the
log messages that match a particular format can be queried by a
particular table. You can find the file format and table name for
the top log message by looking in the upper right hand corner of the
log file view.

Some commonly used format tables are:

| Name        | Description                                                                                                                                    |
| ----------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| access_log  | Apache common access log format                                                                                                                |
| syslog_log  | Syslog format                                                                                                                                  |
| strace_log  | Strace log format                                                                                                                              |
| generic_log | 'Generic' log format. This table contains messages from files that have a very simple format with a leading timestamp followed by the message. |

NOTE: You can get a dump of the schema for the internal tables, and
any attached databases, by running the `.schema` SQL command.

The columns available for the top log line in the view will
automatically be displayed after pressing the semicolon (`;`) key.
All log tables contain at least the following columns:

| Column         | Description                                                                                                                                                                                                                                  |
| -------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| log_line       | The line number in the file, starting at zero.                                                                                                                                                                                               |
| log_part       | The name of the partition.  You can change this column using an UPDATE SQL statement or with the 'partition-name' command.  After a value is set, the following log messages will have the same partition name up until another name is set. |
| log_time       | The time of the log entry.                                                                                                                                                                                                                   |
| log_idle_msecs | The amount of time, in milliseconds, between the current log message and the previous one.                                                                                                                                                   |
| log_level      | The log level (e.g. info, error, etc...).                                                                                                                                                                                                    |
| log_mark       | The bookmark status for the line.  This column can be written to using an UPDATE query.                                                                                                                                                      |
| log_path       | The full path to the file.                                                                                                                                                                                                                   |
| log_text       | The raw line of text.  Note that this column is not included in the result of a 'select *', but it does exist.                                                                                                                               |

The following tables include the basic columns as listed above and
include a few more columns since the log file format is more
structured.

* `syslog_log`

  | Column       | Description                                          |
  | ------------ | ---------------------------------------------------- |
  | log_hostname | The hostname the message was received from.          |
  | log_procname | The name of the process that sent the message.       |
  | log_pid      | The process ID of the process that sent the message. |

* `access_log` (The column names are the same as those in the
  Microsoft LogParser tool.)

  | Column        | Description                               |
  | ------------- | ----------------------------------------- |
  | c_ip          | The client IP address.                    |
  | cs_username   | The client user name.                     |
  | cs_method     | The HTTP method.                          |
  | cs_uri_stem   | The stem portion of the URI.              |
  | cs_uri_query  | The query portion of the URI.             |
  | cs_version    | The HTTP version string.                  |
  | sc_status     | The status number returned to the client. |
  | sc_bytes      | The number of bytes sent to the client.   |
  | cs_referrer   | The URL of the referring page.            |
  | cs_user_agent | The user agent string.                    |

* `strace_log` (Currently, you need to run strace with the `-tt -T`
  options so there are timestamps for each function call.)

  | Column      | Description                              |
  | ----------- | ---------------------------------------- |
  | funcname    | The name of the syscall.                 |
  | result      | The result code.                         |
  | duration    | The amount of time spent in the syscall. |
  | arg0 - arg9 | The arguments passed to the syscall.     |

These tables are created dynamically and not stored in memory or on
disk. If you would like to persist some information from the tables,
you can attach another database and create tables in that database.
For example, if you wanted to save the results from the earlier
example of a top ten query into the "/tmp/topten.db" file, you can do:

```lnav
;ATTACH DATABASE '/tmp/topten.db' AS topten;
;CREATE TABLE topten.foo AS SELECT cs_uri_stem, count(*) AS total
   FROM access_log GROUP BY cs_uri_stem ORDER BY total DESC
   LIMIT 10;
```

### PRQL Support

The Pipelined Relational Query Language
[(PRQL)](https://prql-lang.org) is an alternative database query
language that compiles to SQL.  The main advantage of PRQL,
in the context of lnav, is that it is easier to work with
interactively compared to SQL.  For example, lnav can provide
previews of different stages of the pipeline and provide more
accurate tab-completions for the columns in the result set.

You can execute a PRQL query in the SQL prompt.  A PRQL query 
starts with the `from` keyword that specifies the table to use as 
a data source.  The next stage of a pipeline is started by entering
a pipe symbol (`|`) followed by a
[PRQL transform](https://prql-lang.org/book/reference/stdlib/transforms/index.html).
As you build the query in the prompt, lnav will display any relevant
help and preview for the current and previous stages of the pipeline.

Using the top ten URLs query from earlier as an example, the PRQL
version would be as follows:

```lnav
;from access_log | stats.count_by cs_uri_stem | take 10
```

The first stage selects the data source, the web `access_log` table
in this case.  The `stats.count_by` transform is a convenience
provided by lnav that groups by the given column, counts the rows
in each group, and sorts by count in descending order.  The `take 10`
turns into the `LIMIT 10`.

## Dynamic logline Table (experimental)

(NOTE: This feature is still very new and not completely reliable yet,
use with care.)

For log formats that lack message structure, lnav can parse the log
message and attempt to extract any data fields that it finds. This
feature is available through the `logline` log table. This table is
dynamically created and defined based on the message at the top of
the log view. For example, given the following log message from "sudo",
lnav will create the "logline" table with columns for "TTY", "PWD",
"USER", and "COMMAND":

```
May 24 06:48:38 Tim-Stacks-iMac.local sudo[76387]: stack : TTY=ttys003 ; PWD=/Users/stack/github/lbuild ; USER=root ; COMMAND=/bin/echo Hello, World!
```

Queries executed against this table will then only return results for
other log messages that have the same format. So, if you were to
execute the following query while viewing the above line, you might
get the following results:

```lnav
;SELECT USER,COMMAND FROM logline;
```

| USER | COMMAND                   |
| ---- | ------------------------- |
| root | /bin/echo Hello, World!   |
| mal  | /bin/echo Goodbye, World! |

The log parser works by examining each message for key/value pairs
separated by an equal sign (=) or a colon (:). For example, in the
previous example of a "sudo" message, the parser sees the "USER=root"
string as a pair where the key is "USER" and the value is "root".
If no pairs can be found, then anything that looks like a value is
extracted and assigned a numbered column. For example, the following
line is from "dhcpd":

```
Sep 16 22:35:57 drill dhcpd: DHCPDISCOVER from 00:16:ce:54:4e:f3 via hme3
```

In this case, the lnav parser recognizes that "DHCPDISCOVER", the MAC
address and the "hme3" device name are values and not normal words. So,
it builds a table with three columns for each of these values. The
regular words in the message, like "from" and "via", are then used to
find other messages with a similar format.

If you would like to execute queries against log messages of different
formats at the same time, you can use the 'create-logline-table' command
to permanently create a table using the top line of the log view as a
template.

## Other SQL Features

Environment variables can be used in SQL statements by prefixing the
variable name with a dollar-sign (\$). For example, to read the value of
the `HOME` variable, you can do:

```lnav
;SELECT \$HOME;
```

To select the syslog messages that have a hostname field that is equal
to the `HOSTNAME` variable:

```lnav
;SELECT * FROM syslog_log WHERE log_hostname = \$HOSTNAME;
```

NOTE: Variable substitution is done for fields in the query and is not
a plain text substitution. For example, the following statement
WILL NOT WORK:

```lnav
;SELECT * FROM \$TABLE_NAME; -- Syntax error
```

Access to lnav's environment variables is also available via the "environ"
table. The table has two columns (name, value) and can be read and written
to using SQL SELECT, INSERT, UPDATE, and DELETE statements. For example,
to set the "FOO" variable to the value "BAR":

```lnav
;INSERT INTO environ SELECT 'FOO', 'BAR';
```

As a more complex example, you can set the variable "LAST" to the last
syslog line number by doing:

```lnav
;INSERT INTO environ SELECT 'LAST', (SELECT max(log_line) FROM syslog_log);
```

A delete will unset the environment variable:

```lnav
;DELETE FROM environ WHERE name='LAST';
```

The table allows you to easily use the results of a SQL query in lnav
commands, which is especially useful when scripting lnav.

## Contact

For more information, visit the lnav website at:

http://lnav.org

For support questions, email:

* lnav@googlegroups.com
* support@lnav.org
