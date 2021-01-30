
.. include:: kbd.rst

.. _Cookbook:

Cookbook
========

This chapter contains recipes for common tasks that can be done in **lnav**.
These recipes can be used as a starting point for your own needs after some
adaptation.


Log Analysis
------------

Most log analysis within **lnav** is done through the :ref:`sql-ext`.  The
following examples should give you some ideas to start leveraging this
functionality.  One thing to keep in mind is that if a query gets to be too
large or multiple statements need to be executed, you can create a
:code:`.lnav` script that contains the statements and execute it using the
|ks| | |ke| command prompt.

* To count the number of times a client IP shows up in the loaded web access
  logs:

  .. code-block:: custsqlite

    ;SELECT c_ip, count(*) as hits FROM access_log GROUP BY c_ip ORDER BY hits DESC
