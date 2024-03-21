.. _usage:

Usage
=====

This chapter contains an overview of how to use **lnav**.


Basic Controls
--------------

Like most file viewers, scrolling through files can be done with the usual
:ref:`hotkeys<hotkeys>`.  For non-trivial operations, you can enter the
:ref:`command<commands>` prompt by pressing :kbd:`:`.  To analyze data in a
log file, you can enter the :ref:`SQL prompt<sql-ext>` by pressing :kbd:`;`.

.. tip::

  Check the bottom right corner of the screen for tips on hotkeys that might
  be useful in the current context.

  .. figure:: hotkey-tips.png
     :align: center

     When **lnav** is first open, it suggests using :kbd:`e` and
     :kbd:`Shift` + :kbd:`e` to jump to error messages.


Viewing Files
-------------

The files to view in **lnav** can be given on the command-line or passed to the
:ref:`:open<open>` command.  A
`glob pattern <https://en.wikipedia.org/wiki/Glob_(programming)>`_ can be given
to watch for files with a common name.  If the path is a directory, all of the
files in the directory will be opened and the directory will be monitored for
files to be added or removed from the view.  If the path is an archive or
compressed file (and lnav was built with libarchive), the archive will be
extracted to a temporary location and the files within will be loaded.  The
files that are found will be scanned to identify their file format.  Files
that match a log format will be collated by time and displayed in the LOG
view.  Plain text files can be viewed in the TEXT view, which can be accessed
by pressing :kbd:`t`.


Archive Support
^^^^^^^^^^^^^^^

.. f0:archive

If **lnav** is compiled with `libarchive <https://www.libarchive.org>`_,
any files to be opened will be examined to see if they are a supported archive
type.  If so, the contents of the archive will be extracted to the
:code:`$TMPDIR/lnav-user-${UID}-work/archives/` directory.  Once extracted, the
files within will be loaded into lnav.  To speed up opening large amounts of
files, any file that meets the following conditions will be automatically
hidden and not indexed:

* Binary files
* Plain text files that are larger than 128KB
* Duplicate log files

The unpacked files will be left in the temporary directory after exiting
**lnav** so that opening the same archive again will be faster.  Unpacked
archives that have not been accessed in the past two days will be automatically
deleted the next time **lnav** is started.


.. _remote:

Remote Files
^^^^^^^^^^^^

Files on remote machines can be viewed and tailed if you have access to the
machines via SSH.  First, make sure you can SSH into the remote machine without
any interaction by: 1) accepting the host key as known and 2) copying your
identity's public key to the :file:`.ssh/authorized_keys` file on the remote
machine.  Once the setup is complete, you can open a file on a remote host
using the same syntax as :command:`scp(1)` where the username and host are
given, followed by a colon, and then the path to the files, like so::

    [user@]host:/path/to/logs

For example, to open :file:`/var/log/syslog.log` on "host1.example.com" as the
user "dean", you would write:

.. prompt:: bash

   lnav dean@host1.example.com:/var/log/syslog.log

Remote files can also be opened using the :ref:`:open<open>` command.  Opening
a remote file in the TUI has the advantage that the file path can be
:kbd:`TAB`-completed and a preview is shown of the first few lines of the
file.

.. note::

  If lnav is installed from the `snap <https://snapcraft.io/lnav>`_, you will
  need to connect it to the
  `ssh-keys plug <https://snapcraft.io/docs/ssh-keys-interface>`_ using the
  following command:

  .. prompt:: bash

    sudo snap connect lnav:ssh-keys

.. note::

  Remote file access is implemented by transferring an
  `αcτµαlly pδrταblε εxεcµταblε <https://justine.lol/ape.html>`_ to the
  destination and invoking it.  An APE binary can run on most any x86_64
  machine and OS (i.e. MacOS, Linux, FreeBSD, Windows).  The binary is
  baked into the lnav executable itself, so there is no extra setup that
  needs to be done on the remote machine.
  
  The binary file is named ``tailer.bin.XXXXXX`` where *XXXXXX* is 6 random digits.
  The file is, under normal circumstancies, deleted immediately.

Command Output
^^^^^^^^^^^^^^

The output of commands can be captured and displayed in **lnav** using
the :ref:`:sh<sh>` command or by passing the :option:`-e` option on the
command-line.   The captured output will be displayed in the TEXT view.
The lines from stdout and stderr are recorded separately so that the
lines from stderr can be shown in the theme's "error" highlight.  The
time that the lines were received are also recorded internally so that
the "time-offset" display (enabled by pressing :kbd:`Shift` + :kbd:`T`)
can be shown and the "jump to slow-down" hotkeys (:kbd:`s` /
:kbd:`Shift` + :kbd:`S`) work.  Since the line-by-line timestamps are
recorded internally, they will not interfere with timestamps that are
in the commands output.

Docker Logs
^^^^^^^^^^^

