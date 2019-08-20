
.. _cli:

Command Line Interface
======================

-h  Print these command-line options and exit.
-H  Start lnav and switch to the help view.
-C  Check the configuration for any errors and exit.
-c  Execute the given command.  This option can be given multiple times.
-f  Execute the given command file.  This option can be given multiple times.
-I path  Add a configuration directory.
-i  Install the format files in the ``.lnav/formats/`` directory.
    Individual files will be installed in the ``installed``
    directory and git repositories will be cloned with a directory
    name based on their repository URI.
-u  Update formats installed from git repositories.
-d file  Write debug messages to the given file.
-n  Run without the curses UI (headless mode).
-r  Recursively load files from the given base directories.
-t  Prepend timestamps to the lines of data being read in on the standard
    input.
-w path  Write the contents of the standard input to this file.
-V  Print the version of lnav
