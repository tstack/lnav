
.. _cli:

Command Line Interface
======================

The following options can be used when starting **lnav**.  There are not
many flags because the majority of the functionality is accessed using
the :code:`-c` option to execute :ref:`commands<commands>` or
:ref:`SQL queries<sql-ext>`.

-h  Print these command-line options and exit.
-H  Start lnav and switch to the help view.
-C  Check the given files against the configuration, report any errors, and
    exit.  This option can be helpful for validating that a log format is
    well-formed.
-c cmd  Execute the given lnav command, SQL query, or lnav script.  The
   argument must be prefixed with the character used to enter the prompt
   to distinguish between the different types (i.e. ':', ';', '|').
   This option can be given multiple times.
-f path  Execute the given command file.  This option can be given multiple times.
-I path  Add a configuration directory.
-i  Install the format files in the :file:`.lnav/formats/` directory.
    Individual files will be installed in the :file:`installed`
    directory and git repositories will be cloned with a directory
    name based on their repository URI.
-u  Update formats installed from git repositories.
-d path  Write debug messages to the given file.
-n  Run without the curses UI (headless mode).
-r  Recursively load files from the given base directories.
-t  Prepend timestamps to the lines of data being read in on the standard
    input.
-w path  Write the contents of the standard input to this file.
-V  Print the version of lnav.
-q  Do not print the log messages after executing all of the commands.

Examples
--------

  To load and follow the system syslog file:

  .. prompt:: bash

    lnav

  To load all of the files in :file:`/var/log`:

  .. prompt:: bash

    lnav /var/log

  To watch the output of make with timestamps prepended:

  .. prompt:: bash

    make 2>&1 | lnav -t
