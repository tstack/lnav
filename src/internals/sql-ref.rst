
.. _infix_between_and:

expr *\[NOT\]* BETWEEN *low* AND *hi*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Test if an expression is between two values.

  **Parameters**
    * **low\*** --- The low point
    * **hi\*** --- The high point

  **Examples**
    To check if 3 is between 5 and 10:

    .. code-block::  custsqlite

      ;SELECT 3 BETWEEN 5 AND 10
      0

    To check if 10 is between 5 and 10:

    .. code-block::  custsqlite

      ;SELECT 10 BETWEEN 5 AND 10
      1


----


.. _attach:

ATTACH DATABASE *filename* AS *schema-name*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Attach a database file to the current connection.

  **Parameters**
    * **filename\*** --- The path to the database file.
    * **schema-name\*** --- The prefix for tables in this database.

  **Examples**
    To attach the database file '/tmp/customers.db' with the name customers:

    .. code-block::  custsqlite

      ;ATTACH DATABASE '/tmp/customers.db' AS customers


----


.. _create_view:

CREATE *\[TEMP\]* VIEW  *\[IF NOT EXISTS\]* *\[schema-name.\]* *view-name* AS *select-stmt*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Assign a name to a SELECT statement

  **Parameters**
    * **IF NOT EXISTS** --- Do not create the view if it already exists
    * **schema-name.** --- The database to create the view in
    * **view-name\*** --- The name of the view
    * **select-stmt\*** --- The SELECT statement the view represents


----


.. _create_table:

CREATE *\[TEMP\]* TABLE  *\[IF NOT EXISTS\]* *\[schema-name.\]* *table-name* AS *select-stmt*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Create a table


----


.. _with_recursive:

WITH RECURSIVE  *cte-table-name* AS *select-stmt*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Create a temporary view that exists only for the duration of a SQL statement.

  **Parameters**
    * **cte-table-name\*** --- The name for the temporary table.
    * **select-stmt\*** --- The SELECT statement used to populate the temporary table.


----


.. _cast:

