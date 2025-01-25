.. _cli:

Command Line Interface
======================

There are two command-line interfaces provided by **lnav**, one for viewing
files and one for managing **lnav**'s configuration.  The file viewing mode is
the default and is all that most people will need.  The management mode can
be useful for those that are developing log file formats and is activated by
passing the :option:`-m` option as the first argument.

File Viewing Mode
-----------------

The following options can be used when starting **lnav**.  There are not
many flags because the majority of the functionality is accessed using
the :option:`-c` option to execute :ref:`commands<commands>` or
:ref:`SQL queries<sql-ext>`.

Options
^^^^^^^

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
   to distinguish between the different types (i.e. :code:`:`, :code:`;`,
   :code:`|`, :code:`/`).  This option can be given multiple times.

.. option:: -f <path>

   Execute the given command file.  This option can be given multiple times.

.. option:: -e <command-line>

   Execute the given shell command-line and display its output.  This is
   equivalent to executing the :code:`:sh` command and passing the
   :option:`-N` flag. This option can be given multiple times.

.. option:: -I <path>

   Add a configuration directory.

.. option:: -i

   Install the given files in the lnav configuration directories.
   Format files, SQL, and lnav scripts will be installed in the
   :file:`formats/installed`.  Configuration files will be installed
   in the :file:`configs/installed` directory.  Git repository URIs
   will be cloned with a directory name based on their repository URI.

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

.. option:: -V

   Print the version of lnav.

.. option:: -v

   Print extra information during operations.

.. option:: -q

   Do not print informational messages.


.. _management_cli:

Management Mode (v0.11.0+)
--------------------------

The management CLI mode provides functionality for query **lnav**'s log
format definitions.

Options
^^^^^^^

.. option:: -m

   Switch to management mode.  This must be the first option passed on the
   command-line.

.. option:: -I <path>

   Add a configuration directory.

Subcommands
^^^^^^^^^^^

.. option:: config get

   Print out the current configuration as JSON on the standard output.

.. option:: config blame

   Print out the configuration options as JSON-Pointers and the
   file/line-number where the configuration is sourced from.

.. option:: config file-options <path>

   Print out the options that will be applied to the given file.  The
   options are stored in the :file:`file-options.json` file in the
   **lnav** configuration directory.  The only option available at
   the moment is the timezone to be used for log message timestamps
   that do not include a zone.  The timezone for a file can be set
   using the :ref:`:set-file-timezone<set_file_timezone>` command
   and cleared with the :ref:`:clear-file-timezone<clear_file_timezone>`
   command.

.. option:: format <format-name> get

   Print information about the given log format.

.. option:: format <format-name> source

   Print the name of the first file that contained this log format
   definition.

.. option:: format <format-name> regex <regex-name> push

   Push a log format regular expression to regex101.com .

.. option:: format <format-name> regex <regex-name> pull

   Pull changes to a regex that was previously pushed to regex101.com .

.. _format_test_cli:
.. option:: format <format-name> test <path>

   Test this format against the given file.

.. option:: piper clean

   Remove all of the files that stored data that was piped into **lnav**.

.. option:: piper list

   List all of the data that was piped into **lnav** from oldest to newest.
   The listing will show the creation time, the URL you can use to reopen
   the data, and a description of the data.  Passing the :option:`-v`
   option will print out additional metadata that was captured, such as
   the current working directory of **lnav** and the environment variables.

.. option:: regex101 import <regex101-url> <format-name> [<regex-name>]

   Convert a regex101.com entry into a skeleton log format file.

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

   The timezone setting is used in some log formats to convert timestamps
   with a timezone to the local timezone.


Examples
--------

To load and follow the system syslog file:

.. prompt:: bash

   lnav

To load all of the files in :file:`/var/log`:

.. prompt:: bash

   lnav /var/log

To watch the output of make:

.. prompt:: bash

   lnav -e 'make -j4'
