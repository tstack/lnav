
User Interface
==============

The main part of the display shows the log messages from all files sorted by the
message time.  Status bars at the top and bottom of the screen can given you an
idea of where you are in the logs.  And, the last line is used for entering
commands.  Navigation is controlled by a series of hotkeys, see :ref:`hotkeys`
for more information.

.. figure:: lnav-ui.png
   :align: center
   :alt: Screenshot showing syslog messages.

   Screenshot of **lnav** viewing syslog messages.

On color displays, the log messages will be highlighted as follows:

* Errors will be colored in red;
* warnings will be yellow;
* search hits are reverse video;
* various color highlights will be applied to: IP addresses, SQL keywords,
  XML tags, file and line numbers in Java backtraces, and quoted strings;
* "identifiers" in the messages will be randomly assigned colors based on their
  content (works best on "xterm-256color" terminals).

The right side of the display has a proportionally sized 'scrollbar' that
shows:

* your current position in the file;
* the locations of errors/warnings in the log files by using a red or yellow
  coloring;
* the locations of search hits by using a tick-mark pointing to the left;
* the locations of bookmarks by using a tick-mark pointing to the right.

Above and below the main body are status lines that display:

* the current time;
* the name of the file the top line was pulled from;
* the log format for the top line;
* the current view;
* the line number for the top line in the display;
* the current search hit, the total number of hits, and the search term;

If the view supports filtering, there will be a status line showing the
following:

  * the number of enabled filters and the total number of filters;
  * the number of lines that are **not** displayed because of filtering.

To edit the filters, you can press TAB to change the focus from the main
view to the filter editor.  The editor allows you to create, enable/disable,
and delete filters easily.

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
