.. _hotkeys:

Hotkey Reference
================

This reference covers the keys used to control **lnav**.  Consult the `built-in
help <https://github.com/tstack/lnav/blob/master/src/help.md>`_ in **lnav** for
a more detailed explanation of each key.

Global
------

.. list-table::
   :header-rows: 1
   :widths: 6 20

   * - Keypress
     - Command

   * - :kbd:`Ctrl` + :kbd:`C`
     - If the focused line is from a file connected to an open pipe (the "↻"
       icon will be next to the file name), a SIGINT will be sent to the
       child process.  Otherwise, **lnav** will quickly exit.  If **lnav**
       seems to be stuck in a loop, pressing :kbd:`Ctrl` + :kbd:`C` three
       times will trigger an abort exit.

Spatial Navigation
------------------

The majority of these hotkeys should be available in all views.

.. list-table::
   :header-rows: 1
   :widths: 6 6 6 20

   * - Keypress
     -
     -
     - Command
   * - :kbd:`Space`
     - :kbd:`PgDn`
     -
     - Down a page
   * - :kbd:`Ctrl` + :kbd:`d`
     -
     -
     - Down by half a page
   * - :kbd:`b`
     - :kbd:`Backspace`
     - :kbd:`PgUp`
     - Up a page
   * - :kbd:`Ctrl` + :kbd:`u`
     -
     -
     - Up by half a page
   * - :kbd:`j`
     - :kbd:`↓`
     -
     - Down a line
   * - :kbd:`k`
     - :kbd:`↑`
     -
     - Up a line
   * - :kbd:`h`
     - :kbd:`←`
     -
     - Left half a page.  In the log view, pressing left while at the start of
       the message text will reveal the shortened source file name for each line.
       Pressing again will reveal the full path.
   * - :kbd:`Shift` + :kbd:`h`
     - :kbd:`Shift` + :kbd:`←`
     -
     - Left ten columns
   * - :kbd:`l`
     - :kbd:`→`
     -
     - Right half a page.  In the log view, pressing right while at the start
       of the message text can hide the timestamp/level fields in the message
       and insert a shorter timestamp column on the left side.  The column
       should take less space than the existing field and aligns all
       timestamps across all log formats.  This feature is gated by the
       :ref:`/ui/views/log/time-column<config_log_time_column>` setting.
   * - :kbd:`Shift` + :kbd:`l`
     - :kbd:`Shift` + :kbd:`→`
     -
     - Right ten columns
   * - :kbd:`Home`
     - :kbd:`g`
     -
     - Top of the view
   * - :kbd:`End`
     - :kbd:`G`
     -
     - Bottom of the view
   * - :kbd:`e`
     - :kbd:`Shift` + :kbd:`e`
     -
     - Next/previous error
   * - :kbd:`w`
     - :kbd:`Shift` + :kbd:`w`
     -
     - Next/previous warning
   * - :kbd:`n`
     - :kbd:`Shift` + :kbd:`n`
     -
     - Next/previous search hit
   * - :kbd:`>`
     - :kbd:`<`
     -
     - Next/previous search hit (horizontal)
   * - :kbd:`f`
     - :kbd:`Shift` + :kbd:`f`
     -
     - Next/previous file
   * - :kbd:`u`
     - :kbd:`Shift` + :kbd:`u`
     -
     - Next/previous bookmark
   * - :kbd:`o`
     - :kbd:`Shift` + :kbd:`o`
     -
     - Forward/backward through log messages with a matching "opid" field
   * - :kbd:`s`
     - :kbd:`Shift` + :kbd:`s`
     -
     - Next/previous slow down in the log message rate
   * - :kbd:`{`
     - :kbd:`}`
     -
     - Previous/next location in history

Chronological Navigation
------------------------

These hotkeys are only functional on views that are time-based, like the log
view or the histogram view.

.. list-table::
   :header-rows: 1
   :widths: 5 5 20

   * - Keypress
     -
     - Command
   * - :kbd:`d`
     - :kbd:`Shift` + :kbd:`d`
     - Forward/backward 24 hours
   * - :kbd:`1` - :kbd:`6`
     - :kbd:`Shift` + :kbd:`1` - :kbd:`6`
     - Next/previous n'th ten minute of the hour
   * - :kbd:`7`
     - :kbd:`8`
     - Previous/next minute
   * - :kbd:`0`
     - :kbd:`Shift` + :kbd:`0`
     - Next/previous day
   * - :kbd:`r`
     - :kbd:`Shift` + :kbd:`r`
     - Forward/backward by the relative time that was last used with the goto command.

