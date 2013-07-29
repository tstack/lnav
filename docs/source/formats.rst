
.. _log-formats:

Log Formats
===========

The log formats **lnav** uses to parse a file are defined by a simple
configuration file.  Many formats are already built in to the **lnav** binary
and you can define your own using a JSON file.  When loading files, each format
is checked to see if it can parse the first few lines in the file.  Once a match
is found, that format will be considered that files format and used to parse the
remaining lines in the file.

The following log formats are built into **lnav**:

.. csv-table::
   :header: "Name", "Table Name", "Description"
   :widths: 8 5 20
   :file: format-table.csv

Modifying an Existing Format
----------------------------

Defining a New Format
---------------------

