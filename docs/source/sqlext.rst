
.. _sql-ext:

SQLite Extensions Reference
===========================

To make it easier to analyze log data from within **lnav**, there are several
built-in extensions that provide extra functions and collators beyond those
`provided by SQLite <http://www.sqlite.org/lang_corefunc.html>`_.  The majority
of the functions are from the
`extensions-functions.c <http://www.sqlite.org/contrib>`_ file available from
the `sqlite.org <http://sqlite.org>`_ web site.

*Tip*: You can include a SQLite database file on the command-line and use
**lnav**'s interface to perform queries.  The database will be attached with
a name based on the database file name.

Commands
--------

A SQL command is an internal macro implemented by lnav.

* .schema - Open the schema view.  This view contains a dump of the schema
  for the internal tables and any tables in attached databases.

Environment
-----------

Environment variables can be accessed in queries using the usual syntax of
"$VAR_NAME".  For example, to read the value of the "USER" variable, you can
write:

    ;SELECT $USER;


Math
----

Basic mathematical functions:

* cos(n)
* sin(n)
* tan(n)
* cot(n)
* cosh(n)
* sinh(n)
* coth(n)
* acos(n)
* asin(n)
* atan(r1,r2)
* atan2(r1,r2)
* exp(n)
* log(n)
* log10(n)
* power(x,y)
* sign(n) - Return one of 3 possibilities +1,0 or -1 when the argument is
  respectively positive, 0 or negative.
* sqrt(n)
* square(n)
* ceil(n)
* floor(n)
* pi()

* degrees - Convert radians to degrees
* radians - Convert degrees to radians

Aggregate functions:

* stddev
* variance
* mode
* median
* lower_quartile
* upper_quartile

String
------

Additional string comparison and manipulation functions:

* difference(s1,s2) - Computes the number of different characters between the
  soundex value fo 2 strings.
* replicate(s,n) - Given a string (s) in the first argument and an integer (n)
  in the second returns the string that constains s contatenated n times.
* proper(s) - Ensures that the words in the given string have their first
  letter capitalized and the following letters are lower case.
* charindex(s1,s2), charindex(s1,s2,n) - Given 2 input strings (s1,s2) and an
  integer (n) searches from the nth character for the string s1. Returns the
  position where the match occured. Characters are counted from 1. 0 is
  returned when no match occurs.
* leftstr(s,n) - Given a string (s) and an integer (n) returns the n leftmost
  (UTF-8) characters if the string has a length<=n or is NULL this function is
  NOP.
* rightstr(s,n) - Given a string (s) and an integer (n) returns the n rightmost
  (UTF-8) characters if the string has a length<=n or is NULL this function is
  NOP
* reverse(s) - Given a string returns the same string but with the characters
  in reverse order.
* padl(s,n) - Given an input string (s) and an integer (n) adds spaces at the
  beginning of (s) until it has a length of n characters.  When s has a length
  >=n it's a NOP. padl(NULL) = NULL
* padr(s,n) - Given an input string (s) and an integer (n) appends spaces at
  the end of s until it has a length of n characters. When s has a length >=n
  it's a NOP. padr(NULL) = NULL
* padc(s,n) - Given an input string (s) and an integer (n) appends spaces at
  the end of s and adds spaces at the begining of s until it has a length of n
  characters.  Tries to add has many characters at the left as at the right.
  When s has a length >=n it's a NOP. padc(NULL) = NULL
* strfilter(s1,s2) - Given 2 string (s1,s2) returns the string s1 with the
  characters NOT in s2 removed assumes strings are UTF-8 encoded.
* regexp(re,s) - Return 1 if the regular expression 're' matches the given
  string.
* regexp_replace(str, re, repl) - Replace the portions of the given string
  that match the regular expression with the replacement string.  **NOTE**:
  The arguments for the string and the regular expression in this function are
  reversed from the plain regexp() function.  This is to be somewhat compatible
  with functions in other database implementations.
* startswith(s1,prefix) - Given a string and prefix, return 1 if the string
  starts with the given prefix.
* endswith(s1,suffix) - Given a string and suffix, return 1 if the string ends
  with the given suffix.

File Paths
----------

File path manipulation functions:

* basename(s) - Return the file name part of a path.
* dirname(s) - Return the directory part of a path.
* joinpath(s1,s2,...) - Return the arguments joined together into a path.

Networking
----------

Network information functions:

* gethostbyname - Convert a host name into an IP address.  The host name could
  not be resolved, the input value will be returned.
* gethostbyaddr - Convert an IPv4/IPv6 address into a host name.  If the
  reverse lookup fails, the input value will be returned.

JSON
----

JSON functions:

* jget(json, json_ptr) - Get the value from the JSON-encoded string in
  first argument that is referred to by the
  `JSON-Pointer <https://tools.ietf.org/html/rfc6901>`_ in the second.
* json_group_object(key0, value0, ... keyN, valueN) - An aggregate function
  that creates a JSON-encoded object from the key value pairs given as
  arguments.
* json_group_array(value0, ... valueN) - An aggregate function that creates
  a JSON-encoded array from the values given as arguments.

Time
----

Time functions:

* timeslice(t, s) - Given a time stamp (t) and a time slice (s), return a
  timestamp for the bucket of time that the timestamp falls in.  For example,
  with the timestamp "2015-03-01 11:02:00' and slice '5min' the returned value
  will be '2015-03-01 11:00:00'.  This function can be useful when trying to
  group together log messages into buckets.

Internal State
--------------

The following functions can be used to access **lnav**'s internal state:

* log_top_line() - Return the line number at the top of the log view.
* log_top_datetime() - Return the timestamp of the line at the top of the log
  view.

Collators
---------

* naturalcase - Compare strings "naturally" so that number values in the string
  are compared based on their numeric value and not their character values.
  For example, "foo10" would be considered greater than "foo2".
* naturalnocase - The same as naturalcase, but case-insensitive.
* ipaddress - Compare IPv4/IPv6 addresses.
