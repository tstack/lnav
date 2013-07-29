
User Interface
==============

The main part of the display shows the log lines from the files interleaved
based on time-of-day.  New lines are automatically loaded as they are appended
to the files and, if you are viewing the bottom of the files, lnav will scroll
down to display the new lines, much like 'tail -f'.

On color displays, the lines will be highlighted as follows:

* Errors will be colored in red;
* warnings will be yellow;
  boundaries between days will be underlined; and
* various color highlights will be applied to: IP addresses, SQL keywords,
  XML tags, file and line numbers in Java backtraces, and quoted strings.

To give you an idea of where you are in the file spatially, the right
side of the display has a proportionally sized 'scrollbar' that
indicates your current position in the file.

Above and below the main body are status lines that display:

* the current time;
* the name of the file the top line was pulled from;
* the log format for the top line;
* the current view;
* the line number for the top line in the display;
* the total number of warnings and errors;
* the number of search hits, which updates as more are found; and
* the number of lines not displayed because of filtering.

Finally, the last line on the display is where you can enter search
patterns and execute internal commands, such as converting a
unix-timestamp into a human-readable date.  The command-line is by
the readline library, so the usual set of keyboard shortcuts can
be used.

The body of the display is also used to display other content, such
as: the help file, histograms of the log messages over time, and
SQL results.  The views are organized into a stack so that any time
you activate a new view with a key press or command, the new view
is pushed onto the stack.  Pressing the same key again will pop the
view off of the stack and return you to the previous view.  Note
that you can always use 'q' to pop the top view off of the stack.