Breadcrumb Navigation
---------------------

The following hotkeys are related to the breadcrumb bar that is below the top
status bar.

.. list-table::
   :header-rows: 1
   :widths: 5 20

   * - Keypress
     - Description
   * - :kbd:`\``
     - Focus on the breadcrumb bar.
   * - :kbd:`ENTER`
     - If the bar is currently focused, accept the selected value and drop focus.
   * - :kbd:`Escape`
     - Drop focus on the breadcrumb bar.
   * - :kbd:`←`
     - Select the crumb to the left. If the first crumb is selected, the
       selection will wrap around to the last crumb.
   * - :kbd:`→`
     - Accept the current value, which might mean navigating to the value in
       the view, then selecting the crumb to the right.
   * - :kbd:`Ctrl` + :kbd:`a`
     - Select the first crumb.
   * - :kbd:`Ctrl` + :kbd:`e`
     - Select the last crumb.
   * - :kbd:`↓`
     - Select the next value in the crumb dropdown.
   * - :kbd:`↑`
     - Select the previous value in the crumb dropdown.
   * - :kbd:`Home`
     - Select the first value in the crumb dropdown.
   * - :kbd:`End`
     - Select the last value in the crumb dropdown.

While a crumb is selected, you can perform a fuzzy search on the possible
values by typing in the value you are interested in.

.. _hotkeys_bookmarks:

Bookmarks
---------

.. list-table::
   :header-rows: 1
   :widths: 5 20

   * - Keypress
     - Command
   * - :kbd:`m`
     - Mark/unmark the top line or focused line when in cursor mode
   * - :kbd:`Shift` + :kbd:`m`
     - Mark/unmark the range of lines from the last marked to the top
   * - :kbd:`Shift` + :kbd:`j`
     - Mark/unmark the next line after the previously marked
   * - :kbd:`Shift` + :kbd:`k`
     - Mark/unmark the previous line
   * - :kbd:`c`
     - Copy marked lines to the clipboard
   * - :kbd:`Shift` + :kbd:`c`
     - Clear marked lines

.. _hotkeys_display:

Display
-------

.. list-table::
   :header-rows: 1
   :widths: 5 20

   * - Keypress
     - Command
   * - :kbd:`?` or :kbd:`F1`
     - View/leave builtin help
   * - :kbd:`q`
     - Return to the previous view/quit
   * - :kbd:`Shift` + :kbd:`q`
     - Return to the previous view/quit while matching the top times of the two views
   * - :kbd:`a`
     - Restore the view that was previously popped with 'q/Q'
   * - :kbd:`Shift` + :kbd:`a`
     - Restore the view that was previously popped with 'q/Q' and match the top times of the views
   * - :kbd:`Shift` + :kbd:`p`
     - Switch to/from the pretty-printed view of the displayed log or text files
   * - :kbd:`Shift` + :kbd:`t`
     - Display the elapsed time from a bookmark to a given line.  In the TEXT view,
       this only works for content that was captured from stdin or a :code:`:sh`
       command.
   * - :kbd:`t`
     - Switch to/from the text file view
   * - :kbd:`i`
     - Switch to/from the histogram view
   * - :kbd:`Shift` + :kbd:`i`
     - Switch to/from the histogram view
   * - :kbd:`v`
     - Switch to/from the SQL result view
   * - :kbd:`Shift` + :kbd:`v`
     - Switch to/from the SQL result view and move to the corresponding in the
       log_line column
   * - :kbd:`p`
     - Toggle the display of the log parser results
   * - :kbd:`Tab`
     - In the log/text views, focus on the configuration panel for editing
       filters and examining the list of loaded files.
   * - :kbd:`Ctrl` + :kbd:`l`
     - Switch to lo-fi mode.  The displayed log lines will be dumped to the
       terminal without any decorations so they can be copied easily.
   * - :kbd:`Ctrl` + :kbd:`w`
     - Toggle word-wrap.
   * - :kbd:`Ctrl` + :kbd:`p`
     - Show/hide the data preview panel that may be opened when entering
       commands or SQL queries.
   * - :kbd:`Ctrl` + :kbd:`f`
     - Toggle the enabled/disabled state of all filters in the current view.
   * - :kbd:`x`
     - Toggle the hiding of log message fields. The hidden fields will be
       replaced with three bullets and highlighted in yellow.
   * - :kbd:`Ctrl` + :kbd:`x`
     - Toggle the cursor mode. Allows moving the focused line instead of
       keeping it fixed at the top of the current screen.
   * - :kbd:`=`
     - Pause/unpause loading of new file data.

