
.. _Cookbook:

Cookbook
========

This chapter contains recipes for common tasks that can be done in **lnav**.
These recipes can be used as a starting point for your own needs after some
adaptation.


Log Analysis
------------

To count the number of times a client IP shows up in the loaded web access
logs:

.. code-block:: custsqlite

  ;SELECT c_ip, count(*) as hits FROM access_log GROUP BY c_ip ORDER BY hits DESC