To make it easier to view
`docker logs <https://docs.docker.com/engine/reference/commandline/logs/>`_
within **lnav**, a :code:`docker://` URL scheme is available.  Passing
the container name in the authority field will run the :code:`docker logs`
command.  If a path is added to the URL, then **lnav** will execute
:code:`docker exec <container> tail -F -n +0 /path/to/file` to try and
tail the file in the container.

Custom URL Schemes
^^^^^^^^^^^^^^^^^^

Custom URL schemes can be defined using the :ref:`/tuning/url-schemes<url_scheme>`
configuration.  By adding a scheme name to the tuning configuration along
with the name of an **lnav** handler script, you can control how the URL is
interpreted and turned into **lnav** commands.  This feature is how the
`Docker Logs`_ functionality is implemented.

Custom URLs can be passed on the command-line or to the :ref:`:open<open>`
command.  When passed on the command-line, an :code:`:open` command with the
URL is added to the list of initial commands.  When the :code:`:open` command
detects a custom URL, it checks for the definition in the configuration.
If found, it will call the associated handler script with the URL as the
first parameter.  The script can parse the URL using the :ref:`parse_url`
SQL function, if needed.  The script should then execute whatever commands
it needs to open the destination for viewing in **lnav**.  For example,
the docker URL handler uses the :ref:`:sh<sh>` command to run
:code:`docker logs` with the container.

Using as a PAGER
^^^^^^^^^^^^^^^^

Setting **lnav** as your :envvar:`PAGER` can have some advantages, like
basic syntax highlighting and discovering sections in a document.  For
example, when viewing a man page, the current section is displayed in
the breadcrumb bar and you can jump to a section with the
:ref:`:goto<goto>` command.

You will probably want to pass the :option:`-q` option to suppress the
message showing the path to the captured input.

.. prompt:: bash

   export PAGER="lnav -q"

Searching
---------

Any log messages that are loaded into **lnav** are indexed by time and log
level (e.g. error, warning) to make searching quick and easy with
:ref:`hotkeys<hotkeys>`.  For example, pressing :kbd:`e` will jump to the
next error in the file and pressing :kbd:`Shift` + :kbd:`e` will jump to
the previous error.  Plain text searches can be done by pressing :kbd:`/`
to enter the search prompt.  A regular expression can be entered into the
prompt to start a search through the current view.


.. _filtering:

Filtering
---------

To reduce the amount of noise in a log file, **lnav** can hide log messages
that match certain criteria.  The following sub-sections explain ways to go
about that.


Regular Expression Match
^^^^^^^^^^^^^^^^^^^^^^^^

If there are log messages that you are not interested in, you can do a
"filter out" to hide messages that match a pattern.  A filter can be created
using the interactive editor, the :ref:`:filter-out<filter_out>` command, or
by doing an :code:`INSERT` into the
:ref:`lnav_view_filters<table_lnav_view_filters>` table.

If there are log messages that you are only interested in, you can do a
"filter in" to only show messages that match a pattern.  The filter can be
created using the interactive editor, the :ref:`:filter-in<filter_in>` command,
or by doing an :code:`INSERT` into the
:ref:`lnav_view_filters<table_lnav_view_filters>` table.


SQLite Expression
^^^^^^^^^^^^^^^^^

Complex filtering can be done by passing a SQLite expression to the
:ref:`:filter-expr<filter_expr>` command.  The expression will be executed for
every log message and if it returns true, the line will be shown in the log
view.


Time
^^^^

To limit log messages to a given time frame, the
:ref:`:hide-lines-before<hide_lines_before>` and
:ref:`:hide-lines-after<hide_lines_after>` commands can be used to specify
the beginning and end of the time frame.


Log level
^^^^^^^^^

To hide messages below a certain log level, you can use the
:ref:`:set-min-log-level<set_min_log_level>` command.

.. _search_tables:

Search Tables
-------------

Search tables allow you to access arbitrary data in log messages through
SQLite virtual tables.  If there is some data in a log message that you can
match with a regular expression, you can create a search-table that matches
that data and any capture groups will be plumbed through as columns in the
search table.

Creating a search table can be done interactively using the
:ref:`:create-search-table<create_search_table>` command or by adding it to
a :ref:`log format definition<log_formats>`.  The main difference between
the two is that tables defined as part of a format will only search messages
from log files with that format and the tables will include log message
columns defined in that format.  Whereas a table created with the command
will search messages from all different formats and no format-specific
columns will be included in the table.

.. _taking_notes:

Taking Notes
------------

As you are looking through logs, you might find that you want to leave some
notes of your findings.  **lnav** can help here by saving information in
the session without needing to modify the actual log files.  Thus, when
you re-open the files in lnav, the notes will be restored.  The following
types of information can be saved:

