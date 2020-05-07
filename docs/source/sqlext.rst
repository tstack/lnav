
.. _sql-ext:

SQLite Interface
================

To make it easier to analyze log data from within **lnav**, there are several
built-in extensions that provide extra functions and collators beyond those
`provided by SQLite <http://www.sqlite.org/lang_corefunc.html>`_.  The majority
of the functions are from the
`extensions-functions.c <http://www.sqlite.org/contrib>`_ file available from
the `sqlite.org <http://sqlite.org>`_ web site.

.. tip:: You can include a SQLite database file on the command-line and use
  **lnav**'s interface to perform queries.  The database will be attached with
  a name based on the database file name.

Commands
--------

A SQL command is an internal macro implemented by lnav.

* .schema - Open the schema view.  This view contains a dump of the schema
  for the internal tables and any tables in attached databases.

Variables
---------

The following variables are available in SQL statements:

* $LINES - The number of lines in the terminal window.
* $COLS - The number of columns in the terminal window.

Environment
-----------

Environment variables can be accessed in queries using the usual syntax of
"$VAR_NAME".  For example, to read the value of the "USER" variable, you can
write:

.. code-block:: sql

    SELECT $USER

.. _collators:

Collators
---------

* naturalcase - Compare strings "naturally" so that number values in the string
  are compared based on their numeric value and not their character values.
  For example, "foo10" would be considered greater than "foo2".
* naturalnocase - The same as naturalcase, but case-insensitive.
* ipaddress - Compare IPv4/IPv6 addresses.

Reference
---------

The following is a reference of the SQL syntax and functions that are available:

.. include:: ../../src/internals/sql-ref.rst
