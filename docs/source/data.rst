
.. _data-ext:

Extracting Data
===============

**Note**: This feature is still in **BETA**, you should expect bugs and
incompatible changes in the future.

Log messages contain a good deal of useful data, but it's not always easy to get
at.  The log parser built into **lnav** is able to extract data as described by
:ref:`log-formats` as well as discovering data in plain text messages. This data
can then be queried and processed using the SQLite front-end that is also
incorporated into **lnav**.  As an example, the following Syslog message from
:cmd:`sudo` can be processed to extract several key/value pairs::

    Jul 31 11:42:26 Example-MacBook-Pro.local sudo[87024]:  testuser : TTY=ttys004 ; PWD=/Users/testuser/github/lbuild ; USER=root ; COMMAND=/usr/bin/make install

The data that can be extracted by the parser is viewable directly in **lnav**
by pressing the 'p' key.  The results will be shown in an overlay like the
following::

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

Notice that the parser has detected pairs of the form '<key>=<value>'.  The data
parser will also look for pairs separated by a colon.  If there are no clearly
demarcated pairs, then the parser will extract anything that looks like data
values and assign them keys of the form 'col_N'.  For example, two data values,
an IPv4 address and a symbol, will be extracted from the following log
messsage::

    Apr 29 08:13:43 sample-centos5 avahi-daemon[2467]: Registering new address record for 10.1.10.62 on eth0.

Since there are no keys for the values in the message, the parser will assign
'col_0' for the IP address and 'col_1' for the symbol, as seen here::

    Current Time: 2013-04-29T08:13:43.000  Original Time: 2013-04-29T08:13:43.000  Offset: +0.000
    Known message fields:
    ├ log_hostname = sample-centos5
    ├ log_procname = avahi-daemon
    ├ log_pid      = 2467
    Discovered message fields:
    ├ col_0        = 10.1.10.62
    └ col_1        = eth0

Now that you have an idea of how the parser works, you can begin to perform
queries on the data that is being extracted.  The SQLite database engine is
embedded into **lnav** and its `Virtual Table
<http://www.sqlite.org/vtab.html>`_ mechanism is used to provide a means to
process this log data.  Each log format has its own table that can be used to
access all of the loaded messages that are in that format.  For accessing log
message content that is more free-form, like the examples given here, the
**logline** table can be used. The **logline** table is recreated for each
query and is based on the format and pairs discovered in the log message at
the top of the display.

Queries can be performed by pressing the semi-colon (;) key in **lnav**.  After
pressing the key, the overlay showing any known or discovered fields will be
displayed to give you an idea of what data is available.  The query can be any
`SQL query <http://sqlite.org/lang.html>`_ supported by SQLite.  To make
analysis easier, **lnav** includes many extra functions for processing strings,
paths, and IP addresses.  See :ref:`sql-ext` for more information.

As an example, the simplest query to perform initially would be a "select all",
like so::

    select * from logline

