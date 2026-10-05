.. _ExternalAccess:

External Access (v0.14.0+)
==========================

The "External Access" feature opens a local HTTP port that can be used to
interact with an lnav instance outside of the TUI.  The server is started
automatically when lnav runs with the TUI (but not in headless or secure mode)
on a port picked by the OS and with a random API key.  The
:ref:`external_access` command can be used to pick the port, API key, or
instance name instead.  Once the port is open,
HTTP requests can be sent to access static files, execute commands, or
poll for changes.  When the external port is open, a globe icon (🌐) is
displayed in the top-right corner.  Clicking that icon will open a URL
in a browser and log you into the server.  The :ref:`external_access_login`
command can also be used to login.

.. note:: The server only binds to :code:`localhost`, so it will not
    be accessible over the network.  If you need to access lnav
    remotely, consider using SSH forwarding.

Discovery
---------

So that clients, like editor plugins, can find a running lnav without being
configured with a port and key, each instance with an open port writes a
discovery file to :file:`external-access/<pid>.json` in lnav's
configuration directory (usually :file:`~/.lnav`).  The directory
is only accessible by the user and the file is removed when lnav exits.  Files
left behind by instances that did not exit cleanly are removed the next time
an instance starts.  The file contains the paths given on lnav's
command-line, along with its name and current directory, to make it easier to
tell instances apart.  The contents are described by the following schema:

.. jsonschema:: ../schemas/external-access-instance-v1.schema.json#

When there are multiple instances, a client can pick the one to talk to by
matching the :code:`name` or :code:`cwd` against its own project.  The
:code:`instances list` subcommand of the
:ref:`management CLI<management_cli>` does this matching and skips files
whose process has exited.  For example, to get the URL of the instance
started in the current directory, or the closest one beneath it:

.. code-block:: bash

    lnav -m instances list --cwd . -o url | head -1

The :code:`GET /api/version` response also includes the :code:`name` and
:code:`cwd` so a client can check that it reached the instance it expected.

Authentication
--------------

All requests to lnav's external access server are authenticated.  A request
must have one of the following:

* An :code:`X-Api-Key` header with the Base64-encoded value of the API-key that
  was passed to the :code:`:external-access` command or that was written to
  the discovery file.  This header should be used for automations.
* An :code:`lnav_session_id` cookie.  This cookie will be set through the
  flow initiated by the :ref:`external_access_login` command that opens
  the :code:`/login` URL using the configured
  :ref:`external-opener<config_external_opener>`.  The :code:`/login` URL
  accepts a one-time-password query parameter and, if it matches, the session
  cookie will be created.  A single one-time-password is possible at any time
  and sessions are only valid for the current invocation of lnav.

Endpoints
---------

The following routes are available:

* | :code:`GET /api/version`

  Get the version, process ID, name, and current working directory of the
  lnav instance.

* | :code:`POST /api/exec`
  | :code:`Content-Type: text/x-lnav-script`

  Execute an lnav :ref:`script<scripts>` and receive the resulting output.
  Values can be passed to the script in headers instead of being quoted
  into its text: a :code:`X-Lnav-Var-<name>` header whose value is the
  Base64-encoded value sets the variable :code:`$<name>`.  The prefix is
  matched without regard to case; the rest of the header name is the
  variable name as sent.  In SQL statements, the variable is a bound
  parameter, and in commands it expands to a single argument.

* | :code:`POST /api/poll`
  | :code:`Content-Type: application/json`

  Perform a long-poll of lnav's TUI state.  The first request to this API
  should be a :code:`null` to get the current state.  The response contains
  the following fields:

  * :code:`next_input` - Subsequent calls should send this object so the
    server knows when there has been a state-change with respect to this
    client.  Currently, the :code:`view_states/log_selection` field is
    the only stable field and refers to the focused message in the LOG
    view.  The :code:`log_index_seq` field is the :code:`log_index.seq`
    from the previous response.
  * :code:`background_tasks` - A list of background task progress updates.
  * :code:`log_index` - What changed in the LOG view's index since the
    :code:`log_index_seq` that was sent, so a client can re-query only the
    rows that changed (e.g. with :code:`log_line >= from_row` against
    :code:`all_logs`).  It contains:

    * :code:`seq` - The current change sequence number.  A script can read
      it in the same snapshot as its queries with
      :code:`jget(view_details, '/index-seq')` from the :code:`log` row of
      :code:`lnav_views`, so it can send that instead and skip changes its
      query already saw.
    * :code:`row_count` - The number of rows in the LOG view.
    * :code:`reset` - True when the changes since the given sequence
      number are not available (the first poll, or too many changes
      since then); treat every row as changed.
    * :code:`changes` - A list of changes, oldest first.  Each has a
      :code:`seq`, the index :code:`generation`, :code:`from_row`, and
      :code:`row_count`: every row from :code:`from_row` onward was added
      or replaced, leaving :code:`row_count` rows.  Newly appended lines
      have a :code:`from_row` equal to the previous row count.  A rebuild
      or filter change starts earlier, possibly at zero.
  * :code:`open_requests` - Files for an editor client to open; see below.

  An editor, like an IDE plugin, can ask to open the files that lnav would
  otherwise pass to an :ref:`external editor<config_external_editor>`
  command, which can be much faster for IDEs that are slow to launch.  To
  do so, it adds these fields to the object it sends:

  * :code:`client_id` - A string that identifies the client across polls.
  * :code:`editor_roots` - The directories, as lnav sees them, that the
    client can open files under.

  When lnav needs to open a file under one of those directories, it adds an
  entry to the :code:`open_requests` of the client with the closest root.
  Each entry has an :code:`id`, the :code:`path`, and the :code:`line` and
  :code:`col` to go to, which are one-based.  The :code:`last_event_id` in
  :code:`next_input` is the highest :code:`id` sent; a request is sent again
  until a poll comes back with that :code:`last_event_id`.  If no client
  has a matching root, the external editor command is used instead.  If
  the client does not receive the request within two seconds, the request
  is dropped.

* | :code:`GET /assets/css/theme.css`

  Get the CSS rules for the classes used in the :code:`'html'` output of the
  :code:`lnav_view_lines()` SQL function.  The rules are generated from the
  current theme, so the HTML looks like the lines on the screen.  The pages
  rendered from Markdown, including apps, already link to this stylesheet.
  The same rules are returned by the :code:`lnav_theme_css()` SQL function.

Apps
----

To support custom user-interfaces on top of lnav, "apps" can be installed
that are reachable via the external access server.  These browser-based apps
can provide a rich interface for executing queries and presenting
their results.  For example, a dashboard for :code:`access_log`
files can display multiple charts for interesting request statistics.
Apps are reachable from the landing page for the external access server
or you can open one directly by passing its ID to the
:ref:`external_access_login` command.  An app ID has the form
:code:`<publisher>/<app-name>` (e.g. :code:`lnav/api-test`).

Creating an app can be done using the :ref:`management CLI<management_cli>`,
like so:

.. code-block:: bash

    lnav -m apps create mydash

This command creates a directory in the :file:`configs` directory and
populates it with the necessary configuration file and a sample
:file:`index.md` file.

Test Harness
^^^^^^^^^^^^

The :code:`lnav/api-test` app is included by default as a demonstration of the
external access server APIs.

Reference
^^^^^^^^^

The following are the configuration properties necessary to define an app:

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/apps
