
.. _commands:

Commands
========

This chapter covers the commands used to control **lnav**.

.. tip::

  Note that almost all commands support TAB-completion for their arguments.
  So, if you are in doubt as to what to type for an argument, you can double-
  tap the TAB key to get suggestions.  For example, the TAB-completion for the
  :code:`filter-in` command will suggest words that are currently displayed in
  the view.

.. note:: The following commands can be disabled by setting the ``LNAVSECURE``
   environment variable before executing the **lnav** binary:

   - open
   - pipe-to
   - pipe-line-to
   - write-\*-to

   This makes it easier to run **lnav** in restricted environments without the
   risk of privilege escalation.

Reference
---------

.. include:: ../../src/internals/cmd-ref.rst
