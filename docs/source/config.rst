
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
  using the :code:`-i` flag (e.g. :code:`$ lnav -i /path/to/config.json`).
* :file:`configs/*/*.json` -- Other directories that contain :file:`*.json`
  files will be loaded on startup.

.. note::

  Log format definitions are stored separately in the :file:`~/.lnav/formats`
  directly.  See the :ref:`Log Formats<log_formats>` chapter for more
  information.


Options
-------

The following configuration options can be used to customize **lnav** to
your liking.  The options can be changed using the :code:`:config` command.

.. jsonschema:: ../../src/internals/config-v1.schema.json#/properties/ui/properties/keymap

.. jsonschema:: ../../src/internals/config-v1.schema.json#/properties/ui/properties/theme

.. jsonschema:: ../../src/internals/config-v1.schema.json#/properties/ui/properties/clock-format

.. jsonschema:: ../../src/internals/config-v1.schema.json#/properties/ui/properties/dim-text

.. jsonschema:: ../../src/internals/config-v1.schema.json#/properties/ui/properties/default-colors


.. _themes:

Theme Definitions
-----------------

User interface themes are also defined through the JSON configuration files.

.. jsonschema:: ../../src/internals/config-v1.schema.json#/properties/ui/properties/theme-defs/patternProperties/([\w\-]+)/properties/vars

.. jsonschema:: ../../src/internals/config-v1.schema.json#/properties/ui/properties/theme-defs/patternProperties/([\w\-]+)/properties/styles

.. jsonschema:: ../../src/internals/config-v1.schema.json#/definitions/style


.. _keymaps:

Keymap Definitions
------------------

Keymaps in **lnav** map a key sequence to a command to execute.

.. jsonschema:: ../../src/internals/config-v1.schema.json#/properties/ui/properties/keymap-defs/patternProperties/([\w\-]+)