Session
-------

.. list-table::
   :header-rows: 1
   :widths: 5 20

   * - Keypress
     - Command
   * - :kbd:`Ctrl` + :kbd:`R`
     - Reset the current :ref:`session<sessions>` state.  The session state
       includes things like filters, bookmarks, and hidden fields.

Query Prompts
-------------

.. list-table::
   :header-rows: 1
   :widths: 5 20

   * - Keypress
     - Command
   * - :kbd:`/`
     - Search for lines matching a regular expression
   * - :kbd:`;`
     - Open the :ref:`sql-ext` to execute SQL statements/queries
   * - :kbd:`:`
     - Execute an internal command, see :ref:`commands` for more information
   * - :kbd:`\|`
     - Execute an lnav script located in a format directory
   * - :kbd:`Ctrl` + :kbd:`]`
     - Abort the prompt

Prompt
------

After activating the prompt, the following shortcuts are available for
editing:

.. list-table::
    :header-rows: 1
    :widths: 6 6 20

    * - Keypress
      -
      - Command
    * - :kbd:`Escape`
      -
      - Depending on the mode: close the completion popup; exit search
        mode; cancel the prompt
    * - :kbd:`Enter`
      -
      - In single line mode, exit the prompt and perform the operation.
        In multi-line mode, insert a new line.
    * - :kbd:`Ctrl` + :kbd:`X`
      -
      - Exit the prompt and perform the operation
    * - :kbd:`F1`
      -
      - Open the help text for the prompt
    * - :kbd:`←`
      -
      - Move left one character
    * - :kbd:`→`
      -
      - Move right one character
    * - :kbd:`Shift` + :kbd:`←`
      -
      - Move left one character and include it in the selection
    * - :kbd:`Shift` + :kbd:`→`
      -
      - Move right one character and include it in the selection
    * - :kbd:`↑`
      - :kbd:`Ctrl` + :kbd:`P`
      - In single-line mode, search the history with the current contents.
        In multi-line mode, move the cursor up a line.  If a popup is open,
        move the selection up.
    * - :kbd:`↓`
      - :kbd:`Ctrl` + :kbd:`N`
      - In multi-line mode, move the cursor down a line.  If a popup is open,
        move the selection down.
    * - :kbd:`Ctrl` + :kbd:`A`
      -
      - Move to the beginning of the line
    * - :kbd:`Ctrl` + :kbd:`E`
      -
      - Move to the end of the line
    * - :kbd:`Ctrl` + :kbd:`K`
      -
      - Cut to the end of the line into the clipboard
    * - :kbd:`Ctrl` + :kbd:`U`
      -
      - Cut from the beginning of the line to the cursor into the clipboard
    * - :kbd:`Ctrl` + :kbd:`W`
      -
      - Cut from the beginning of the previous word into the clipboard
    * - :kbd:`Ctrl` + :kbd:`Y`
      -
      - Paste the clipboard contents
    * - :kbd:`Tab`
      - :kbd:`Enter`
      - Accept a completion suggestion
    * - :kbd:`Ctrl` + :kbd:`_`
      -
      - Undo a change
    * - :kbd:`Ctrl` + :kbd:`L`
      -
      - In the SQL prompt, reformat the contents of the prompt and switch
        to multi-line mode
    * - :kbd:`Ctrl` + :kbd:`O`
      -
      - Save the contents of the prompt in a script file named
        :code:`saved-prompt` and, if available, open the file in
        the :ref:`configured external editor <config_external_editor>`
    * - :kbd:`Ctrl` + :kbd:`S`
      -
      - In multi-line mode, switch to search mode.  If already in search
        mode, find the next occurrence.
    * - :kbd:`Ctrl` + :kbd:`R`
      -
      - In search mode, search for the previous occurrence.  Otherwise,
        search history using current contents of the prompt

Customizing
-----------

You can customize the behavior of hotkeys by defining your own keymaps.
Consult the :ref:`Keymaps<keymaps>` configuration section for more information.
