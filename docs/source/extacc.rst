.. _ExternalAccess:

External Access (v0.14.0+)
==========================

The "External Access" feature opens a local HTTP port that can be used to
interact with an lnav instance outside of the TUI.  This feature can be
enabled with the :ref:`external_access` command.  Once the port is open,
HTTP requests can be sent to access static files, execute commands, or
poll for changes.  When the external port is open, a globe icon (🌐) is
displayed in the top-right corner.  Clicking that icon will open a URL
in a browser and log you into the server.  The :ref:`external_access_login`
command can also be used to login.

.. note:: The server only binds to :code:`localhost`, so it will not
    be accessible over the network.  If you need to access lnav
    remotely, consider using SSH forwarding.

Authentication
--------------

All requests to lnav's external access server are authenticated.  A request
must have one of the following:

* An :code:`X-Api-Key` header with the Base64-encoded value of the API-key that
  was passed to the :code:`:external-access` command.  This header should be
  used for automations.
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

  Get the version of the lnav instance.

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
