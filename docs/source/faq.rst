
.. _faq:

Frequently Asked Questions
==========================

Q: How can I copy & paste without decorations?
----------------------------------------------

:Answer: There are a few ways to do this:

  * Use the :ref:`bookmark<hotkeys_bookmarks>` hotkeys to mark lines and then
    press :kbd:`c` to copy to the local system keyboard.

  * Press :kbd:`CTRL` + :kbd:`l` to temporarily switch to "lo-fi"
    mode where the contents of the current view are printed to the terminal.
    This option is useful when you are logged into a remote host.


Q: How can I force a format for a file?
---------------------------------------

:Answer: The log format for a file is automatically detected and cannot be
  forced.

:Solution: Add some of the log file lines to the :ref:`sample<format_sample>`
  array and then startup lnav to get a detailed explanation of where the format
  patterns are not matching the sample lines.

:Details: The first lines of the file are matched against the
  :ref:`regular expressions defined in the format definitions<format_regex>`.
  The order of the formats is automatically determined so that more specific
  formats are tried before more generic ones.  Therefore, if the expected
  format is not being chosen for a file, then it means the regular expressions
  defined by that format are not matching the first few lines of the file.

  See :ref:`format_order` for more information.

Q: Why isn't my log file highlighted correctly?
-----------------------------------------------

TBD

Q: Why isn't a file being displayed?
------------------------------------

:Answer: Plaintext files are displayed separately from log files in the TEXT
  view.

:Solution: Press the :kbd:`t` key to switch to the text view.  Or, open the
  files configuration panel by pressing :kbd:`TAB` to cycle through the
  panels, and then press :kbd:`/` to search for the file you're interested in.
  If the file is a log, a new :ref:`log format<log_formats>` will need to be
  created or an existing one modified.

:Details: If a file being monitored by lnav does not match a known log file
  format, it is treated as plaintext and will be displayed in the TEXT view.
