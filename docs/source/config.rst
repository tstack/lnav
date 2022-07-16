
.. _Configuration:

Configuration
=============

The configuration for **lnav** is stored in the following JSON files in
:file:`~/.lnav`:

* :file:`config.json` -- Contains local customizations that were done using the
  :code:`:config` command.
* :file:`configs/default/*.json` -- The default configuration files that are
  built into lnav are written to this directory with :file:`.sample` appended.
  Removing the :file:`.sample` extension and editing the file will allow you to
  do basic customizations.
* :file:`configs/installed/*.json` -- Contains configuration files installed
  using the :option:`-i` flag (e.g. :code:`$ lnav -i /path/to/config.json`).
* :file:`configs/*/*.json` -- Other directories that contain :file:`*.json`
  files will be loaded on startup.  This structure is convenient for installing
  **lnav** configurations, like from a git repository.

A valid **lnav** configuration file must contain an object with the
:code:`$schema` property, like so:

.. code-block:: json

   {
       "$schema": "https://lnav.org/schemas/config-v1.schema.json"
   }

.. note::

  Log format definitions are stored separately in the :file:`~/.lnav/formats`
  directly.  See the :ref:`Log Formats<log_formats>` chapter for more
  information.


Options
-------

The following configuration options can be used to customize the **lnav** UI to
your liking.  The options can be changed using the :code:`:config` command.

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/ui/properties/keymap

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/ui/properties/theme

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/ui/properties/clock-format

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/ui/properties/dim-text

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/ui/properties/default-colors


.. _themes:

Theme Definitions
-----------------

User Interface themes are defined in a JSON configuration file.  A theme is
made up of the style definitions for different types of text in the UI.  A
:ref:`definition<theme_style>` can include the foreground/background colors
and the bold/underline attributes.  The style definitions are broken up into
multiple categories for the sake of organization.  To make it easier to write
a definition, a theme can define variables that can be referenced as color
values.

Variables
^^^^^^^^^

The :code:`vars` object in a theme definition contains the mapping of variable
names to color values.  These variables can be referenced in style definitions
by prefixing them with a dollar-sign (e.g. :code:`$black`).  The following
variables can also be defined to control the values of the ANSI colors that
are log messages or plain text:

.. csv-table:: ANSI colors
   :header: "Variable Name", "ANSI Escape"

   "black", "ESC[30m"
   "red", "ESC[31m"
   "green", "ESC[32m"
   "yellow", "ESC[33m"
   "blue", "ESC[34m"
   "magenta", "ESC[35m"
   "cyan", "ESC[36m"
   "white", "ESC[37m"

Specifying Colors
^^^^^^^^^^^^^^^^^

Colors can be specified using hexadecimal notation by starting with a hash
(e.g. :code:`#aabbcc`) or using a color name as found at
http://jonasjacek.github.io/colors/.  If colors are not specified for a style,
the values from the :code:`styles/text` definition.

.. note::

  When specifying colors in hexadecimal notation, you do not need to have an
  exact match in the XTerm 256 color palette.  A best approximation will be
  picked based on the `CIEDE2000 <https://en.wikipedia.org/wiki/Color_difference#CIEDE2000>`_
  color difference algorithm.



Example
^^^^^^^

The following example sets the black/background color for text to a dark grey
using a variable and sets the foreground to an off-white.  This theme is
incomplete, but it works enough to give you an idea of how a theme is defined.
You can copy the code block, save it to a file in
:file:`~/.lnav/configs/installed/` and then activate it by executing
:code:`:config /ui/theme example` in lnav.  For a more complete theme
definition, see one of the definitions built into **lnav**, like
`monocai <https://github.com/tstack/lnav/blob/master/src/themes/monocai.json>`_.

  .. code-block:: json

    {
        "$schema": "https://lnav.org/schemas/config-v1.schema.json",
        "ui": {
            "theme-defs": {
                "example1": {
                    "vars": {
                        "black": "#2d2a2e"
                    },
                    "styles": {
                        "text": {
                            "color": "#f6f6f6",
                            "background-color": "$black"
                        }
                    }
                }
            }
        }
    }

