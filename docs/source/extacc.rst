.. _ExternalAccess:

External Access (v0.14.0+)
==========================

The "External Access" feature opens a local HTTP port that can be used to
interact with an lnav instance outside of the TUI.  This feature can be
enabled with the :ref:`external_access` command.  Once the port is open,
HTTP requests can be sent to access static files, execute commands, or
poll for changes.  When the external port is open, a globe icon (🌐) is
displayed in the top-right corner.  Clicking that icon will open a URL
in a browser and log you into the server.  The `:external-access-login`
command can also be used to login.

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

* | :code:`POST /api/exec`
  | :code:`Content-Type: text/x-lnav-script`

  Execute an lnav :ref:`script<scripts>` and receive the resulting output.
* | :code:`POST /api/poll`
  | :code:`Content-Type: application/json`

  Perform a long-poll of lnav's TUI state.  The first request to this API
  should be a :code:`null` to get the current state.  The response contains
  the following fields:

  * :code:`next_input` - Subsequent calls should send this object so the
    server knows when there has been a state-change with respect to this
    client.  Currently, the :code:`view_states/log_selection` field is
    the only stable field and refers to the focused message in the LOG
    view.
  * :code:`background_tasks` - A list of background task progress updates.

Test Harness
^^^^^^^^^^^^

The landing page for the server contains a test harness to exercise these
API endpoints.
