.. _Events:

Events (v0.11.0+)
=================

The events mechanism allows **lnav** to be automated based on events that
occur during processing.  For example, filters could be added only when a
particular log file format is detected instead of always installing them.
Events are published through the :ref:`lnav_events<table_lnav_events>` SQLite
table.  Reacting to events can be done by creating a SQLite trigger on the
table and inspecting the content of the event.

Trigger Example
---------------

The following is an example of a trigger that adds an out filter when a
syslog file is loaded.  You can copy the code into an :file:`.sql` file and
install it by running :code:`lnav -i my_trigger.sql`.

.. code-block:: sql
    :caption: my_trigger.sql
    :linenos:

    CREATE TRIGGER IF NOT EXISTS add_format_specific_filters
      AFTER INSERT ON lnav_events WHEN
        -- Check the event type
        jget(NEW.content, '/$schema') =
          'https://lnav.org/event-file-format-detected-v1.schema.json' AND
        -- Only create the filter when a given format is seen
        jget(NEW.content, '/format') = 'syslog_log' AND
        -- Don't create the filter if it's already there
        NOT EXISTS (
          SELECT 1 FROM lnav_view_filters WHERE pattern = 'noisy message')
    BEGIN
    INSERT INTO lnav_view_filters (view_name, enabled, type, pattern) VALUES
        ('log', 1, 'OUT', 'noisy message');
    END;

.. _event_reference:

Reference
---------

The following tables describe the schema of the event JSON objects.

.. jsonschema:: ../schemas/event-file-open-v1.schema.json#
    :lift_description:

.. jsonschema:: ../schemas/event-file-format-detected-v1.schema.json#
    :lift_description:

.. jsonschema:: ../schemas/event-log-msg-detected-v1.schema.json#
    :lift_description:

.. jsonschema:: ../schemas/event-session-loaded-v1.schema.json#
    :lift_description:
