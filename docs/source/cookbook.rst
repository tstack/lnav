
.. _Cookbook:

Cookbook
========

This chapter contains recipes for common tasks that can be done in **lnav**.
These recipes can be used as a starting point for your own needs after some
adaptation.


Log Formats
-----------

TBD

Defining a New Format
^^^^^^^^^^^^^^^^^^^^^

TBD

Log Analysis
------------

Most log analysis within **lnav** is done through the :ref:`sql-ext`.  The
following examples should give you some ideas to start leveraging this
functionality.  One thing to keep in mind is that if a query gets to be too
large or multiple statements need to be executed, you can create a
:code:`.lnav` script that contains the statements and execute it using the
:kbd:`\|` command prompt.

Count client IPs in web access logs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To count the occurrences of an IP in web access logs and order the results
from highest to lowest:

  .. code-block:: custsqlite

    ;SELECT c_ip, count(*) as hits FROM access_log GROUP BY c_ip ORDER BY hits DESC


Show only lines where a numeric field is in a range
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The :ref:`:filter-expr<filter_expr>` command can be used to filter web access
logs to only show lines where the number of bytes transferred to the client is
between 10,000 and 40,000 bytes like so:

  .. code-block:: custsqlite

    :filter-expr :sc_bytes BETWEEN 10000 AND 40000
