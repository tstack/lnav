.. _faq:

Frequently Asked Questions
==========================

Q: How can I copy & paste without decorations?
----------------------------------------------

:Answer: There are a couple ways to do this:

  * Use the :ref:`bookmark<hotkeys_bookmarks>` hotkeys to mark lines and then
    press :kbd:`c` to copy to the local system keyboard.  The system clipboard
    is accessed using commands like :code:`pbcopy` and :code:`xclip`.  See the
    :ref:`tuning` section for more details.

    If a system clipboard is not available,
    the `OSC 52 <https://www.reddit.com/r/vim/comments/k1ydpn/a_guide_on_how_to_copy_text_from_anywhere/>`_
    terminal escape sequence will be tried.  If your terminal supports this
    escape sequence, the selected text will be copied to the clipboard, even
    if you are on an SSH connection.

  * Press :kbd:`CTRL` + :kbd:`l` to temporarily switch to "lo-fi"
    mode where the contents of the current view are printed to the terminal.
    This option is useful when you are logged into a remote host.


Q: How can I force a format for a file?
---------------------------------------

:Answer: The log format for a file is automatically detected and cannot be
  forced.

:Solution: First, you need to get an idea of why the file is not being
  detected by the expected format using one of the following methods:

  * Use the :ref:`format test<format_test_cli>` management command to check
    the file against the format.  This command will give some details about
    why the file is not matching the format.  For example, the following
    command will check the file :code:`my_access.log` against the
    :code:`access_log` format:

    .. prompt:: bash

        lnav -m format access_log test my_access.log

  * Add some of the log file lines to the :ref:`sample<format_sample>`
    array and then startup lnav to get a detailed explanation of where the
    format patterns are not matching the sample lines.

  After you get an idea of why the regular expressions are not matching,
  you can update the existing regexes or add a new one.  For formats
  where you are not the author, you can create a :ref:`patch<patch_format>`.

:Details: The first lines of the file are matched against the
  :ref:`regular expressions defined in the format definitions<format_regex>`.
  The order of the formats is automatically determined so that more specific
  formats are tried before more generic ones.  Therefore, if the expected
  format is not being chosen for a file, then it means the regular expressions
  defined by that format are not matching the first few lines of the file.

  See :ref:`format_order` for more information.


Q: How can I search backwards, like pressing :kbd:`?` in less?
--------------------------------------------------------------

:Answer: Searches in **lnav** runs in the background and do not block input
  waiting to find the first hit.  While the search prompt is open, pressing
  :kbd:`CTRL` + :kbd:`j` will jump to the previous hit that was found.  A
  preview panel is also opened that shows the hits that have been found so
  far.

  After pressing :kbd:`Enter` at the search prompt, the view will jump to
  the first hit that was found.  Then, you can press :kbd:`n` to move to
  the next search hit and :kbd:`N` to move to the previous one.  If you
  would like to add a hotkey for jumping to the previous hit by default,
  enter the following configuration command:

  .. code-block:: lnav

     :config /ui/keymap-defs/default/x3f/command :prompt --alt search ?


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

Q: How can I search for an exact word?
--------------------------------------

:Solution: Surround the word to search for with :code:`\b`.

:Details: The :code:`\b` means "word break" and matches a position where a
  "word" ends.  That is, right before a space character, punctuation, etc,
  or at EOL.
