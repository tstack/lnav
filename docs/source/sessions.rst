
.. _sessions:

Sessions
========

Session information is stored automatically for the set of files that were
passed in on the command-line and reloaded the next time **lnav** is executed.
The information currently stored is:

* Position within the files being viewed.
* Active searches for each view.
* :ref:`Log filters<filtering>`.
* :ref:`Highlights<highlight>`.
* :ref:`Hidden files<hide_file>`.
* :ref:`Hidden fields<hide_fields>`.

Bookmarks and log-time adjustments are stored separately on a per-file basis.
Note that the bookmarks are associated with files based on the content of the
first line of the file so that they are preserved even if the file has been
moved from its current location.

Session data is stored in the :file:`~/.lnav` directory.