CAST(*expr* AS *type-name*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Convert the value of the given expression to a different storage class specified by type-name.

  **Parameters**
    * **expr\*** --- The value to convert.
    * **type-name\*** --- The name of the type to convert to.

  **Examples**
    To cast the value 1.23 as an integer:

    .. code-block::  custsqlite

      ;SELECT CAST(1.23 AS INTEGER)
      1


----


.. _case_end:

CASE *\[base-expr\]* WHEN *cmp-expr* ELSE *\[else-expr\]* END 
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Evaluate a series of expressions in order until one evaluates to true and then return it's result.  Similar to an IF-THEN-ELSE construct in other languages.

  **Parameters**
    * **base-expr** --- The base expression that is used for comparison in the branches
    * **cmp-expr** --- The expression to test if this branch should be taken
    * **else-expr** --- The result of this CASE if no branches matched.

  **Examples**
    To evaluate the number one and return the string 'one':

    .. code-block::  custsqlite

      ;SELECT CASE 1 WHEN 0 THEN 'zero' WHEN 1 THEN 'one' END
      one


----


.. _infix_collate:

expr COLLATE *collation-name*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Assign a collating sequence to the expression.

  **Parameters**
    * **collation-name\*** --- The name of the collator.

  **Examples**
    To change the collation method for string comparisons:

    .. code-block::  custsqlite

      ;SELECT ('a2' < 'a10'), ('a2' < 'a10' COLLATE naturalnocase)
      ('a2' < 'a10') ('a2' < 'a10' COLLATE naturalnocase) 
                   0                                    1 


----


.. _detach:

DETACH DATABASE *schema-name*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Detach a database from the current connection.

  **Parameters**
    * **schema-name\*** --- The prefix for tables in this database.

  **Examples**
    To detach the database named 'customers':

    .. code-block::  custsqlite

      ;DETACH DATABASE customers


----


.. _delete:

DELETE FROM *table-name* WHERE *\[cond\]*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Delete rows from a table

  **Parameters**
    * **table-name\*** --- The name of the table
    * **cond** --- The conditions used to delete the rows.


----


.. _drop_index:

DROP INDEX  *\[IF EXISTS\]* *\[schema-name.\]* *index-name*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Drop an index


----


.. _drop_table:

DROP TABLE  *\[IF EXISTS\]* *\[schema-name.\]* *table-name*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Drop a table


----


.. _drop_view:

DROP VIEW  *\[IF EXISTS\]* *\[schema-name.\]* *view-name*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Drop a view


----


.. _drop_trigger:

DROP TRIGGER  *\[IF EXISTS\]* *\[schema-name.\]* *trigger-name*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Drop a trigger


----


.. _infix_glob:

expr *\[NOT\]* GLOB *pattern*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Match an expression against a glob pattern.

  **Parameters**
    * **pattern\*** --- The glob pattern to match against.

  **Examples**
    To check if a value matches the pattern '*.log':

    .. code-block::  custsqlite

      ;SELECT 'foobar.log' GLOB '*.log'
      1


----


.. _infix_like:

expr *\[NOT\]* LIKE *pattern*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Match an expression against a text pattern.

  **Parameters**
    * **pattern\*** --- The pattern to match against.

  **Examples**
    To check if a value matches the pattern 'Hello, %!':

    .. code-block::  custsqlite

      ;SELECT 'Hello, World!' LIKE 'Hello, %!'
      1


----


.. _infix_regexp:

expr *\[NOT\]* REGEXP *pattern*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Match an expression against a regular expression.

  **Parameters**
    * **pattern\*** --- The regular expression to match against.

  **Examples**
    To check if a value matches the pattern 'file-\d+':

    .. code-block::  custsqlite

      ;SELECT 'file-23' REGEXP 'file-\d+'
      1


----


.. _select:

SELECT *result-column* FROM *table* WHERE *\[cond\]* GROUP BY *grouping-expr* ORDER BY *ordering-term* LIMIT *limit-expr*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Query the database and return zero or more rows of data.

  **Parameters**
    * **table** --- The table(s) to query for data
    * **cond** --- The conditions used to select the rows to return.
    * **grouping-expr** --- The expression to use when grouping rows.
    * **ordering-term** --- The values to use when ordering the result set.
    * **limit-expr** --- The maximum number of rows to return

  **Examples**
    To select all of the columns from the table 'syslog_log':

    .. code-block::  custsqlite

      ;SELECT * FROM syslog_log


----


.. _insert_into:

INSERT INTO  *\[schema-name.\]* *table-name* *column-name* VALUES *expr*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Insert rows into a table

  **Examples**
    To insert the pair containing 'MSG' and 'HELLO, WORLD!' into the 'environ' table:

    .. code-block::  custsqlite

      ;INSERT INTO environ VALUES ('MSG', 'HELLO, WORLD!')


----


.. _over:

OVER(*\[base-window-name\]* PARTITION BY *expr* ORDER BY *expr*, *\[frame-spec\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Executes the preceding function over a window

  **Parameters**
    * **base-window-name** --- The name of the window definition
    * **expr** --- The values to use for partitioning
    * **expr** --- The values used to order the rows in the window
    * **frame-spec** --- Determines which output rows are read by an aggregate window function


----


.. _over:

OVER *window-name*
^^^^^^^^^^^^^^^^^^

  Executes the preceding function over a window

  **Parameters**
    * **window-name\*** --- The name of the window definition


----


.. _update_set:

UPDATE *table* SET  *column-name* WHERE *\[cond\]*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Modify a subset of values in zero or more rows of the given table

  **Parameters**
    * **table\*** --- The table to update
    * **column-name** --- The columns in the table to update.
    * **cond** --- The condition used to determine whether a row should be updated.

  **Examples**
    To mark the syslog message at line 40:

    .. code-block::  custsqlite

      ;UPDATE syslog_log SET log_mark = 1 WHERE log_line = 40


----


.. _abs:

abs(*x*)
^^^^^^^^

  Return the absolute value of the argument

  **Parameters**
    * **x\*** --- The number to convert

  **Examples**
    To get the absolute value of -1:

    .. code-block::  custsqlite

      ;SELECT abs(-1)
      1

  **See Also**
    :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _acos:

acos(*num*)
^^^^^^^^^^^

  Returns the arccosine of a number, in radians

  **Parameters**
    * **num\*** --- A cosine value that is between -1 and 1

  **Examples**
    To get the arccosine of 0.2:

    .. code-block::  custsqlite

      ;SELECT acos(0.2)
      1.36943840600457

  **See Also**
    :ref:`abs`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _acosh:

acosh(*num*)
^^^^^^^^^^^^

  Returns the hyperbolic arccosine of a number

  **Parameters**
    * **num\*** --- A number that is one or more

  **Examples**
    To get the hyperbolic arccosine of 1.2:

    .. code-block::  custsqlite

      ;SELECT acosh(1.2)
      0.622362503714779

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _asin:

asin(*num*)
^^^^^^^^^^^

  Returns the arcsine of a number, in radians

  **Parameters**
    * **num\*** --- A sine value that is between -1 and 1

  **Examples**
    To get the arcsine of 0.2:

    .. code-block::  custsqlite

      ;SELECT asin(0.2)
      0.201357920790331

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _asinh:

asinh(*num*)
^^^^^^^^^^^^

  Returns the hyperbolic arcsine of a number

  **Parameters**
    * **num\*** --- The number

  **Examples**
    To get the hyperbolic arcsine of 0.2:

    .. code-block::  custsqlite

      ;SELECT asinh(0.2)
      0.198690110349241

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _atan:

atan(*num*)
^^^^^^^^^^^

  Returns the arctangent of a number, in radians

  **Parameters**
    * **num\*** --- The number

  **Examples**
    To get the arctangent of 0.2:

    .. code-block::  custsqlite

      ;SELECT atan(0.2)
      0.197395559849881

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _atan2:

atan2(*y*, *x*)
^^^^^^^^^^^^^^^

  Returns the angle in the plane between the positive X axis and the ray from (0, 0) to the point (x, y)

  **Parameters**
    * **y\*** --- The y coordinate of the point
    * **x\*** --- The x coordinate of the point

  **Examples**
    To get the angle, in degrees, for the point at (5, 5):

    .. code-block::  custsqlite

      ;SELECT degrees(atan2(5, 5))
      45.0

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _atanh:

atanh(*num*)
^^^^^^^^^^^^

  Returns the hyperbolic arctangent of a number

  **Parameters**
    * **num\*** --- The number

  **Examples**
    To get the hyperbolic arctangent of 0.2:

    .. code-block::  custsqlite

      ;SELECT atanh(0.2)
      0.202732554054082

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _atn2:

atn2(*y*, *x*)
^^^^^^^^^^^^^^

  Returns the angle in the plane between the positive X axis and the ray from (0, 0) to the point (x, y)

  **Parameters**
    * **y\*** --- The y coordinate of the point
    * **x\*** --- The x coordinate of the point

  **Examples**
    To get the angle, in degrees, for the point at (5, 5):

    .. code-block::  custsqlite

      ;SELECT degrees(atn2(5, 5))
      45.0

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _avg:

avg(*X*)
^^^^^^^^

  Returns the average value of all non-NULL numbers within a group.

  **Parameters**
    * **X\*** --- The value to compute the average of.

  **Examples**
    To get the average of the column 'ex_duration' from the table 'lnav_example_log':

    .. code-block::  custsqlite

      ;SELECT avg(ex_duration) FROM lnav_example_log
      4.25

    To get the average of the column 'ex_duration' from the table 'lnav_example_log' when grouped by 'ex_procname':

    .. code-block::  custsqlite

      ;SELECT ex_procname, avg(ex_duration) FROM lnav_example_log GROUP BY ex_procname
      ex_procname avg(ex_duration) 
      gw                       5.0 
      hw                       2.0 

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _basename:

basename(*path*)
^^^^^^^^^^^^^^^^

  Extract the base portion of a pathname.

  **Parameters**
    * **path\*** --- The path

  **Examples**
    To get the base of a plain file name:

    .. code-block::  custsqlite

      ;SELECT basename('foobar')
      foobar

    To get the base of a path:

    .. code-block::  custsqlite

      ;SELECT basename('foo/bar')
      bar

    To get the base of a directory:

    .. code-block::  custsqlite

      ;SELECT basename('foo/bar/')
      bar

    To get the base of an empty string:

    .. code-block::  custsqlite

      ;SELECT basename('')
      .

    To get the base of a Windows path:

    .. code-block::  custsqlite

      ;SELECT basename('foo\bar')
      bar

    To get the base of the root directory:

    .. code-block::  custsqlite

      ;SELECT basename('/')
      /

  **See Also**
    :ref:`dirname`, :ref:`joinpath`, :ref:`readlink`, :ref:`realpath`

----


.. _ceil:

ceil(*num*)
^^^^^^^^^^^

  Returns the smallest integer that is not less than the argument

  **Parameters**
    * **num\*** --- The number to raise to the ceiling

  **Examples**
    To get the ceiling of 1.23:

    .. code-block::  custsqlite

      ;SELECT ceil(1.23)
      2

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _changes:

changes()
^^^^^^^^^

  The number of database rows that were changed, inserted, or deleted by the most recent statement.


----


.. _char:

char(*X*)
^^^^^^^^^

  Returns a string composed of characters having the given unicode code point values

  **Parameters**
    * **X** --- The unicode code point values

  **Examples**
    To get a string with the code points 0x48 and 0x49:

    .. code-block::  custsqlite

      ;SELECT char(0x48, 0x49)
      HI

  **See Also**
    :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _charindex:

charindex(*needle*, *haystack*, *\[start\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Finds the first occurrence of the needle within the haystack and returns the number of prior characters plus 1, or 0 if Y is nowhere found within X

  **Parameters**
    * **needle\*** --- The string to look for in the haystack
    * **haystack\*** --- The string to search within
    * **start** --- The one-based index within the haystack to start the search

  **Examples**
    To search for the string 'abc' within 'abcabc' and starting at position 2:

    .. code-block::  custsqlite

      ;SELECT charindex('abc', 'abcabc', 2)
      4

    To search for the string 'abc' within 'abcdef' and starting at position 2:

    .. code-block::  custsqlite

      ;SELECT charindex('abc', 'abcdef', 2)
      0

  **See Also**
    :ref:`char`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _coalesce:

coalesce(*X*, *Y*)
^^^^^^^^^^^^^^^^^^

  Returns a copy of its first non-NULL argument, or NULL if all arguments are NULL

  **Parameters**
    * **X\*** --- A value to check for NULL-ness
    * **Y** --- A value to check for NULL-ness

  **Examples**
    To get the first non-null value from three parameters:

    .. code-block::  custsqlite

      ;SELECT coalesce(null, 0, null)
      0


----


.. _count:

count(*X*)
^^^^^^^^^^

  If the argument is '*', the total number of rows in the group is returned.  Otherwise, the number of times the argument is non-NULL.

  **Parameters**
    * **X\*** --- The value to count.

  **Examples**
    To get the count of the non-NULL rows of 'lnav_example_log':

    .. code-block::  custsqlite

      ;SELECT count(*) FROM lnav_example_log
      4

    To get the count of the non-NULL values of 'log_part' from 'lnav_example_log':

    .. code-block::  custsqlite

      ;SELECT count(log_part) FROM lnav_example_log
      2


----


.. _cume_dist:

cume_dist()
^^^^^^^^^^^

  Returns the cumulative distribution

  **See Also**
    :ref:`dense_rank`, :ref:`first_value`, :ref:`lag`, :ref:`last_value`, :ref:`lead`, :ref:`nth_value`, :ref:`ntile`, :ref:`percent_rank`, :ref:`rank`, :ref:`row_number`

----


.. _date:

date(*timestring*, *modifier*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns the date in this format: YYYY-MM-DD.

  **Parameters**
    * **timestring\*** --- The string to convert to a date.
    * **modifier** --- A transformation that is applied to the value to the left.

  **Examples**
    To get the date portion of the timestamp '2017-01-02T03:04:05':

    .. code-block::  custsqlite

      ;SELECT date('2017-01-02T03:04:05')
      2017-01-02

    To get the date portion of the timestamp '2017-01-02T03:04:05' plus one day:

    .. code-block::  custsqlite

      ;SELECT date('2017-01-02T03:04:05', '+1 day')
      2017-01-03

    To get the date portion of the epoch timestamp 1491341842:

    .. code-block::  custsqlite

      ;SELECT date(1491341842, 'unixepoch')
      2017-04-04

  **See Also**
    :ref:`datetime`, :ref:`julianday`, :ref:`strftime`, :ref:`time`, :ref:`timediff`, :ref:`timeslice`

----


.. _datetime:

datetime(*timestring*, *modifier*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns the date and time in this format: YYYY-MM-DD HH:MM:SS.

  **Parameters**
    * **timestring\*** --- The string to convert to a date with time.
    * **modifier** --- A transformation that is applied to the value to the left.

  **Examples**
    To get the date and time portion of the timestamp '2017-01-02T03:04:05':

    .. code-block::  custsqlite

      ;SELECT datetime('2017-01-02T03:04:05')
      2017-01-02 03:04:05

    To get the date and time portion of the timestamp '2017-01-02T03:04:05' plus one minute:

    .. code-block::  custsqlite

      ;SELECT datetime('2017-01-02T03:04:05', '+1 minute')
      2017-01-02 03:05:05

    To get the date and time portion of the epoch timestamp 1491341842:

    .. code-block::  custsqlite

      ;SELECT datetime(1491341842, 'unixepoch')
      2017-04-04 21:37:22

  **See Also**
    :ref:`date`, :ref:`julianday`, :ref:`strftime`, :ref:`time`, :ref:`timediff`, :ref:`timeslice`

----


.. _degrees:

degrees(*radians*)
^^^^^^^^^^^^^^^^^^

  Converts radians to degrees

  **Parameters**
    * **radians\*** --- The radians value to convert to degrees

  **Examples**
    To convert PI to degrees:

    .. code-block::  custsqlite

      ;SELECT degrees(pi())
      180.0

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _dense_rank:

dense_rank()
^^^^^^^^^^^^

  Returns the row_number() of the first peer in each group without gaps

  **See Also**
    :ref:`cume_dist`, :ref:`first_value`, :ref:`lag`, :ref:`last_value`, :ref:`lead`, :ref:`nth_value`, :ref:`ntile`, :ref:`percent_rank`, :ref:`rank`, :ref:`row_number`

----


.. _dirname:

dirname(*path*)
^^^^^^^^^^^^^^^

  Extract the directory portion of a pathname.

  **Parameters**
    * **path\*** --- The path

  **Examples**
    To get the directory of a relative file path:

    .. code-block::  custsqlite

      ;SELECT dirname('foo/bar')
      foo

    To get the directory of an absolute file path:

    .. code-block::  custsqlite

      ;SELECT dirname('/foo/bar')
      /foo

    To get the directory of a file in the root directory:

    .. code-block::  custsqlite

      ;SELECT dirname('/bar')
      /

    To get the directory of a Windows path:

    .. code-block::  custsqlite

      ;SELECT dirname('foo\bar')
      foo

    To get the directory of an empty path:

    .. code-block::  custsqlite

      ;SELECT dirname('')
      .

  **See Also**
    :ref:`basename`, :ref:`joinpath`, :ref:`readlink`, :ref:`realpath`

----


.. _endswith:

endswith(*str*, *suffix*)
^^^^^^^^^^^^^^^^^^^^^^^^^

  Test if a string ends with the given suffix

  **Parameters**
    * **str\*** --- The string to test
    * **suffix\*** --- The suffix to check in the string

  **Examples**
    To test if the string 'notbad.jpg' ends with '.jpg':

    .. code-block::  custsqlite

      ;SELECT endswith('notbad.jpg', '.jpg')
      1

    To test if the string 'notbad.png' starts with '.jpg':

    .. code-block::  custsqlite

      ;SELECT endswith('notbad.png', '.jpg')
      0

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _exp:

exp(*x*)
^^^^^^^^

  Returns the value of e raised to the power of x

  **Parameters**
    * **x\*** --- The exponent

  **Examples**
    To raise e to 2:

    .. code-block::  custsqlite

      ;SELECT exp(2)
      7.38905609893065

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _extract:

extract(*str*)
^^^^^^^^^^^^^^

  Automatically Parse and extract data from a string

  **Parameters**
    * **str\*** --- The string to parse

  **Examples**
    To extract key/value pairs from a string:

    .. code-block::  custsqlite

      ;SELECT extract('foo=1 bar=2 name="Rolo Tomassi"')
      {"foo":1,"bar":2,"name":"Rolo Tomassi"}

    To extract columnar data from a string:

    .. code-block::  custsqlite

      ;SELECT extract('1.0 abc 2.0')
      {"col_0":1.0,"col_1":2.0}

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _first_value:

first_value(*expr*)
^^^^^^^^^^^^^^^^^^^

  Returns the result of evaluating the expression against the first row in the window frame.

  **Parameters**
    * **expr\*** --- The expression to execute over the first row

  **See Also**
    :ref:`cume_dist`, :ref:`dense_rank`, :ref:`lag`, :ref:`last_value`, :ref:`lead`, :ref:`nth_value`, :ref:`ntile`, :ref:`percent_rank`, :ref:`rank`, :ref:`row_number`

----


.. _floor:

floor(*num*)
^^^^^^^^^^^^

  Returns the largest integer that is not greater than the argument

  **Parameters**
    * **num\*** --- The number to lower to the floor

  **Examples**
    To get the floor of 1.23:

    .. code-block::  custsqlite

      ;SELECT floor(1.23)
      1

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _generate_series:

generate_series(*start*, *stop*, *\[step\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  A table-valued-function that returns the whole numbers between a lower and upper bound, inclusive

  **Parameters**
    * **start\*** --- The starting point of the series
    * **stop\*** --- The stopping point of the series
    * **step** --- The increment between each value

  **Examples**
    To generate the numbers in the range [10, 14]:

    .. code-block::  custsqlite

      ;SELECT value FROM generate_series(10, 14)
      value 
         10 
         11 
         12 
         13 
         14 

    To generate every other number in the range [10, 14]:

    .. code-block::  custsqlite

      ;SELECT value FROM generate_series(10, 14, 2)
      value 
         10 
         12 
         14 

    To count down from five to 1:

    .. code-block::  custsqlite

      ;SELECT value FROM generate_series(1, 5, -1)
      value 
          5 
          4 
          3 
          2 
          1 


----


.. _gethostbyaddr:

gethostbyaddr(*hostname*)
^^^^^^^^^^^^^^^^^^^^^^^^^

  Get the hostname for the given IP address

  **Parameters**
    * **hostname\*** --- The IP address to lookup.

  **Examples**
    To get the hostname for the IP '127.0.0.1':

    .. code-block::  custsqlite

      ;SELECT gethostbyaddr('127.0.0.1')
      localhost

  **See Also**
    :ref:`gethostbyname`

----


.. _gethostbyname:

gethostbyname(*hostname*)
^^^^^^^^^^^^^^^^^^^^^^^^^

  Get the IP address for the given hostname

  **Parameters**
    * **hostname\*** --- The DNS hostname to lookup.

  **Examples**
    To get the IP address for 'localhost':

    .. code-block::  custsqlite

      ;SELECT gethostbyname('localhost')
      127.0.0.1

  **See Also**
    :ref:`gethostbyaddr`

----


.. _glob:

glob(*pattern*, *str*)
^^^^^^^^^^^^^^^^^^^^^^

  Match a string against Unix glob pattern

  **Parameters**
    * **pattern\*** --- The glob pattern
    * **str\*** --- The string to match

  **Examples**
    To test if the string 'abc' matches the glob 'a*':

    .. code-block::  custsqlite

      ;SELECT glob('a*', 'abc')
      1


----


.. _group_concat:

group_concat(*X*, *\[sep\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns a string which is the concatenation of all non-NULL values of X separated by a comma or the given separator.

  **Parameters**
    * **X\*** --- The value to concatenate.
    * **sep** --- The separator to place between the values.

  **Examples**
    To concatenate the values of the column 'ex_procname' from the table 'lnav_example_log':

    .. code-block::  custsqlite

      ;SELECT group_concat(ex_procname) FROM lnav_example_log
      hw,gw,gw,gw

    To join the values of the column 'ex_procname' using the string ', ':

    .. code-block::  custsqlite

      ;SELECT group_concat(ex_procname, ', ') FROM lnav_example_log
      hw, gw, gw, gw

    To concatenate the distinct values of the column 'ex_procname' from the table 'lnav_example_log':

    .. code-block::  custsqlite

      ;SELECT group_concat(DISTINCT ex_procname) FROM lnav_example_log
      hw,gw

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _group_spooky_hash_agg:

group_spooky_hash(*str*)
^^^^^^^^^^^^^^^^^^^^^^^^

  Compute the hash value for the given arguments

  **Parameters**
    * **str** --- The string to hash

  **Examples**
    To produce a hash of all of the values of 'column1':

    .. code-block::  custsqlite

      ;SELECT group_spooky_hash(column1) FROM (VALUES ('abc'), ('123'))
      4e7a190aead058cb123c94290f29c34a

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _hex:

hex(*X*)
^^^^^^^^

  Returns a string which is the upper-case hexadecimal rendering of the content of its argument.

  **Parameters**
    * **X\*** --- The blob to convert to hexadecimal

  **Examples**
    To get the hexadecimal rendering of the string 'abc':

    .. code-block::  custsqlite

      ;SELECT hex('abc')
      616263


----


.. _humanize_file_size:

humanize_file_size(*value*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Format the given file size as a human-friendly string

  **Parameters**
    * **value\*** --- The file size to format

  **Examples**
    To format an amount:

    .. code-block::  custsqlite

      ;SELECT humanize_file_size(10 * 1024 * 1024)
      10.0MB

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _ifnull:

ifnull(*X*, *Y*)
^^^^^^^^^^^^^^^^

  Returns a copy of its first non-NULL argument, or NULL if both arguments are NULL

  **Parameters**
    * **X\*** --- A value to check for NULL-ness
    * **Y\*** --- A value to check for NULL-ness

  **Examples**
    To get the first non-null value between null and zero:

    .. code-block::  custsqlite

      ;SELECT ifnull(null, 0)
      0


----


.. _instr:

instr(*haystack*, *needle*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Finds the first occurrence of the needle within the haystack and returns the number of prior characters plus 1, or 0 if the needle was not found

  **Parameters**
    * **haystack\*** --- The string to search within
    * **needle\*** --- The string to look for in the haystack

  **Examples**
    To test get the position of 'b' in the string 'abc':

    .. code-block::  custsqlite

      ;SELECT instr('abc', 'b')
      2

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _jget:

jget(*json*, *ptr*, *\[default\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Get the value from a JSON object using a JSON-Pointer.

  **Parameters**
    * **json\*** --- The JSON object to query.
    * **ptr\*** --- The JSON-Pointer to lookup in the object.
    * **default** --- The default value if the value was not found

  **Examples**
    To get the root of a JSON value:

    .. code-block::  custsqlite

      ;SELECT jget('1', '')
      1

    To get the property named 'b' in a JSON object:

    .. code-block::  custsqlite

      ;SELECT jget('{ "a": 1, "b": 2 }', '/b')
      2

    To get the 'msg' property and return a default if it does not exist:

    .. code-block::  custsqlite

      ;SELECT jget(null, '/msg', 'Hello')
      Hello

  **See Also**
    :ref:`json_concat`, :ref:`json_contains`, :ref:`json_group_array`, :ref:`json_group_object`

----


.. _joinpath:

joinpath(*path*)
^^^^^^^^^^^^^^^^

  Join components of a path together.

  **Parameters**
    * **path** --- One or more path components to join together.  If an argument starts with a forward or backward slash, it will be considered an absolute path and any preceding elements will be ignored.

  **Examples**
    To join a directory and file name into a relative path:

    .. code-block::  custsqlite

      ;SELECT joinpath('foo', 'bar')
      foo/bar

    To join an empty component with other names into a relative path:

    .. code-block::  custsqlite

      ;SELECT joinpath('', 'foo', 'bar')
      foo/bar

    To create an absolute path with two path components:

    .. code-block::  custsqlite

      ;SELECT joinpath('/', 'foo', 'bar')
      /foo/bar

    To create an absolute path from a path component that starts with a forward slash:

    .. code-block::  custsqlite

      ;SELECT joinpath('/', 'foo', '/bar')
      /bar

  **See Also**
    :ref:`basename`, :ref:`dirname`, :ref:`readlink`, :ref:`realpath`

----


.. _json_concat:

json_concat(*json*, *value*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns an array with the given values concatenated onto the end.  If the initial value is null, the result will be an array with the given elements.  If the initial value is an array, the result will be an array with the given values at the end.  If the initial value is not null or an array, the result will be an array with two elements: the initial value and the given value.

  **Parameters**
    * **json\*** --- The initial JSON value.
    * **value** --- The value(s) to add to the end of the array.

  **Examples**
    To append the number 4 to null:

    .. code-block::  custsqlite

      ;SELECT json_concat(NULL, 4)
      [4]

    To append 4 and 5 to the array [1, 2, 3]:

    .. code-block::  custsqlite

      ;SELECT json_concat('[1, 2, 3]', 4, 5)
      [1,2,3,4,5]

    To concatenate two arrays together:

    .. code-block::  custsqlite

      ;SELECT json_concat('[1, 2, 3]', json('[4, 5]'))
      [1,2,3,4,5]

  **See Also**
    :ref:`jget`, :ref:`json_contains`, :ref:`json_group_array`, :ref:`json_group_object`

----


.. _json_contains:

json_contains(*json*, *value*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Check if a JSON value contains the given element.

  **Parameters**
    * **json\*** --- The JSON value to query.
    * **value\*** --- The value to look for in the first argument

  **Examples**
    To test if a JSON array contains the number 4:

    .. code-block::  custsqlite

      ;SELECT json_contains('[1, 2, 3]', 4)
      0

    To test if a JSON array contains the string 'def':

    .. code-block::  custsqlite

      ;SELECT json_contains('["abc", "def"]', 'def')
      1

  **See Also**
    :ref:`jget`, :ref:`json_concat`, :ref:`json_group_array`, :ref:`json_group_object`

----


.. _json_group_array:

json_group_array(*value*)
^^^^^^^^^^^^^^^^^^^^^^^^^

  Collect the given values from a query into a JSON array

  **Parameters**
    * **value** --- The values to append to the array

  **Examples**
    To create an array from arguments:

    .. code-block::  custsqlite

      ;SELECT json_group_array('one', 2, 3.4)
      ["one",2,3.3999999999999999112]

    To create an array from a column of values:

    .. code-block::  custsqlite

      ;SELECT json_group_array(column1) FROM (VALUES (1), (2), (3))
      [1,2,3]

  **See Also**
    :ref:`jget`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_group_object`

----


.. _json_group_object:

json_group_object(*name*, *value*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Collect the given values from a query into a JSON object

  **Parameters**
    * **name\*** --- The property name for the value
    * **value** --- The value to add to the object

  **Examples**
    To create an object from arguments:

    .. code-block::  custsqlite

      ;SELECT json_group_object('a', 1, 'b', 2)
      {"a":1,"b":2}

    To create an object from a pair of columns:

    .. code-block::  custsqlite

      ;SELECT json_group_object(column1, column2) FROM (VALUES ('a', 1), ('b', 2))
      {"a":1,"b":2}

  **See Also**
    :ref:`jget`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_group_array`

----


.. _julianday:

julianday(*timestring*, *modifier*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns the number of days since noon in Greenwich on November 24, 4714 B.C.

  **Parameters**
    * **timestring\*** --- The string to convert to a date with time.
    * **modifier** --- A transformation that is applied to the value to the left.

  **Examples**
    To get the julian day from the timestamp '2017-01-02T03:04:05':

    .. code-block::  custsqlite

      ;SELECT julianday('2017-01-02T03:04:05')
      2457755.62783565

    To get the julian day from the timestamp '2017-01-02T03:04:05' plus one minute:

    .. code-block::  custsqlite

      ;SELECT julianday('2017-01-02T03:04:05', '+1 minute')
      2457755.62853009

    To get the julian day from the timestamp 1491341842:

    .. code-block::  custsqlite

      ;SELECT julianday(1491341842, 'unixepoch')
      2457848.40094907

  **See Also**
    :ref:`date`, :ref:`datetime`, :ref:`strftime`, :ref:`time`, :ref:`timediff`, :ref:`timeslice`

----


.. _lag:

lag(*expr*, *\[offset\]*, *\[default\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns the result of evaluating the expression against the previous row in the partition.

  **Parameters**
    * **expr\*** --- The expression to execute over the previous row
    * **offset** --- The offset from the current row in the partition
    * **default** --- The default value if the previous row does not exist instead of NULL

  **See Also**
    :ref:`cume_dist`, :ref:`dense_rank`, :ref:`first_value`, :ref:`last_value`, :ref:`lead`, :ref:`nth_value`, :ref:`ntile`, :ref:`percent_rank`, :ref:`rank`, :ref:`row_number`

----


.. _last_insert_rowid:

last_insert_rowid()
^^^^^^^^^^^^^^^^^^^

  Returns the ROWID of the last row insert from the database connection which invoked the function


----


.. _last_value:

last_value(*expr*)
^^^^^^^^^^^^^^^^^^

  Returns the result of evaluating the expression against the last row in the window frame.

  **Parameters**
    * **expr\*** --- The expression to execute over the last row

  **See Also**
    :ref:`cume_dist`, :ref:`dense_rank`, :ref:`first_value`, :ref:`lag`, :ref:`lead`, :ref:`nth_value`, :ref:`ntile`, :ref:`percent_rank`, :ref:`rank`, :ref:`row_number`

----


.. _lead:

lead(*expr*, *\[offset\]*, *\[default\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns the result of evaluating the expression against the next row in the partition.

  **Parameters**
    * **expr\*** --- The expression to execute over the next row
    * **offset** --- The offset from the current row in the partition
    * **default** --- The default value if the next row does not exist instead of NULL

  **See Also**
    :ref:`cume_dist`, :ref:`dense_rank`, :ref:`first_value`, :ref:`lag`, :ref:`last_value`, :ref:`nth_value`, :ref:`ntile`, :ref:`percent_rank`, :ref:`rank`, :ref:`row_number`

----


.. _leftstr:

leftstr(*str*, *N*)
^^^^^^^^^^^^^^^^^^^

  Returns the N leftmost (UTF-8) characters in the given string.

  **Parameters**
    * **str\*** --- The string to return subset.
    * **N\*** --- The number of characters from the left side of the string to return.

  **Examples**
    To get the first character of the string 'abc':

    .. code-block::  custsqlite

      ;SELECT leftstr('abc', 1)
      a

    To get the first ten characters of a string, regardless of size:

    .. code-block::  custsqlite

      ;SELECT leftstr('abc', 10)
      abc

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _length:

length(*str*)
^^^^^^^^^^^^^

  Returns the number of characters (not bytes) in the given string prior to the first NUL character

  **Parameters**
    * **str\*** --- The string to determine the length of

  **Examples**
    To get the length of the string 'abc':

    .. code-block::  custsqlite

      ;SELECT length('abc')
      3

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _like:

like(*pattern*, *str*, *\[escape\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Match a string against a pattern

  **Parameters**
    * **pattern\*** --- The pattern to match.  A percent symbol (%) will match zero or more characters and an underscore (_) will match a single character.
    * **str\*** --- The string to match
    * **escape** --- The escape character that can be used to prefix a literal percent or underscore in the pattern.

  **Examples**
    To test if the string 'aabcc' contains the letter 'b':

    .. code-block::  custsqlite

      ;SELECT like('%b%', 'aabcc')
      1

    To test if the string 'aab%' ends with 'b%':

    .. code-block::  custsqlite

      ;SELECT like('%b:%', 'aab%', ':')
      1


----


.. _likelihood:

likelihood(*value*, *probability*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Provides a hint to the query planner that the first argument is a boolean that is true with the given probability

  **Parameters**
    * **value\*** --- The boolean value to return
    * **probability\*** --- A floating point constant between 0.0 and 1.0


----


.. _likely:

likely(*value*)
^^^^^^^^^^^^^^^

  Short-hand for likelihood(X,0.9375)

  **Parameters**
    * **value\*** --- The boolean value to return


----


.. _lnav_top_file:

lnav_top_file()
^^^^^^^^^^^^^^^

  Return the name of the file that the top line in the current view came from.


----


.. _load_extension:

load_extension(*path*, *\[entry-point\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Loads SQLite extensions out of the given shared library file using the given entry point.

  **Parameters**
    * **path\*** --- The path to the shared library containing the extension.


----


.. _log:

log(*x*)
^^^^^^^^

  Returns the natural logarithm of x

  **Parameters**
    * **x\*** --- The number

  **Examples**
    To get the natual logarithm of 8:

    .. code-block::  custsqlite

      ;SELECT log(8)
      2.07944154167984

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _log10:

log10(*x*)
^^^^^^^^^^

  Returns the base-10 logarithm of X

  **Parameters**
    * **x\*** --- The number

  **Examples**
    To get the logarithm of 100:

    .. code-block::  custsqlite

      ;SELECT log10(100)
      2.0

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _log_top_datetime:

log_top_datetime()
^^^^^^^^^^^^^^^^^^

  Return the timestamp of the line at the top of the log view.


----


.. _log_top_line:

log_top_line()
^^^^^^^^^^^^^^

  Return the line number at the top of the log view.


----


.. _lower:

lower(*str*)
^^^^^^^^^^^^

  Returns a copy of the given string with all ASCII characters converted to lower case.

  **Parameters**
    * **str\*** --- The string to convert.

  **Examples**
    To lowercase the string 'AbC':

    .. code-block::  custsqlite

      ;SELECT lower('AbC')
      abc

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _ltrim:

ltrim(*str*, *\[chars\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns a string formed by removing any and all characters that appear in the second argument from the left side of the first.

  **Parameters**
    * **str\*** --- The string to trim characters from the left side
    * **chars** --- The characters to trim.  Defaults to spaces.

  **Examples**
    To trim the leading whitespace from the string '   abc':

    .. code-block::  custsqlite

      ;SELECT ltrim('   abc')
      abc

    To trim the characters 'a' or 'b' from the left side of the string 'aaaabbbc':

    .. code-block::  custsqlite

      ;SELECT ltrim('aaaabbbc', 'ab')
      c

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _max:

max(*X*)
^^^^^^^^

  Returns the argument with the maximum value, or return NULL if any argument is NULL.

  **Parameters**
    * **X** --- The numbers to find the maximum of.  If only one argument is given, this function operates as an aggregate.

  **Examples**
    To get the largest value from the parameters:

    .. code-block::  custsqlite

      ;SELECT max(2, 1, 3)
      3

    To get the largest value from an aggregate:

    .. code-block::  custsqlite

      ;SELECT max(status) FROM http_status_codes
      511

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _min:

min(*X*)
^^^^^^^^

  Returns the argument with the minimum value, or return NULL if any argument is NULL.

  **Parameters**
    * **X** --- The numbers to find the minimum of.  If only one argument is given, this function operates as an aggregate.

  **Examples**
    To get the smallest value from the parameters:

    .. code-block::  custsqlite

      ;SELECT min(2, 1, 3)
      1

    To get the smallest value from an aggregate:

    .. code-block::  custsqlite

      ;SELECT min(status) FROM http_status_codes
      100

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _nth_value:

nth_value(*expr*, *N*)
^^^^^^^^^^^^^^^^^^^^^^

  Returns the result of evaluating the expression against the nth row in the window frame.

  **Parameters**
    * **expr\*** --- The expression to execute over the nth row
    * **N\*** --- The row number

  **See Also**
    :ref:`cume_dist`, :ref:`dense_rank`, :ref:`first_value`, :ref:`lag`, :ref:`last_value`, :ref:`lead`, :ref:`ntile`, :ref:`percent_rank`, :ref:`rank`, :ref:`row_number`

----


.. _ntile:

ntile(*groups*)
^^^^^^^^^^^^^^^

  Returns the number of the group that the current row is a part of

  **Parameters**
    * **groups\*** --- The number of groups

  **See Also**
    :ref:`cume_dist`, :ref:`dense_rank`, :ref:`first_value`, :ref:`lag`, :ref:`last_value`, :ref:`lead`, :ref:`nth_value`, :ref:`percent_rank`, :ref:`rank`, :ref:`row_number`

----


.. _nullif:

nullif(*X*, *Y*)
^^^^^^^^^^^^^^^^

  Returns its first argument if the arguments are different and NULL if the arguments are the same.

  **Parameters**
    * **X\*** --- The first argument to compare.
    * **Y\*** --- The argument to compare against the first.

  **Examples**
    To test if 1 is different from 1:

    .. code-block::  custsqlite

      ;SELECT nullif(1, 1)
      <NULL>

    To test if 1 is different from 2:

    .. code-block::  custsqlite

      ;SELECT nullif(1, 2)
      1


----


.. _padc:

padc(*str*, *len*)
^^^^^^^^^^^^^^^^^^

  Pad the given string with enough spaces to make it centered within the given length

  **Parameters**
    * **str\*** --- The string to pad
    * **len\*** --- The minimum desired length of the output string

  **Examples**
    To pad the string 'abc' to a length of six characters:

    .. code-block::  custsqlite

      ;SELECT padc('abc', 6) || 'def'
       abc  def

    To pad the string 'abcdef' to a length of eight characters:

    .. code-block::  custsqlite

      ;SELECT padc('abcdef', 8) || 'ghi'
       abcdef ghi

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _padl:

padl(*str*, *len*)
^^^^^^^^^^^^^^^^^^

  Pad the given string with leading spaces until it reaches the desired length

  **Parameters**
    * **str\*** --- The string to pad
    * **len\*** --- The minimum desired length of the output string

  **Examples**
    To pad the string 'abc' to a length of six characters:

    .. code-block::  custsqlite

      ;SELECT padl('abc', 6)
         abc

    To pad the string 'abcdef' to a length of four characters:

    .. code-block::  custsqlite

      ;SELECT padl('abcdef', 4)
      abcdef

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _padr:

padr(*str*, *len*)
^^^^^^^^^^^^^^^^^^

  Pad the given string with trailing spaces until it reaches the desired length

  **Parameters**
    * **str\*** --- The string to pad
    * **len\*** --- The minimum desired length of the output string

  **Examples**
    To pad the string 'abc' to a length of six characters:

    .. code-block::  custsqlite

      ;SELECT padr('abc', 6) || 'def'
      abc   def

    To pad the string 'abcdef' to a length of four characters:

    .. code-block::  custsqlite

      ;SELECT padr('abcdef', 4) || 'ghi'
      abcdefghi

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _percent_rank:

percent_rank()
^^^^^^^^^^^^^^

  Returns (rank - 1) / (partition-rows - 1)

  **See Also**
    :ref:`cume_dist`, :ref:`dense_rank`, :ref:`first_value`, :ref:`lag`, :ref:`last_value`, :ref:`lead`, :ref:`nth_value`, :ref:`ntile`, :ref:`rank`, :ref:`row_number`

----


.. _pi:

pi()
^^^^

  Returns the value of PI

  **Examples**
    To get the value of PI:

    .. code-block::  custsqlite

      ;SELECT pi()
      3.14159265358979

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _power:

power(*base*, *exp*)
^^^^^^^^^^^^^^^^^^^^

  Returns the base to the given exponent

  **Parameters**
    * **base\*** --- The base number
    * **exp\*** --- The exponent

  **Examples**
    To raise two to the power of three:

    .. code-block::  custsqlite

      ;SELECT power(2, 3)
      8.0

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _printf:

printf(*format*, *X*)
^^^^^^^^^^^^^^^^^^^^^

  Returns a string with this functions arguments substituted into the given format.  Substitution points are specified using percent (%) options, much like the standard C printf() function.

  **Parameters**
    * **format\*** --- The format of the string to return.
    * **X\*** --- The argument to substitute at a given position in the format.

  **Examples**
    To substitute 'World' into the string 'Hello, %s!':

    .. code-block::  custsqlite

      ;SELECT printf('Hello, %s!', 'World')
      Hello, World!

    To right-align 'small' in the string 'align:' with a column width of 10:

    .. code-block::  custsqlite

      ;SELECT printf('align: % 10s', 'small')
      align:      small

    To format 11 with a width of five characters and leading zeroes:

    .. code-block::  custsqlite

      ;SELECT printf('value: %05d', 11)
      value: 00011

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _proper:

proper(*str*)
^^^^^^^^^^^^^

  Capitalize the first character of words in the given string

  **Parameters**
    * **str\*** --- The string to capitalize.

  **Examples**
    To capitalize the words in the string 'hello, world!':

    .. code-block::  custsqlite

      ;SELECT proper('hello, world!')
      Hello, World!

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _quote:

quote(*X*)
^^^^^^^^^^

  Returns the text of an SQL literal which is the value of its argument suitable for inclusion into an SQL statement.

  **Parameters**
    * **X\*** --- The string to quote.

  **Examples**
    To quote the string 'abc':

    .. code-block::  custsqlite

      ;SELECT quote('abc')
      'abc'

    To quote the string 'abc'123':

    .. code-block::  custsqlite

      ;SELECT quote('abc''123')
      'abc''123'


----


.. _radians:

radians(*degrees*)
^^^^^^^^^^^^^^^^^^

  Converts degrees to radians

  **Parameters**
    * **degrees\*** --- The degrees value to convert to radians

  **Examples**
    To convert 180 degrees to radians:

    .. code-block::  custsqlite

      ;SELECT radians(180)
      3.14159265358979

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _raise_error:

raise_error(*msg*)
^^^^^^^^^^^^^^^^^^

  Raises an error with the given message when executed

  **Parameters**
    * **msg\*** --- The error message


----


.. _random:

random()
^^^^^^^^

  Returns a pseudo-random integer between -9223372036854775808 and +9223372036854775807.


----


.. _randomblob:

randomblob(*N*)
^^^^^^^^^^^^^^^

  Return an N-byte blob containing pseudo-random bytes.

  **Parameters**
    * **N\*** --- The size of the blob in bytes.


----


.. _rank:

rank()
^^^^^^

  Returns the row_number() of the first peer in each group with gaps

  **See Also**
    :ref:`cume_dist`, :ref:`dense_rank`, :ref:`first_value`, :ref:`lag`, :ref:`last_value`, :ref:`lead`, :ref:`nth_value`, :ref:`ntile`, :ref:`percent_rank`, :ref:`row_number`

----


.. _readlink:

readlink(*path*)
^^^^^^^^^^^^^^^^

  Read the target of a symbolic link.

  **Parameters**
    * **path\*** --- The path to the symbolic link.

  **See Also**
    :ref:`basename`, :ref:`dirname`, :ref:`joinpath`, :ref:`realpath`

----


.. _realpath:

realpath(*path*)
^^^^^^^^^^^^^^^^

  Returns the resolved version of the given path, expanding symbolic links and resolving '.' and '..' references.

  **Parameters**
    * **path\*** --- The path to resolve.

  **See Also**
    :ref:`basename`, :ref:`dirname`, :ref:`joinpath`, :ref:`readlink`

----


.. _regexp:

regexp(*re*, *str*)
^^^^^^^^^^^^^^^^^^^

  Test if a string matches a regular expression

  **Parameters**
    * **re\*** --- The regular expression to use
    * **str\*** --- The string to test against the regular expression


----


.. _regexp_capture:

regexp_capture(*string*, *pattern*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  A table-valued function that executes a regular-expression over a string and returns the captured values.  If the regex only matches a subset of the input string, it will be rerun on the remaining parts of the string until no more matches are found.

  **Parameters**
    * **string\*** --- The string to match against the given pattern.
    * **pattern\*** --- The regular expression to match.

  **Examples**
    To extract the key/value pairs 'a'/1 and 'b'/2 from the string 'a=1; b=2':

    .. code-block::  custsqlite

      ;SELECT * FROM regexp_capture('a=1; b=2', '(\w+)=(\d+)')
      match_index capture_index capture_name capture_count range_start range_stop content 
                0             0 <NULL>                   3           1          4 a=1     
                0             1                          3           1          2 a       
                0             2                          3           3          4 1       
                1             0 <NULL>                   3           6          9 b=2     
                1             1                          3           6          7 b       
                1             2                          3           8          9 2       

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _regexp_match:

regexp_match(*re*, *str*)
^^^^^^^^^^^^^^^^^^^^^^^^^

  Match a string against a regular expression and return the capture groups as JSON.

  **Parameters**
    * **re\*** --- The regular expression to use
    * **str\*** --- The string to test against the regular expression

  **Examples**
    To capture the digits from the string '123':

    .. code-block::  custsqlite

      ;SELECT regexp_match('(\d+)', '123')
      123

    To capture a number and word into a JSON object with the properties 'col_0' and 'col_1':

    .. code-block::  custsqlite

      ;SELECT regexp_match('(\d+) (\w+)', '123 four')
      {"col_0":123,"col_1":"four"}

    To capture a number and word into a JSON object with the named properties 'num' and 'str':

    .. code-block::  custsqlite

      ;SELECT regexp_match('(?<num>\d+) (?<str>\w+)', '123 four')
      {"num":123,"str":"four"}

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_replace`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _regexp_replace:

regexp_replace(*str*, *re*, *repl*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Replace the parts of a string that match a regular expression.

  **Parameters**
    * **str\*** --- The string to perform replacements on
    * **re\*** --- The regular expression to match
    * **repl\*** --- The replacement string.  You can reference capture groups with a backslash followed by the number of the group, starting with 1.

  **Examples**
    To replace the word at the start of the string 'Hello, World!' with 'Goodbye':

    .. code-block::  custsqlite

      ;SELECT regexp_replace('Hello, World!', '^(\w+)', 'Goodbye')
      Goodbye, World!

    To wrap alphanumeric words with angle brackets:

    .. code-block::  custsqlite

      ;SELECT regexp_replace('123 abc', '(\w+)', '<\1>')
      <123> <abc>

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_match`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _replace:

replace(*str*, *old*, *replacement*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns a string formed by substituting the replacement string for every occurrence of the old string in the given string.

  **Parameters**
    * **str\*** --- The string to perform substitutions on.
    * **old\*** --- The string to be replaced.
    * **replacement\*** --- The string to replace any occurrences of the old string with.

  **Examples**
    To replace the string 'x' with 'z' in 'abc':

    .. code-block::  custsqlite

      ;SELECT replace('abc', 'x', 'z')
      abc

    To replace the string 'a' with 'z' in 'abc':

    .. code-block::  custsqlite

      ;SELECT replace('abc', 'a', 'z')
      zbc

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _replicate:

replicate(*str*, *N*)
^^^^^^^^^^^^^^^^^^^^^

  Returns the given string concatenated N times.

  **Parameters**
    * **str\*** --- The string to replicate.
    * **N\*** --- The number of times to replicate the string.

  **Examples**
    To repeat the string 'abc' three times:

    .. code-block::  custsqlite

      ;SELECT replicate('abc', 3)
      abcabcabc

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _reverse:

reverse(*str*)
^^^^^^^^^^^^^^

  Returns the reverse of the given string.

  **Parameters**
    * **str\*** --- The string to reverse.

  **Examples**
    To reverse the string 'abc':

    .. code-block::  custsqlite

      ;SELECT reverse('abc')
      cba

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _rightstr:

rightstr(*str*, *N*)
^^^^^^^^^^^^^^^^^^^^

  Returns the N rightmost (UTF-8) characters in the given string.

  **Parameters**
    * **str\*** --- The string to return subset.
    * **N\*** --- The number of characters from the right side of the string to return.

  **Examples**
    To get the last character of the string 'abc':

    .. code-block::  custsqlite

      ;SELECT rightstr('abc', 1)
      c

    To get the last ten characters of a string, regardless of size:

    .. code-block::  custsqlite

      ;SELECT rightstr('abc', 10)
      abc

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _round:

round(*num*, *\[digits\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns a floating-point value rounded to the given number of digits to the right of the decimal point.

  **Parameters**
    * **num\*** --- The value to round.
    * **digits** --- The number of digits to the right of the decimal to round to.

  **Examples**
    To round the number 123.456 to an integer:

    .. code-block::  custsqlite

      ;SELECT round(123.456)
      123.0

    To round the number 123.456 to a precision of 1:

    .. code-block::  custsqlite

      ;SELECT round(123.456, 1)
      123.5

    To round the number 123.456 to a precision of 5:

    .. code-block::  custsqlite

      ;SELECT round(123.456, 5)
      123.456

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _row_number:

row_number()
^^^^^^^^^^^^

  Returns the number of the row within the current partition, starting from 1.

  **Examples**
    To number messages from a process:

    .. code-block::  custsqlite

      ;SELECT row_number() OVER (PARTITION BY ex_procname ORDER BY log_line) AS msg_num, ex_procname, log_body FROM lnav_example_log
      msg_num ex_procname    log_body     
            1 gw          Goodbye, World! 
            2 gw          Goodbye, World! 
            3 gw          Goodbye, World! 
            1 hw          Hello, World!   

  **See Also**
    :ref:`cume_dist`, :ref:`dense_rank`, :ref:`first_value`, :ref:`lag`, :ref:`last_value`, :ref:`lead`, :ref:`nth_value`, :ref:`ntile`, :ref:`percent_rank`, :ref:`rank`

----


.. _rtrim:

rtrim(*str*, *\[chars\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns a string formed by removing any and all characters that appear in the second argument from the right side of the first.

  **Parameters**
    * **str\*** --- The string to trim characters from the right side
    * **chars** --- The characters to trim.  Defaults to spaces.

  **Examples**
    To trim the whitespace from the end of the string 'abc   ':

    .. code-block::  custsqlite

      ;SELECT rtrim('abc   ')
      abc

    To trim the characters 'b' and 'c' from the string 'abbbbcccc':

    .. code-block::  custsqlite

      ;SELECT rtrim('abbbbcccc', 'bc')
      a

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _sign:

sign(*num*)
^^^^^^^^^^^

  Returns the sign of the given number as -1, 0, or 1

  **Parameters**
    * **num\*** --- The number

  **Examples**
    To get the sign of 10:

    .. code-block::  custsqlite

      ;SELECT sign(10)
      1

    To get the sign of 0:

    .. code-block::  custsqlite

      ;SELECT sign(0)
      0

    To get the sign of -10:

    .. code-block::  custsqlite

      ;SELECT sign(-10)
      -1

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _sparkline:

sparkline(*value*, *\[upper\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Function used to generate a sparkline bar chart.  The non-aggregate version converts a single numeric value on a range to a bar chart character.  The aggregate version returns a string with a bar character for every numeric input

  **Parameters**
    * **value\*** --- The numeric value to convert
    * **upper** --- The upper bound of the numeric range.  The non-aggregate version defaults to 100.  The aggregate version uses the largest value in the inputs.

  **Examples**
    To get the unicode block element for the value 32 in the range of 0-128:

    .. code-block::  custsqlite

      ;SELECT sparkline(32, 128)
      ▂

    To chart the values in a JSON array:

    .. code-block::  custsqlite

      ;SELECT sparkline(value) FROM json_each('[0, 1, 2, 3, 4, 5, 6, 7, 8]')
       ▁▂▃▄▅▆▇█

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _spooky_hash:

spooky_hash(*str*)
^^^^^^^^^^^^^^^^^^

  Compute the hash value for the given arguments.

  **Parameters**
    * **str** --- The string to hash

  **Examples**
    To produce a hash for the string 'Hello, World!':

    .. code-block::  custsqlite

      ;SELECT spooky_hash('Hello, World!')
      0b1d52cc5427db4c6a9eed9d3e5700f4

    To produce a hash for the parameters where one is NULL:

    .. code-block::  custsqlite

      ;SELECT spooky_hash('Hello, World!', NULL)
      c96ee75d48e6ea444fee8af948f6da25

    To produce a hash for the parameters where one is an empty string:

    .. code-block::  custsqlite

      ;SELECT spooky_hash('Hello, World!', '')
      c96ee75d48e6ea444fee8af948f6da25

    To produce a hash for the parameters where one is a number:

    .. code-block::  custsqlite

      ;SELECT spooky_hash('Hello, World!', 123)
      f96b3d9c1a19f4394c97a1b79b1880df

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _sqlite_compileoption_get:

sqlite_compileoption_get(*N*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns the N-th compile-time option used to build SQLite or NULL if N is out of range.

  **Parameters**
    * **N\*** --- The option number to get


----


.. _sqlite_compileoption_used:

sqlite_compileoption_used(*option*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns true (1) or false (0) depending on whether or not that compile-time option was used during the build.

  **Parameters**
    * **option\*** --- The name of the compile-time option.

  **Examples**
    To check if the SQLite library was compiled with ENABLE_FTS3:

    .. code-block::  custsqlite

      ;SELECT sqlite_compileoption_used('ENABLE_FTS3')
      1


----


.. _sqlite_source_id:

sqlite_source_id()
^^^^^^^^^^^^^^^^^^

  Returns a string that identifies the specific version of the source code that was used to build the SQLite library.


----


.. _sqlite_version:

sqlite_version()
^^^^^^^^^^^^^^^^

  Returns the version string for the SQLite library that is running.


----


.. _square:

square(*num*)
^^^^^^^^^^^^^

  Returns the square of the argument

  **Parameters**
    * **num\*** --- The number to square

  **Examples**
    To get the square of two:

    .. code-block::  custsqlite

      ;SELECT square(2)
      4

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`sum`, :ref:`total`

----


.. _startswith:

startswith(*str*, *prefix*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Test if a string begins with the given prefix

  **Parameters**
    * **str\*** --- The string to test
    * **prefix\*** --- The prefix to check in the string

  **Examples**
    To test if the string 'foobar' starts with 'foo':

    .. code-block::  custsqlite

      ;SELECT startswith('foobar', 'foo')
      1

    To test if the string 'foobar' starts with 'bar':

    .. code-block::  custsqlite

      ;SELECT startswith('foobar', 'bar')
      0

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _strfilter:

strfilter(*source*, *include*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns the source string with only the characters given in the second parameter

  **Parameters**
    * **source\*** --- The string to filter
    * **include\*** --- The characters to include in the result

  **Examples**
    To get the 'b', 'c', and 'd' characters from the string 'abcabc':

    .. code-block::  custsqlite

      ;SELECT strfilter('abcabc', 'bcd')
      bcbc

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _strftime:

strftime(*format*, *timestring*, *modifier*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns the date formatted according to the format string specified as the first argument.

  **Parameters**
    * **format\*** --- A format string with substitutions similar to those found in the strftime() standard C library.
    * **timestring\*** --- The string to convert to a date with time.
    * **modifier** --- A transformation that is applied to the value to the left.

  **Examples**
    To get the year from the timestamp '2017-01-02T03:04:05':

    .. code-block::  custsqlite

      ;SELECT strftime('%Y', '2017-01-02T03:04:05')
      2017

    To create a string with the time from the timestamp '2017-01-02T03:04:05' plus one minute:

    .. code-block::  custsqlite

      ;SELECT strftime('The time is: %H:%M:%S', '2017-01-02T03:04:05', '+1 minute')
      The time is: 03:05:05

    To create a string with the Julian day from the epoch timestamp 1491341842:

    .. code-block::  custsqlite

      ;SELECT strftime('Julian day: %J', 1491341842, 'unixepoch')
      Julian day: 2457848.400949074

  **See Also**
    :ref:`date`, :ref:`datetime`, :ref:`julianday`, :ref:`time`, :ref:`timediff`, :ref:`timeslice`

----


.. _substr:

substr(*str*, *start*, *\[size\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns a substring of input string X that begins with the Y-th character and which is Z characters long.

  **Parameters**
    * **str\*** --- The string to extract a substring from.
    * **start\*** --- The index within 'str' that is the start of the substring.  Indexes begin at 1.  A negative value means that the substring is found by counting from the right rather than the left.  
    * **size** --- The size of the substring.  If not given, then all characters through the end of the string are returned.  If the value is negative, then the characters before the start are returned.

  **Examples**
    To get the substring starting at the second character until the end of the string 'abc':

    .. code-block::  custsqlite

      ;SELECT substr('abc', 2)
      bc

    To get the substring of size one starting at the second character of the string 'abc':

    .. code-block::  custsqlite

      ;SELECT substr('abc', 2, 1)
      b

    To get the substring starting at the last character until the end of the string 'abc':

    .. code-block::  custsqlite

      ;SELECT substr('abc', -1)
      c

    To get the substring starting at the last character and going backwards one step of the string 'abc':

    .. code-block::  custsqlite

      ;SELECT substr('abc', -1, -1)
      b

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _sum:

sum(*X*)
^^^^^^^^

  Returns the sum of the values in the group as an integer.

  **Parameters**
    * **X\*** --- The values to add.

  **Examples**
    To sum all of the values in the column 'ex_duration' from the table 'lnav_example_log':

    .. code-block::  custsqlite

      ;SELECT sum(ex_duration) FROM lnav_example_log
      17

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`total`

----


.. _time:

time(*timestring*, *modifier*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns the time in this format: HH:MM:SS.

  **Parameters**
    * **timestring\*** --- The string to convert to a time.
    * **modifier** --- A transformation that is applied to the value to the left.

  **Examples**
    To get the time portion of the timestamp '2017-01-02T03:04:05':

    .. code-block::  custsqlite

      ;SELECT time('2017-01-02T03:04:05')
      03:04:05

    To get the time portion of the timestamp '2017-01-02T03:04:05' plus one minute:

    .. code-block::  custsqlite

      ;SELECT time('2017-01-02T03:04:05', '+1 minute')
      03:05:05

    To get the time portion of the epoch timestamp 1491341842:

    .. code-block::  custsqlite

      ;SELECT time(1491341842, 'unixepoch')
      21:37:22

  **See Also**
    :ref:`date`, :ref:`datetime`, :ref:`julianday`, :ref:`strftime`, :ref:`timediff`, :ref:`timeslice`

----


.. _timediff:

timediff(*time1*, *time2*)
^^^^^^^^^^^^^^^^^^^^^^^^^^

  Compute the difference between two timestamps in seconds

  **Parameters**
    * **time1\*** --- The first timestamp
    * **time2\*** --- The timestamp to subtract from the first

  **Examples**
    To get the difference between two timestamps:

    .. code-block::  custsqlite

      ;SELECT timediff('2017-02-03T04:05:06', '2017-02-03T04:05:00')
      6.0

    To get the difference between relative timestamps:

    .. code-block::  custsqlite

      ;SELECT timediff('today', 'yesterday')
      86400.0

  **See Also**
    :ref:`date`, :ref:`datetime`, :ref:`julianday`, :ref:`strftime`, :ref:`time`, :ref:`timeslice`

----


.. _timeslice:

timeslice(*time*, *slice*)
^^^^^^^^^^^^^^^^^^^^^^^^^^

  Return the start of the slice of time that the given timestamp falls in.  If the time falls outside of the slice, NULL is returned.

  **Parameters**
    * **time\*** --- The timestamp to get the time slice for.
    * **slice\*** --- The size of the time slices

  **Examples**
    To get the timestamp rounded down to the start of the ten minute slice:

    .. code-block::  custsqlite

      ;SELECT timeslice('2017-01-01T05:05:00', '10m')
      2017-01-01 05:00:00.000

    To group log messages into five minute buckets and count them:

    .. code-block::  custsqlite

      ;SELECT timeslice(log_time_msecs, '5m') AS slice, count(1) FROM lnav_example_log GROUP BY slice
               slice          count(1) 
      2017-02-03 04:05:00.000        2 
      2017-02-03 04:25:00.000        1 
      2017-02-03 04:55:00.000        1 

    To group log messages by those before 4:30am and after:

    .. code-block::  custsqlite

      ;SELECT timeslice(log_time_msecs, 'before 4:30am') AS slice, count(1) FROM lnav_example_log GROUP BY slice
               slice          count(1) 
      <NULL>                         1 
      2017-02-03 00:00:00.000        3 

  **See Also**
    :ref:`date`, :ref:`datetime`, :ref:`julianday`, :ref:`strftime`, :ref:`time`, :ref:`timediff`

----


.. _total:

total(*X*)
^^^^^^^^^^

  Returns the sum of the values in the group as a floating-point.

  **Parameters**
    * **X\*** --- The values to add.

  **Examples**
    To total all of the values in the column 'ex_duration' from the table 'lnav_example_log':

    .. code-block::  custsqlite

      ;SELECT total(ex_duration) FROM lnav_example_log
      17.0

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`

----


.. _total_changes:

total_changes()
^^^^^^^^^^^^^^^

  Returns the number of row changes caused by INSERT, UPDATE or DELETE statements since the current database connection was opened.


----


.. _trim:

trim(*str*, *\[chars\]*)
^^^^^^^^^^^^^^^^^^^^^^^^

  Returns a string formed by removing any and all characters that appear in the second argument from the left and right sides of the first.

  **Parameters**
    * **str\*** --- The string to trim characters from the left and right sides.
    * **chars** --- The characters to trim.  Defaults to spaces.

  **Examples**
    To trim whitespace from the start and end of the string '    abc   ':

    .. code-block::  custsqlite

      ;SELECT trim('    abc   ')
      abc

    To trim the characters '-' and '+' from the string '-+abc+-':

    .. code-block::  custsqlite

      ;SELECT trim('-+abc+-', '-+')
      abc

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

----


.. _typeof:

typeof(*X*)
^^^^^^^^^^^

  Returns a string that indicates the datatype of the expression X: "null", "integer", "real", "text", or "blob".

  **Parameters**
    * **X\*** --- The expression to check.

  **Examples**
    To get the type of the number 1:

    .. code-block::  custsqlite

      ;SELECT typeof(1)
      integer

    To get the type of the string 'abc':

    .. code-block::  custsqlite

      ;SELECT typeof('abc')
      text


----


.. _unicode:

unicode(*X*)
^^^^^^^^^^^^

  Returns the numeric unicode code point corresponding to the first character of the string X.

  **Parameters**
    * **X\*** --- The string to examine.

  **Examples**
    To get the unicode code point for the first character of 'abc':

    .. code-block::  custsqlite

      ;SELECT unicode('abc')
      97

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`upper`, :ref:`xpath`

----


.. _unlikely:

unlikely(*value*)
^^^^^^^^^^^^^^^^^

  Short-hand for likelihood(X, 0.0625)

  **Parameters**
    * **value\*** --- The boolean value to return


----


.. _upper:

upper(*str*)
^^^^^^^^^^^^

  Returns a copy of the given string with all ASCII characters converted to upper case.

  **Parameters**
    * **str\*** --- The string to convert.

  **Examples**
    To uppercase the string 'aBc':

    .. code-block::  custsqlite

      ;SELECT upper('aBc')
      ABC

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`xpath`

----


.. _xpath:

xpath(*xpath*, *xmldoc*)
^^^^^^^^^^^^^^^^^^^^^^^^

  A table-valued function that executes an xpath expression over an XML string and returns the selected values.

  **Parameters**
    * **xpath\*** --- The XPATH expression to evaluate over the XML document.
    * **xmldoc\*** --- The XML document as a string.

  **Examples**
    To select the XML nodes on the path '/abc/def':

    .. code-block::  custsqlite

      ;SELECT * FROM xpath('/abc/def', '<abc><def a="b">Hello</def><def>Bye</def></abc>')
              result           node_path  node_attr node_text 
      <def a="b">Hello</def>␊ /abc/def[1] {"a":"b"} Hello     
      <def>Bye</def>␊         /abc/def[2] {}        Bye       

    To select all 'a' attributes on the path '/abc/def':

    .. code-block::  custsqlite

      ;SELECT * FROM xpath('/abc/def/@a', '<abc><def a="b">Hello</def><def>Bye</def></abc>')
      result   node_path    node_attr node_text 
      b      /abc/def[1]/@a {"a":"b"} Hello     

    To select the text nodes on the path '/abc/def':

    .. code-block::  custsqlite

      ;SELECT * FROM xpath('/abc/def/text()', '<abc><def a="b">Hello &#x2605;</def></abc>')
      result     node_path    node_attr node_text 
      Hello ★ /abc/def/text() {}        Hello ★   

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`trim`, :ref:`unicode`, :ref:`upper`

----


.. _zeroblob:

zeroblob(*N*)
^^^^^^^^^^^^^

  Returns a BLOB consisting of N bytes of 0x00.

  **Parameters**
    * **N\*** --- The size of the BLOB.


----