:tags: Log messages can be tagged with the :ref:`:tag<tag>` command as a
  simple way to leave a descriptive mark.  The tags attached to a
  message will be shown underneath the message.  You can press
  :kbd:`u` and :kbd:`Shift` + :kbd:`u` to jump to the next/previous
  marked line.  A regular search will also match tags.

:comments: Free-form text can be attached to a log message with the
  :ref:`:comment<comment>` command.  The comment will be shown
  underneath the message. If the text contains markdown syntax,
  it will be rendered to the best of the terminal's ability.
  You can press :kbd:`u` and :kbd:`Shift` + :kbd:`u` to jump to the
  next/previous marked line.  A regular search will also match the
  comment text.

:partitions: The log view can be partitioned to provide some context
  about where you are in a collection of logs.  For example, in logs
  for a test run, partitions could be created with the name for each
  test.  The current partition is shown in the breadcrumb bar and
  prefixed by the "⊑" symbol.  You can select the partition breadcrumb
  to jump to another partition.  Pressing :kbd:`{` and :kbd:`}` will
  jump to the next/previous partition.

Accessing notes through the SQLite interface
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The note taking functionality in lnav can also be accessed through the
log tables exposed through SQLite.  The majority of the columns in a log
table are read-only since they are backed by the log files themselves.
However, the following columns can be changed by an :code:`UPDATE` statement:

* **log_part** - The "partition" the log message belongs to.  This column can
  also be changed by the :ref:`:partition-name<partition_name>` command.
* **log_mark** - Indicates whether the line has been bookmarked.
* **log_comment** - A free-form text field for storing commentary.  This
  column can also be changed by the :ref:`:comment<comment>` command.
* **log_tags** - A JSON list of tags associated with the log message.  This
  column can also be changed by the :ref:`:tag<tag>` command.

While these columns can be updated by through other means, using the SQL
interface allows you to make changes automatically and en masse.  For example,
to bookmark all lines that have the text "something interesting" in the log
message body, you can execute:

.. code-block:: custsqlite

   ;UPDATE all_logs SET log_mark = 1 WHERE log_body LIKE '%something interesting%'

As a more advanced example of the power afforded by SQL and **lnav**'s virtual
tables, we will tag log messages where the IP address bound by dhclient has
changed.  For example, if dhclient reports "bound to 10.0.0.1" initially and
then reports "bound to 10.0.0.2", we want to tag only the messages where the
IP address was different from the previous message.  While this can be done
with a single SQL statement [#]_, we will break things down into a few steps for
this example.  First, we will use the :ref:`:create-search-table<create_search_table>`
command to match the dhclient message and extract the IP address:

.. code-block:: lnav

   :create-search-table dhclient_ip bound to (?<ip>[^ ]+)

The above command will create a new table named :code:`dhclient_ip` with the
standard log columns and an :code:`ip` column that contains the IP address.
Next, we will create a view over the :code:`dhclient_ip` table that returns
the log message line number, the IP address from the current row and the IP
address from the previous row:

.. code-block:: custsqlite

   ;CREATE VIEW IF NOT EXISTS dhclient_ip_changes AS SELECT log_line, ip, lag(ip) OVER (ORDER BY log_line) AS prev_ip FROM dhclient_ip

Finally, the following :code:`UPDATE` statement will concatenate the tag
"#ipchanged" onto the :code:`log_tags` column for any rows in the view where
the current IP is different from the previous IP:

.. code-block:: custsqlite

   ;UPDATE syslog_log SET log_tags = json_concat(log_tags, '#ipchanged') WHERE log_line IN (SELECT log_line FROM dhclient_ip_changes WHERE ip != prev_ip)

Since the above can be a lot to type out interactively, you can put these
commands into a :ref:`script<scripts>` and execute that script with the
:kbd:`\|` hotkey.

.. [#] The expression :code:`regexp_match('bound to ([^ ]+)', log_body) as ip`
   can be used to extract the IP address from the log message body.

Sharing Sessions With Others
----------------------------

After setting up filters, bookmarks, and making notes, you might want to share
your work with others.  If they have access to the same log files, you can
use the :ref:`:export-session-to<export_session_to>` command to write an
executable **lnav** script that will recreate the current session state.  The
script contains various SQL statements and **lnav** commands that capture the
current state.  So, you should feel free to modify the script or use it as a
reference to learn about more advanced uses of lnav.

The script will capture the file paths that were explicitly specified and
not the files that were actually opened.  For example, if you specified
"/var/log" on the command line, the script will include
:code:`:open /var/log/*` and not an individual open for each file in that
directory.

Also, in order to support archives of log files, lnav will try to find the
directory where the archive was unpacked and use that as the base for the
:code:`:open` command.  Currently, this is done by searching for the top
"README" file in the directory hierarchy containing the files [1]_.  The
consumer of the session script can then set the :code:`LOG_DIR_0` (or 1, 2,
...) environment variable to change where the log files will be loaded from.

.. [1] It is assumed a log archive would have a descriptive README file.
   Other heuristics may be added in the future.