Reference
^^^^^^^^^

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/ui/properties/theme-defs/patternProperties/([\w\-]+)/properties/vars

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/ui/properties/theme-defs/patternProperties/([\w\-]+)/properties/styles

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/ui/properties/theme-defs/patternProperties/([\w\-]+)/properties/syntax-styles

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/ui/properties/theme-defs/patternProperties/([\w\-]+)/properties/status-styles

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/ui/properties/theme-defs/patternProperties/([\w\-]+)/properties/log-level-styles

.. _theme_style:

.. jsonschema:: ../schemas/config-v1.schema.json#/definitions/style


.. _keymaps:

Keymap Definitions
------------------

Keymaps in **lnav** map a key sequence to a command to execute.  When a key is
pressed, it is converted into a hex-encoded string that is looked up in the
keymap.  The :code:`command` value associated with the entry in the keymap is
then executed.  Note that the "command" can be an **lnav**
:ref:`command<commands>`, a :ref:`SQL statement/query<sql-ext>`, or an
**lnav** script.  If an :code:`alt-msg` value is included in the entry, the
bottom-right section of the UI will be updated with the help text.

.. note::

  Not all functionality is available via commands or SQL at the moment.  Also,
  some hotkeys are not implemented via keymaps.

Key Sequence Encoding
^^^^^^^^^^^^^^^^^^^^^

Key presses are converted into a hex-encoded string that is used to lookup an
entry in the keymap.  Each byte of the keypress value is formatted as an
:code:`x` followed by the hex-encoding in lowercase.  For example, the encoding
for the £ key would be :code:`xc2xa3`.  To make it easier to discover the
encoding for unassigned keys, **lnav** will print in the command prompt the
:code:`:config` command and
`JSON-Pointer <https://tools.ietf.org/html/rfc6901>`_ for assigning a command
to the key.

.. figure:: key-encoding-prompt.png
  :align: center

  Screenshot of the command prompt when an unassigned key is pressed.

.. note::

  Since **lnav** is a terminal application, it can only receive keypresses that
  can be represented as characters or escape sequences.  For example, it cannot
  handle the press of a modifier key.

Reference
^^^^^^^^^

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/ui/properties/keymap-defs/patternProperties/([\w\-]+)


Log Handling
------------

The handling of logs is largely determined by the
:ref:`log file formats<log_formats>`, this section covers options that are not
specific to a particular format.

Watch Expressions (v0.11.0+)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Watch expressions can be used to fire an event when a log message matches a
condition.  You can then install a listener for these events and trigger an
action to be performed.  For example, to automate filtering based on
identifiers, a watch expression can match messages that mention the ID and then
a trigger can install a filter for that ID.  Creating a watch expression is
done by adding an entry into the :code:`/log/watch-expressions` configuration
tree.  For example, to create a watch named "dhcpdiscover" that matches
DHCPDISCOVER messages from the :code:`dhclient` daemon, you would run the
following:

.. code-block:: lnav

   :config /log/watch-expressions/dhcpdiscover/expr :log_procname = 'dhclient' AND startswith(:log_body, 'DHCPDISCOVER')

The watch expression can refer to column names in the log message by prefixing
them with a colon.  The expression is evaluated by passing the log message
fields as bound parameters and not against a table.  The easiest way to test
out an expression is with the :ref:`mark_expr` command, since it will behave
similarly.  After changing the configuration, you'll need to restart lnav
for the effect to take place.  You can then query the :code:`lnav_events`
table to see any generated
:code:`https://lnav.org/event-log-msg-detected-v1.schema.json` events from the
logs that were loaded:

.. code-block:: custsqlite

   ;SELECT * FROM lnav_events

From there, you can create a SQLite trigger on the :code:`lnav_events` table
that will examine the event contents and perform an action.  See the
:ref:`Events` section for more information on handling events.

Reference
^^^^^^^^^

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/log/properties/watch-expressions/patternProperties/([\w\-]+)

.. _tuning:

Tuning
------

The following configuration options can be used to tune the internals of
**lnav** to your liking.  The options can be changed using the :code:`:config`
command.

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/tuning/properties/archive-manager

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/tuning/properties/clipboard

.. jsonschema:: ../schemas/config-v1.schema.json#/definitions/clip-commands

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/tuning/properties/file-vtab

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/tuning/properties/logfile

.. jsonschema:: ../schemas/config-v1.schema.json#/properties/tuning/properties/remote/properties/ssh