When this query is run against the second example log message given above, the
following results are received::

    log_line log_part         log_time        log_idle_msecs log_level  log_hostname  log_procname log_pid           col_0          col_1

         292 p.0      2013-04-11T16:42:51.000              0 info      localhost      avahi-daemon     2480    fe80::a00:27ff:fe98:7f6e eth0  
         293 p.0      2013-04-11T16:42:51.000              0 info      localhost      avahi-daemon     2480    10.0.2.15                eth0  
         330 p.0      2013-04-11T16:47:02.000              0 info      localhost      avahi-daemon     2480    fe80::a00:27ff:fe98:7f6e eth0  
         336 p.0      2013-04-11T16:47:02.000              0 info      localhost      avahi-daemon     2480    10.1.10.75               eth0  
         343 p.0      2013-04-11T16:47:02.000              0 info      localhost      avahi-daemon     2480    10.1.10.75               eth0  
         370 p.0      2013-04-11T16:59:39.000              0 info      localhost      avahi-daemon     2480    10.1.10.75               eth0  
         377 p.0      2013-04-11T16:59:39.000              0 info      localhost      avahi-daemon     2480    10.1.10.75               eth0  
         382 p.0      2013-04-11T16:59:41.000              0 info      localhost      avahi-daemon     2480    fe80::a00:27ff:fe98:7f6e eth0  
         401 p.0      2013-04-11T17:20:45.000              0 info      localhost      avahi-daemon     4247    fe80::a00:27ff:fe98:7f6e eth0  
         402 p.0      2013-04-11T17:20:45.000              0 info      localhost      avahi-daemon     4247    10.1.10.75               eth0  
    
         735 p.0      2013-04-11T17:41:46.000              0 info      sample-centos5 avahi-daemon     2465    fe80::a00:27ff:fe98:7f6e eth0  
         736 p.0      2013-04-11T17:41:46.000              0 info      sample-centos5 avahi-daemon     2465    10.1.10.75               eth0  
         781 p.0      2013-04-12T03:32:30.000              0 info      sample-centos5 avahi-daemon     2465    10.1.10.64               eth0  
         788 p.0      2013-04-12T03:32:30.000              0 info      sample-centos5 avahi-daemon     2465    10.1.10.64               eth0  
        1166 p.0      2013-04-25T10:56:00.000              0 info      sample-centos5 avahi-daemon     2467    fe80::a00:27ff:fe98:7f6e eth0  
        1167 p.0      2013-04-25T10:56:00.000              0 info      sample-centos5 avahi-daemon     2467    10.1.10.111              eth0  
        1246 p.0      2013-04-26T06:06:25.000              0 info      sample-centos5 avahi-daemon     2467    10.1.10.49               eth0  
        1253 p.0      2013-04-26T06:06:25.000              0 info      sample-centos5 avahi-daemon     2467    10.1.10.49               eth0  
        1454 p.0      2013-04-28T06:53:55.000              0 info      sample-centos5 avahi-daemon     2467    10.1.10.103              eth0  
        1461 p.0      2013-04-28T06:53:55.000              0 info      sample-centos5 avahi-daemon     2467    10.1.10.103              eth0  
    
        1497 p.0      2013-04-29T08:13:43.000              0 info      sample-centos5 avahi-daemon     2467    10.1.10.62               eth0  
        1504 p.0      2013-04-29T08:13:43.000              0 info      sample-centos5 avahi-daemon     2467    10.1.10.62               eth0  

Note that **lnav** is not returning results for all messages that are in this
syslog file.  Rather, it searches for messages that match the format for the
given line and returns only those messages in results.  In this case, that
format is "Registering new address record for <IP> on <symbol>", which
corresponds to the parts of the message that were not recognized as data.

More sophisticated queries can be done, of course.  For example, to find out the
frequency of IP addresses mentioned in these messages, you can run::

    SELECT col_0,count(*) FROM logline GROUP BY col_0

The results for this query are::

              col_0          count(*)

    10.0.2.15                       1 
    10.1.10.49                      2 
    10.1.10.62                      2 
    10.1.10.64                      2 
    10.1.10.75                      6 
    10.1.10.103                     2 
    10.1.10.111                     1 
    fe80::a00:27ff:fe98:7f6e        6 

Since this type of query is fairly common, **lnav** includes a "summarize"
command that will compute the frequencies of identifiers as well as min, max,
average, median, and standard deviation for number columns.  In this case, you
can run the following to compute the frequencies and return an ordered set of
results.

    :summarize col_0


Recognized Data Types
---------------------

When searching for data to extract from log messages, **lnav** looks for the
following set of patterns:


Strings
  Single and double-quoted strings.  Example: "The quick brown fox."

URLs
  URLs that contain the '://' separator.  Example: http://example.com

Paths
  File system paths.  Examples: /path/to/file, ./relative/path

MAC Address
  Ethernet MAC addresses.  Example: c4:2c:03:0e:e4:4a

Hex Dumps
  A colon-separated string of hex numbers.  Example: e8:06:88:ff

Date/Time
  Date and time stamps of the form "YYYY-mm-DD" and "HH:MM:SS".

IP Addresses
  IPv4 and IPv6 addresses.  Examples: 127.0.0.1, fe80::c62c:3ff:fe0e:e44a%en0

UUID
  The common formatting for 128-bit UUIDs.  Example:
  0E305E39-F1E9-4DE4-B10B-5829E5DF54D0

Version Numbers
  Dot-separated version numbers.  Example: 3.7.17

Numbers
  Numbers in base ten, hex, and octal formats.  Examples: 1234, 0xbeef, 0777

E-Mail Address
  Strings that look close to an e-mail address.  Example: gary@example.com

Constants
  Common constants in languages, like: true, false, null, None.

Symbols
  Words that follow the common conventions for symbols in programming
  languages.  For example, containing all capital letters, or separated
  by colons.  Example: SOME_CONSTANT_VALUE, namespace::value
