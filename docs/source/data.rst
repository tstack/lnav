
Extracting Data
===============

**Note**: This feature is still in **BETA**, you should expect bugs and
incompatible changes in the future.

Log messages contain a good deal of useful data, but it's not always easy to
get at.  The log parser built into **lnav** is able to extract data as
described by log formats as well as discovering data in plain text messages.
This data can then be queried and processed using the SQLite front-end that is
also incorporated into **lnav**.  As an example, the following Syslog message
from :cmd:`sudo` will be parsed and several 

    Jul 31 11:42:26 Example-MacBook-Pro.local sudo[87024]:  testuser : TTY=ttys004 ; PWD=/Users/testuser/github/lbuild ; USER=root ; COMMAND=/usr/bin/make install



    Current Time: 2013-07-31T11:42:26.000  Original Time: 2013-07-31T11:42:26.000  Offset: +0.000
    Known message fields:
    ├ log_hostname = Example-MacBook-Pro.local
    ├ log_procname = sudo
    ├ log_pid      = 87024
    Discovered message fields:
    ├ col_0        = testuser
    ├ TTY          = ttys004
    ├ PWD          = /Users/testuser/github/lbuild
    ├ USER         = root
    └ COMMAND      = /usr/bin/make install
