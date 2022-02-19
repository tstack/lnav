
.. _cli:

Command Line Interface
======================

The following options can be used when starting **lnav**.  There are not
many flags because the majority of the functionality is accessed using
the :option:`-c` option to execute :ref:`commands<commands>` or
:ref:`SQL queries<sql-ext>`.

Options
-------

.. option:: -h

   Print these command-line options and exit.

.. option:: -H

   Start lnav and switch to the help view.

.. option:: -C

   Check the given files against the configuration, report any errors, and
   exit.  This option can be helpful for validating that a log format is
   well-formed.

.. option:: -c <command>

   Execute the given lnav command, SQL query, or lnav script.  The
   argument must be prefixed with the character used to enter the prompt
   to distinguish between the different types (i.e. ':', ';', '|').
   This option can be given multiple times.

.. option:: -f <path>

   Execute the given command file.  This option can be given multiple times.

.. option:: -I <path>

   Add a configuration directory.

.. option:: -i

   Install the format files in the :file:`.lnav/formats/` directory.
   Individual files will be installed in the :file:`installed`
   directory and git repositories will be cloned with a directory
   name based on their repository URI.

.. option:: -u

   Update formats installed from git repositories.

.. option:: -d <path>

   Write debug messages to the given file.

.. option:: -n

   Run without the curses UI (headless mode).

.. option:: -N

   Do not open the default syslog file if no files are given.

.. option:: -r

   Recursively load files from the given base directories.

.. option:: -t

   Prepend timestamps to the lines of data being read in on the standard input.

.. option:: -w <path>

   Write the contents of the standard input to this file.

.. option:: -V

   Print the version of lnav.

.. option:: -q

   Do not print the log messages after executing all of the commands.


Environment Variables
---------------------

.. envvar:: XDG_CONFIG_HOME

   If this variable is set, lnav will use this directory to store its
   configuration in a sub-directory named :file:`lnav`.

.. envvar:: HOME

   If :envvar:`XDG_CONFIG_HOME` is not set, lnav will use this directory
   to store its configuration in a sub-directory named :file:`.lnav`.

.. envvar:: APPDATA

   On Windows, lnav will use this directory instead of HOME
   to store its configuration in a sub-directory named :file:`.lnav`.

.. envvar:: TZ

   The timezone setting is used in some log formats to convert UTC timestamps
   to the local timezone.


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
