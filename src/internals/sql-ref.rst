
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

      * **then-expr\*** --- The result for this branch.
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
      ('a2' < 'a10') ('a2' <⋯nocase) 
                   0               1 


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
    * **result-column** --- The expression used to generate a result for this column.
    * **table** --- The table(s) to query for data
    * **cond** --- The conditions used to select the rows to return.
    * **grouping-expr** --- The expression to use when grouping rows.
    * **ordering-term** --- The values to use when ordering the result set.
    * **limit-expr** --- The maximum number of rows to return.

  **Examples**
    To select all of the columns from the table 'lnav_example_log':

    .. code-block::  custsqlite

      ;SELECT * FROM lnav_example_log
       log_line  log_part    log_time     log_actual_time log_idle_msecs log_level  log_mark  log_comment log_tags log_filters ex_procname ex_duration log_time_msecs log_path    log_text        log_body     
               0 <NULL>   2017-02⋯:06.100 2017-02⋯:06.100              0 info               0 <NULL>      <NULL>   <NULL>      hw                    2  1486094706000 /tmp/log 2017-02⋯ World! Hello, World!   
               1 <NULL>   2017-02⋯:06.200 2017-02⋯:06.200            100 error              0 <NULL>      <NULL>   <NULL>      gw                    4  1486094706000 /tmp/log 2017-02⋯ World! Goodbye, World! 
               2 new      2017-02⋯:06.200 2017-02⋯:06.200        1200000 warn               0 <NULL>      <NULL>   <NULL>      gw                    1  1486095906000 /tmp/log 2017-02⋯ World! Goodbye, World! 
               3 new      2017-02⋯:06.200 2017-02⋯:06.200        1800000 debug              0 <NULL>      <NULL>   <NULL>      gw                   10  1486097706000 /tmp/log 2017-02⋯ World! Goodbye, World! 


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

      * **expr\*** --- The values to place into the column.
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

      ;SELECT printf('%.3f', acos(0.2))
      1.369

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
      0.6223625037147786

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _anonymize:

anonymize(*value*)
^^^^^^^^^^^^^^^^^^

  Replace identifying information with random values.

  **PRQL Name**: text.anonymize

  **Parameters**
    * **value\*** --- The text to anonymize

  **Examples**
    To anonymize an IP address:

    .. code-block::  custsqlite

      ;SELECT anonymize('Hello, 192.168.1.2')
      Aback, 10.0.0.1

  **See Also**
    :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
      0.2013579207903308

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
      0.19869011034924142

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
      0.19739555984988078

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
      45

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
      0.2027325540540822

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
      45

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
      ex_procname avg(ex_⋯ration) 
      gw                        5 
      hw                        2 

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _basename:

basename(*path*)
^^^^^^^^^^^^^^^^

  Extract the base portion of a pathname.

  **PRQL Name**: fs.basename

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

    To get the base of a path:

    .. code-block::  custsqlite

      ;from [{p='foo/bar'}] | select { fs.basename p }
      bar

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
    :ref:`anonymize`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`datetime`, :ref:`humanize_duration`, :ref:`julianday`, :ref:`strftime`, :ref:`time`, :ref:`timediff`, :ref:`timeslice`, :ref:`timezone`

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
    :ref:`date`, :ref:`humanize_duration`, :ref:`julianday`, :ref:`strftime`, :ref:`time`, :ref:`timediff`, :ref:`timeslice`, :ref:`timezone`

----


.. _decode:

decode(*value*, *algorithm*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Decode the value using the given algorithm

  **Parameters**
    * **value\*** --- The value to decode
    * **algorithm\*** --- One of the following encoding algorithms: base64, hex, uri

  **Examples**
    To decode the URI-encoded string '%63%75%72%6c':

    .. code-block::  custsqlite

      ;SELECT decode('%63%75%72%6c', 'uri')
      curl

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
      180

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

  **PRQL Name**: fs.dirname

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


.. _echoln:

echoln(*value*)
^^^^^^^^^^^^^^^

  Echo the argument to the current output file and return it

  **Parameters**
    * **value\*** --- The value to write to the current output file

  **See Also**
    :ref:`append_to`, :ref:`dot_dump`, :ref:`dot_read`, :ref:`echo`, :ref:`export_session_to`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`redirect_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`

----


.. _encode:

encode(*value*, *algorithm*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Encode the value using the given algorithm

  **Parameters**
    * **value\*** --- The value to encode
    * **algorithm\*** --- One of the following encoding algorithms: base64, hex, uri

  **Examples**
    To base64-encode 'Hello, World!':

    .. code-block::  custsqlite

      ;SELECT encode('Hello, World!', 'base64')
      SGVsbG8sIFdvcmxkIQ==

    To hex-encode 'Hello, World!':

    .. code-block::  custsqlite

      ;SELECT encode('Hello, World!', 'hex')
      48656c6c6f2c20576f726c6421

    To URI-encode 'Hello, World!':

    .. code-block::  custsqlite

      ;SELECT encode('Hello, World!', 'uri')
      Hello%2C%20World%21

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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

  **PRQL Name**: text.discover

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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


.. _fstat:

fstat(*pattern*)
^^^^^^^^^^^^^^^^

  A table-valued function for getting information about file paths/globs

  **Parameters**
    * **pattern\*** --- The file path or glob pattern to query.

  **Examples**
    To read a file and raise an error if there is a problem:

    .. code-block::  custsqlite

      ;SELECT ifnull(data, raise_error('cannot read: ' || st_name, error)) FROM fstat('/non-existent')
      ✘ error: cannot read: non-existent
       reason: No such file or directory
       --> fstat:1
       | SELECT ifnull(data, raise_error('cannot read: ' || st_name, error)) FROM fstat('/non-existent')


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

  **PRQL Name**: net.gethostbyaddr

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

  **PRQL Name**: net.gethostbyname

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _gunzip:

gunzip(*b*)
^^^^^^^^^^^

  Decompress a gzip file

  **Parameters**
    * **b** --- The blob to decompress

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _gzip:

gzip(*value*)
^^^^^^^^^^^^^

  Compress a string into a gzip file

  **Parameters**
    * **value** --- The value to compress

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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


.. _humanize_duration:

humanize_duration(*secs*)
^^^^^^^^^^^^^^^^^^^^^^^^^

  Format the given seconds value as an abbreviated duration string

  **PRQL Name**: humanize.duration

  **Parameters**
    * **secs\*** --- The duration in seconds

  **Examples**
    To format a duration:

    .. code-block::  custsqlite

      ;SELECT humanize_duration(15 * 60)
      15m00s

    To format a sub-second value:

    .. code-block::  custsqlite

      ;SELECT humanize_duration(1.5)
      1s500

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`date`, :ref:`datetime`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`julianday`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`strftime`, :ref:`substr`, :ref:`time`, :ref:`timediff`, :ref:`timeslice`, :ref:`timezone`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _humanize_file_size:

humanize_file_size(*value*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Format the given file size as a human-friendly string

  **PRQL Name**: humanize.file_size

  **Parameters**
    * **value\*** --- The file size to format

  **Examples**
    To format an amount:

    .. code-block::  custsqlite

      ;SELECT humanize_file_size(10 * 1024 * 1024)
      10.0MB

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _humanize_id:

humanize_id(*id*)
^^^^^^^^^^^^^^^^^

  Colorize the given ID using ANSI escape codes.

  **PRQL Name**: humanize.id

  **Parameters**
    * **id\*** --- The identifier to color

  **Examples**
    To colorize the ID 'cluster1':

    .. code-block::  custsqlite

      ;SELECT humanize_id('cluster1')
      cluster1

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _jget:

jget(*json*, *ptr*, *\[default\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Get the value from a JSON object using a JSON-Pointer.

  **PRQL Name**: json.get

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
    :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _joinpath:

joinpath(*path*)
^^^^^^^^^^^^^^^^

  Join components of a path together.

  **PRQL Name**: fs.join

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


.. _json:

json(*X*)
^^^^^^^^^

  Verifies that its argument is valid JSON and returns a minified version or throws an error.

  **Parameters**
    * **X\*** --- The string to interpret as JSON.

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`yaml_to_json`

----


.. _json_array:

json_array(*X*)
^^^^^^^^^^^^^^^

  Constructs a JSON array from its arguments.

  **Parameters**
    * **X** --- The values of the JSON array

  **Examples**
    To create an array of all types:

    .. code-block::  custsqlite

      ;SELECT json_array(NULL, 1, 2.1, 'three', json_array(4), json_object('five', 'six'))
      [null,1,2.1,"three",[4],{"five":"six"}]

    To create an empty array:

    .. code-block::  custsqlite

      ;SELECT json_array()
      []

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_array_length:

json_array_length(*X*, *\[P\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns the length of a JSON array.

  **Parameters**
    * **X\*** --- The JSON object.
    * **P** --- The path to the array in 'X'.

  **Examples**
    To get the length of an array:

    .. code-block::  custsqlite

      ;SELECT json_array_length('[1, 2, 3]')
      3

    To get the length of a nested array:

    .. code-block::  custsqlite

      ;SELECT json_array_length('{"arr": [1, 2, 3]}', '$.arr')
      3

  **See Also**
    :ref:`jget`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_concat:

json_concat(*json*, *value*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns an array with the given values concatenated onto the end.  If the initial value is null, the result will be an array with the given elements.  If the initial value is an array, the result will be an array with the given values at the end.  If the initial value is not null or an array, the result will be an array with two elements: the initial value and the given value.

  **PRQL Name**: json.concat

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
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_contains:

json_contains(*json*, *value*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Check if a JSON value contains the given element.

  **PRQL Name**: json.contains

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
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_each:

json_each(*X*, *\[P\]*)
^^^^^^^^^^^^^^^^^^^^^^^

  A table-valued-function that returns the children of the top-level JSON value

  **Parameters**
    * **X\*** --- The JSON value to query
    * **P** --- The path to the value to query

  **Examples**
    To iterate over an array:

    .. code-block::  custsqlite

      ;SELECT * FROM json_each('[null,1,"two",{"three":4.5}]')
      key     value      type    atom  id parent fullkey path 
        0        <NULL> null    <NULL>  2 <NULL> $[0]    $    
        1             1 integer      1  3 <NULL> $[1]    $    
        2           two text       two  5 <NULL> $[2]    $    
        3 {"three":4.5} object  <NULL>  9 <NULL> $[3]    $    

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_extract:

json_extract(*X*, *P*)
^^^^^^^^^^^^^^^^^^^^^^

  Returns the value(s) from the given JSON at the given path(s).

  **Parameters**
    * **X\*** --- The JSON value.
    * **P** --- The path to extract.

  **Examples**
    To get a number:

    .. code-block::  custsqlite

      ;SELECT json_extract('{"num": 1}', '$.num')
      1

    To get two numbers:

    .. code-block::  custsqlite

      ;SELECT json_extract('{"num": 1, "val": 2}', '$.num', '$.val')
      [1,2]

    To get an object:

    .. code-block::  custsqlite

      ;SELECT json_extract('{"obj": {"sub": 1}}', '$.obj')
      {"sub":1}

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_group_array:

json_group_array(*value*)
^^^^^^^^^^^^^^^^^^^^^^^^^

  Collect the given values from a query into a JSON array

  **PRQL Name**: json.group_array

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
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_group_object:

json_group_object(*name*, *value*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Collect the given values from a query into a JSON object

  **PRQL Name**: json.group_object

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
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_insert:

json_insert(*X*, *P*, *Y*)
^^^^^^^^^^^^^^^^^^^^^^^^^^

  Inserts values into a JSON object/array at the given locations, if it does not already exist

  **Parameters**
    * **X\*** --- The JSON value to update
    * **P\*** --- The path to the insertion point.  A '#' array index means append the value
    * **Y\*** --- The value to insert

  **Examples**
    To append to an array:

    .. code-block::  custsqlite

      ;SELECT json_insert('[1, 2]', '$[#]', 3)
      [1,2,3]

    To update an object:

    .. code-block::  custsqlite

      ;SELECT json_insert('{"a": 1}', '$.b', 2)
      {"a":1,"b":2}

    To ensure a value is set:

    .. code-block::  custsqlite

      ;SELECT json_insert('{"a": 1}', '$.a', 2)
      {"a":1}

    To update multiple values:

    .. code-block::  custsqlite

      ;SELECT json_insert('{"a": 1}', '$.b', 2, '$.c', 3)
      {"a":1,"b":2,"c":3}

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_object:

json_object(*N*, *V*)
^^^^^^^^^^^^^^^^^^^^^

  Create a JSON object from the given arguments

  **Parameters**
    * **N\*** --- The property name
    * **V\*** --- The property value

  **Examples**
    To create an object:

    .. code-block::  custsqlite

      ;SELECT json_object('a', 1, 'b', 'c')
      {"a":1,"b":"c"}

    To create an empty object:

    .. code-block::  custsqlite

      ;SELECT json_object()
      {}

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_quote:

json_quote(*X*)
^^^^^^^^^^^^^^^

  Returns the JSON representation of the given value, if it is not already JSON

  **Parameters**
    * **X\*** --- The value to convert

  **Examples**
    To convert a string:

    .. code-block::  custsqlite

      ;SELECT json_quote('Hello, World!')
      "Hello, World!"

    To pass through an existing JSON value:

    .. code-block::  custsqlite

      ;SELECT json_quote(json('"Hello, World!"'))
      "Hello, World!"

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_remove:

json_remove(*X*, *P*)
^^^^^^^^^^^^^^^^^^^^^

  Removes paths from a JSON value

  **Parameters**
    * **X\*** --- The JSON value to update
    * **P** --- The paths to remove

  **Examples**
    To remove elements of an array:

    .. code-block::  custsqlite

      ;SELECT json_remove('[1,2,3]', '$[1]', '$[1]')
      [1]

    To remove object properties:

    .. code-block::  custsqlite

      ;SELECT json_remove('{"a":1,"b":2}', '$.b')
      {"a":1}

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_replace:

json_replace(*X*, *P*, *Y*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Replaces existing values in a JSON object/array at the given locations

  **Parameters**
    * **X\*** --- The JSON value to update
    * **P\*** --- The path to replace
    * **Y\*** --- The new value for the property

  **Examples**
    To replace an existing value:

    .. code-block::  custsqlite

      ;SELECT json_replace('{"a": 1}', '$.a', 2)
      {"a":2}

    To replace a value without creating a new property:

    .. code-block::  custsqlite

      ;SELECT json_replace('{"a": 1}', '$.a', 2, '$.b', 3)
      {"a":2}

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_set:

json_set(*X*, *P*, *Y*)
^^^^^^^^^^^^^^^^^^^^^^^

  Inserts or replaces existing values in a JSON object/array at the given locations

  **Parameters**
    * **X\*** --- The JSON value to update
    * **P\*** --- The path to the insertion point.  A '#' array index means append the value
    * **Y\*** --- The value to set

  **Examples**
    To replace an existing array element:

    .. code-block::  custsqlite

      ;SELECT json_set('[1, 2]', '$[1]', 3)
      [1,3]

    To replace a value and create a new property:

    .. code-block::  custsqlite

      ;SELECT json_set('{"a": 1}', '$.a', 2, '$.b', 3)
      {"a":2,"b":3}

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_tree:

json_tree(*X*, *\[P\]*)
^^^^^^^^^^^^^^^^^^^^^^^

  A table-valued-function that recursively descends through a JSON value

  **Parameters**
    * **X\*** --- The JSON value to query
    * **P** --- The path to the value to query

  **Examples**
    To iterate over an array:

    .. code-block::  custsqlite

      ;SELECT key,value,type,atom,fullkey,path FROM json_tree('[null,1,"two",{"three":4.5}]')
       key        value       type    atom   fullkey   path 
      <NULL> [null,1⋯":4.5}] array   <NULL> $          $    
           0          <NULL> null    <NULL> $[0]       $    
           1               1 integer      1 $[1]       $    
           2             two text       two $[2]       $    
           3   {"three":4.5} object  <NULL> $[3]       $    
       three             4.5 real       4.5 $[3].three $[3] 

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_type:

json_type(*X*, *\[P\]*)
^^^^^^^^^^^^^^^^^^^^^^^

  Returns the type of a JSON value

  **Parameters**
    * **X\*** --- The JSON value to query
    * **P** --- The path to the value

  **Examples**
    To get the type of a value:

    .. code-block::  custsqlite

      ;SELECT json_type('[null,1,2.1,"three",{"four":5}]')
      array

    To get the type of an array element:

    .. code-block::  custsqlite

      ;SELECT json_type('[null,1,2.1,"three",{"four":5}]', '$[0]')
      null

    To get the type of a string:

    .. code-block::  custsqlite

      ;SELECT json_type('[null,1,2.1,"three",{"four":5}]', '$[3]')
      text

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_valid`, :ref:`json`, :ref:`yaml_to_json`

----


.. _json_valid:

json_valid(*X*)
^^^^^^^^^^^^^^^

  Tests if the given value is valid JSON

  **Parameters**
    * **X\*** --- The value to check

  **Examples**
    To check an empty string:

    .. code-block::  custsqlite

      ;SELECT json_valid('')
      0

    To check a string:

    .. code-block::  custsqlite

      ;SELECT json_valid('"a"')
      1

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json`, :ref:`yaml_to_json`

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
      2457755.627835648

    To get the julian day from the timestamp '2017-01-02T03:04:05' plus one minute:

    .. code-block::  custsqlite

      ;SELECT julianday('2017-01-02T03:04:05', '+1 minute')
      2457755.6285300925

    To get the julian day from the timestamp 1491341842:

    .. code-block::  custsqlite

      ;SELECT julianday(1491341842, 'unixepoch')
      2457848.400949074

  **See Also**
    :ref:`date`, :ref:`datetime`, :ref:`humanize_duration`, :ref:`strftime`, :ref:`time`, :ref:`timediff`, :ref:`timeslice`, :ref:`timezone`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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

  **PRQL Name**: lnav.view.top_file


----


.. _lnav_version:

lnav_version()
^^^^^^^^^^^^^^

  Return the current version of lnav

  **PRQL Name**: lnav.version


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
      2.0794415416798357

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
      2

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _log_msg_line:

log_msg_line()
^^^^^^^^^^^^^^

  Return the starting line number of the focused log message.

  **PRQL Name**: lnav.view.msg_line


----


.. _log_top_datetime:

log_top_datetime()
^^^^^^^^^^^^^^^^^^

  Return the timestamp of the line at the top of the log view.

  **PRQL Name**: lnav.view.top_datetime


----


.. _log_top_line:

log_top_line()
^^^^^^^^^^^^^^

  Return the number of the focused line of the log view.

  **PRQL Name**: lnav.view.top_line


----


.. _logfmt2json:

logfmt2json(*str*)
^^^^^^^^^^^^^^^^^^

  Convert a logfmt-encoded string into JSON

  **PRQL Name**: logfmt.to_json

  **Parameters**
    * **str\*** --- The logfmt message to parse

  **Examples**
    To extract key/value pairs from a log message:

    .. code-block::  custsqlite

      ;SELECT logfmt2json('foo=1 bar=2 name="Rolo Tomassi"')
      {"foo":1,"bar":2,"name":"Rolo Tomassi"}

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _ltrim:

ltrim(*str*, *\[chars\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^

  Returns a string formed by removing any and all characters that appear in the second argument from the left side of the first.

  **Parameters**
    * **str\*** --- The string to trim characters from the left side
    * **chars** --- The characters to trim.  Defaults to spaces.

  **Examples**
    To trim the leading space characters from the string '   abc':

    .. code-block::  custsqlite

      ;SELECT ltrim('   abc')
      abc

    To trim the characters 'a' or 'b' from the left side of the string 'aaaabbbc':

    .. code-block::  custsqlite

      ;SELECT ltrim('aaaabbbc', 'ab')
      c

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _parse_url:

parse_url(*url*)
^^^^^^^^^^^^^^^^

  Parse a URL and return the components in a JSON object. Limitations: not all URL schemes are supported and repeated query parameters are not captured.

  **Parameters**
    * **url\*** --- The URL to parse

  **Examples**
    To parse the URL 'https://example.com/search?q=hello%20world':

    .. code-block::  custsqlite

      ;SELECT parse_url('https://example.com/search?q=hello%20world')
      {"scheme":"https","username":null,"password":null,"host":"example.com","port":null,"path":"/search","query":"q=hello%20world","parameters":{"q":"hello world"},"fragment":null}

    To parse the URL 'https://alice@[fe80::14ff:4ee5:1215:2fb2]':

    .. code-block::  custsqlite

      ;SELECT parse_url('https://alice@[fe80::14ff:4ee5:1215:2fb2]')
      {"scheme":"https","username":"alice","password":null,"host":"[fe80::14ff:4ee5:1215:2fb2]","port":null,"path":"/","query":null,"parameters":null,"fragment":null}

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
      3.141592653589793

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
      8

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`radians`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _pretty_print:

pretty_print(*str*)
^^^^^^^^^^^^^^^^^^^

  Pretty-print the given string

  **PRQL Name**: text.pretty

  **Parameters**
    * **str\*** --- The string to format

  **Examples**
    To pretty-print the string '{"scheme": "https", "host": "example.com"}':

    .. code-block::  custsqlite

      ;SELECT pretty_print('{"scheme": "https", "host": "example.com"}')
      {
          "scheme": "https",
          "host": "example.com"
      }

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
      3.141592653589793

  **See Also**
    :ref:`abs`, :ref:`acos`, :ref:`acosh`, :ref:`asin`, :ref:`asinh`, :ref:`atan2`, :ref:`atan`, :ref:`atanh`, :ref:`atn2`, :ref:`avg`, :ref:`ceil`, :ref:`degrees`, :ref:`exp`, :ref:`floor`, :ref:`log10`, :ref:`log`, :ref:`max`, :ref:`min`, :ref:`pi`, :ref:`power`, :ref:`round`, :ref:`sign`, :ref:`square`, :ref:`sum`, :ref:`total`

----


.. _raise_error:

raise_error(*msg*, *\[reason\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Raises an error with the given message when executed

  **Parameters**
    * **msg\*** --- The error message
    * **reason** --- The reason the error occurred

  **Examples**
    To raise an error if a variable is not set:

    .. code-block::  custsqlite

      ;SELECT ifnull($val, raise_error('please set $val', 'because'))
      ✘ error: please set $val
       reason: because
       --> raise_error:1
       | SELECT ifnull($val, raise_error('please set $val', 'because'))


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

  **PRQL Name**: fs.readlink

  **Parameters**
    * **path\*** --- The path to the symbolic link.

  **See Also**
    :ref:`basename`, :ref:`dirname`, :ref:`joinpath`, :ref:`realpath`

----


.. _realpath:

realpath(*path*)
^^^^^^^^^^^^^^^^

  Returns the resolved version of the given path, expanding symbolic links and resolving '.' and '..' references.

  **PRQL Name**: fs.realpath

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
                0             0 <NULL>                   3           1          4     a=1 
                0             1 <NULL>                   3           1          2       a 
                0             2 <NULL>                   3           3          4       1 
                1             0 <NULL>                   3           6          9     b=2 
                1             1 <NULL>                   3           6          7       b 
                1             2 <NULL>                   3           8          9       2 

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _regexp_capture_into_json:

regexp_capture_into_json(*string*, *pattern*, *\[options\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  A table-valued function that executes a regular-expression over a string and returns the captured values as a JSON object.  If the regex only matches a subset of the input string, it will be rerun on the remaining parts of the string until no more matches are found.

  **Parameters**
    * **string\*** --- The string to match against the given pattern.
    * **pattern\*** --- The regular expression to match.
    * **options** --- A JSON object with the following option: convert-numbers - True (default) if text that looks like numeric data should be converted to JSON numbers, false if they should be captured as strings.

  **Examples**
    To extract the key/value pairs 'a'/1 and 'b'/2 from the string 'a=1; b=2':

    .. code-block::  custsqlite

      ;SELECT * FROM regexp_capture_into_json('a=1; b=2', '(\w+)=(\d+)')
      match_index     content     
                0 {"col_0⋯l_1":1} 
                1 {"col_0⋯l_1":2} 

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _regexp_match:

regexp_match(*re*, *str*)
^^^^^^^^^^^^^^^^^^^^^^^^^

  Match a string against a regular expression and return the capture groups as JSON.

  **PRQL Name**: text.regexp_match

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_replace`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _regexp_replace:

regexp_replace(*str*, *re*, *repl*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Replace the parts of a string that match a regular expression.

  **PRQL Name**: text.regexp_replace

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_match`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _reverse:

reverse(*str*)
^^^^^^^^^^^^^^

  Returns the reverse of the given string.

  **PRQL Name**: text.reverse

  **Parameters**
    * **str\*** --- The string to reverse.

  **Examples**
    To reverse the string 'abc':

    .. code-block::  custsqlite

      ;SELECT reverse('abc')
      cba

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
      123

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
       msg_num   ex_procname    log_body     
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
    To trim the space characters from the end of the string 'abc   ':

    .. code-block::  custsqlite

      ;SELECT rtrim('abc   ')
      abc

    To trim the characters 'b' and 'c' from the string 'abbbbcccc':

    .. code-block::  custsqlite

      ;SELECT rtrim('abbbbcccc', 'bc')
      a

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _shell_exec:

shell_exec(*cmd*, *\[input\]*, *\[options\]*)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Executes a shell command and returns its output.

  **PRQL Name**: shell.exec

  **Parameters**
    * **cmd\*** --- The command to execute.
    * **input** --- A blob of data to write to the command's standard input.
    * **options** --- A JSON object containing options for the execution with the following properties:

      * **env** --- An object containing the environment variables to set or, if NULL, to unset.

  **See Also**
    

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

  **PRQL Name**: text.sparkline

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`date`, :ref:`datetime`, :ref:`humanize_duration`, :ref:`julianday`, :ref:`time`, :ref:`timediff`, :ref:`timeslice`, :ref:`timezone`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`date`, :ref:`datetime`, :ref:`humanize_duration`, :ref:`julianday`, :ref:`strftime`, :ref:`timediff`, :ref:`timeslice`, :ref:`timezone`

----


.. _timediff:

timediff(*time1*, *time2*)
^^^^^^^^^^^^^^^^^^^^^^^^^^

  Compute the difference between two timestamps in seconds

  **PRQL Name**: time.diff

  **Parameters**
    * **time1\*** --- The first timestamp
    * **time2\*** --- The timestamp to subtract from the first

  **Examples**
    To get the difference between two timestamps:

    .. code-block::  custsqlite

      ;SELECT timediff('2017-02-03T04:05:06', '2017-02-03T04:05:00')
      6

    To get the difference between relative timestamps:

    .. code-block::  custsqlite

      ;SELECT timediff('today', 'yesterday')
      86400

  **See Also**
    :ref:`date`, :ref:`datetime`, :ref:`humanize_duration`, :ref:`julianday`, :ref:`strftime`, :ref:`time`, :ref:`timeslice`, :ref:`timezone`

----


.. _timeslice:

timeslice(*time*, *slice*)
^^^^^^^^^^^^^^^^^^^^^^^^^^

  Return the start of the slice of time that the given timestamp falls in.  If the time falls outside of the slice, NULL is returned.

  **PRQL Name**: time.slice

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

      ;SELECT timeslice(log_time_msecs, '5m') AS slice, count(1)
    FROM lnav_example_log GROUP BY slice
           slice       count(1)  
      2017-02⋯:00.000          2 
      2017-02⋯:00.000          1 
      2017-02⋯:00.000          1 

    To group log messages by those before 4:30am and after:

    .. code-block::  custsqlite

      ;SELECT timeslice(log_time_msecs, 'before 4:30am') AS slice, count(1) FROM lnav_example_log GROUP BY slice
           slice       count(1)  
               <NULL>          1 
      2017-02⋯:00.000          3 

  **See Also**
    :ref:`date`, :ref:`datetime`, :ref:`humanize_duration`, :ref:`julianday`, :ref:`strftime`, :ref:`time`, :ref:`timediff`, :ref:`timezone`

----


.. _timezone:

timezone(*tz*, *ts*)
^^^^^^^^^^^^^^^^^^^^

  Convert a timestamp to the given timezone

  **PRQL Name**: time.to_zone

  **Parameters**
    * **tz\*** --- The target timezone
    * **ts\*** --- The source timestamp

  **Examples**
    To convert a time to America/Los_Angeles:

    .. code-block::  custsqlite

      ;SELECT timezone('America/Los_Angeles', '2022-03-02T10:00')
      2022-03-02T02:00:00.000000-0800

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`date`, :ref:`datetime`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`julianday`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`strftime`, :ref:`substr`, :ref:`time`, :ref:`timediff`, :ref:`timeslice`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
      17

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
    To trim spaces from the start and end of the string '    abc   ':

    .. code-block::  custsqlite

      ;SELECT trim('    abc   ')
      abc

    To trim the characters '-' and '+' from the string '-+abc+-':

    .. code-block::  custsqlite

      ;SELECT trim('-+abc+-', '-+')
      abc

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unparse_url`, :ref:`upper`, :ref:`xpath`

----


.. _unlikely:

unlikely(*value*)
^^^^^^^^^^^^^^^^^

  Short-hand for likelihood(X, 0.0625)

  **Parameters**
    * **value\*** --- The boolean value to return


----


.. _unparse_url:

unparse_url(*obj*)
^^^^^^^^^^^^^^^^^^

  Convert a JSON object containing the parts of a URL into a URL string

  **Parameters**
    * **obj\*** --- The JSON object containing the URL parts

  **Examples**
    To unparse the object '{"scheme": "https", "host": "example.com"}':

    .. code-block::  custsqlite

      ;SELECT unparse_url('{"scheme": "https", "host": "example.com"}')
      https://example.com/

  **See Also**
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`upper`, :ref:`xpath`

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`xpath`

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
          result       node_path  node_attr node_text 
      <def a=⋯</def>␊ /abc/def[1] {"a":"b"} Hello     
      <def>Bye</def>␊ /abc/def[2] {}        Bye       

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
    :ref:`anonymize`, :ref:`char`, :ref:`charindex`, :ref:`decode`, :ref:`encode`, :ref:`endswith`, :ref:`extract`, :ref:`group_concat`, :ref:`group_spooky_hash_agg`, :ref:`gunzip`, :ref:`gzip`, :ref:`humanize_duration`, :ref:`humanize_file_size`, :ref:`humanize_id`, :ref:`instr`, :ref:`leftstr`, :ref:`length`, :ref:`logfmt2json`, :ref:`lower`, :ref:`ltrim`, :ref:`padc`, :ref:`padl`, :ref:`padr`, :ref:`parse_url`, :ref:`pretty_print`, :ref:`printf`, :ref:`proper`, :ref:`regexp_capture_into_json`, :ref:`regexp_capture`, :ref:`regexp_match`, :ref:`regexp_replace`, :ref:`replace`, :ref:`replicate`, :ref:`reverse`, :ref:`rightstr`, :ref:`rtrim`, :ref:`sparkline`, :ref:`spooky_hash`, :ref:`startswith`, :ref:`strfilter`, :ref:`substr`, :ref:`timezone`, :ref:`trim`, :ref:`unicode`, :ref:`unparse_url`, :ref:`upper`

----


.. _yaml_to_json:

yaml_to_json(*yaml*)
^^^^^^^^^^^^^^^^^^^^

  Convert a YAML document to a JSON-encoded string

  **PRQL Name**: yaml.to_json

  **Parameters**
    * **yaml\*** --- The YAML value to convert to JSON.

  **Examples**
    To convert the document "abc: def":

    .. code-block::  custsqlite

      ;SELECT yaml_to_json('abc: def')
      {"abc": "def"}

  **See Also**
    :ref:`jget`, :ref:`json_array_length`, :ref:`json_array`, :ref:`json_concat`, :ref:`json_contains`, :ref:`json_each`, :ref:`json_extract`, :ref:`json_group_array`, :ref:`json_group_object`, :ref:`json_insert`, :ref:`json_object`, :ref:`json_quote`, :ref:`json_remove`, :ref:`json_replace`, :ref:`json_set`, :ref:`json_tree`, :ref:`json_type`, :ref:`json_valid`, :ref:`json`

----


.. _zeroblob:

zeroblob(*N*)
^^^^^^^^^^^^^

  Returns a BLOB consisting of N bytes of 0x00.

  **Parameters**
    * **N\*** --- The size of the BLOB.


----


.. _dot_dump:

;.dump *path* *table*
^^^^^^^^^^^^^^^^^^^^^

  Dump the contents of the database

  **Parameters**
    * **path\*** --- The path to the file to write
    * **table** --- The name of the table to dump

  **See Also**
    :ref:`append_to`, :ref:`dot_read`, :ref:`echo`, :ref:`echoln`, :ref:`export_session_to`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`redirect_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`

----


.. _dot_msgformats:

;.msgformats
^^^^^^^^^^^^

  Executes a query that will summarize the different message formats found in the logs


----


.. _dot_read:

;.read *path*
^^^^^^^^^^^^^

  Execute the SQLite statements in the given file

  **Parameters**
    * **path\*** --- The path to the file to write

  **See Also**
    :ref:`append_to`, :ref:`dot_dump`, :ref:`echo`, :ref:`echoln`, :ref:`export_session_to`, :ref:`pipe_line_to`, :ref:`pipe_to`, :ref:`redirect_to`, :ref:`write_csv_to`, :ref:`write_json_to`, :ref:`write_jsonlines_to`, :ref:`write_raw_to`, :ref:`write_screen_to`, :ref:`write_table_to`, :ref:`write_to`, :ref:`write_view_to`

----


.. _dot_schema:

;.schema *name*
^^^^^^^^^^^^^^^

  Switch to the SCHEMA view that contains a dump of the current database schema

  **Parameters**
    * **name\*** --- The name of a table to jump to


----


.. _prql_aggregate:

aggregate *expr*
^^^^^^^^^^^^^^^^

  PRQL transform to summarize many rows into one

  **Parameters**
    * **expr\*** --- The aggregate expression(s)

  **Examples**
    To group values into a JSON array:

    .. code-block::  custsqlite

      ;from [{a=1}, {a=2}] | aggregate { arr = json.group_array a }
      [1,2]

  **See Also**
    :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _prql_append:

append *table*
^^^^^^^^^^^^^^

  PRQL transform to concatenate tables together

  **Parameters**
    * **table\*** --- The table to use as a source

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _prql_derive:

derive *column*
^^^^^^^^^^^^^^^

  PRQL transform to derive one or more columns

  **Parameters**
    * **column\*** --- The new column

  **Examples**
    To add a column that is a multiplication of another:

    .. code-block::  custsqlite

      ;from [{a=1}, {a=2}] | derive b = a * 2
          a          b      
               1          2 
               2          4 

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _prql_filter:

filter *expr*
^^^^^^^^^^^^^

  PRQL transform to pick rows based on their values

  **Parameters**
    * **expr\*** --- The expression to evaluate over each row

  **Examples**
    To pick rows where 'a' is greater than one:

    .. code-block::  custsqlite

      ;from [{a=1}, {a=2}] | filter a > 1
      2

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _prql_from:

from *table*
^^^^^^^^^^^^

  PRQL command to specify a data source

  **Parameters**
    * **table\*** --- The table to use as a source

  **Examples**
    To pull data from the 'http_status_codes' database table:

    .. code-block::  custsqlite

      ;from http_status_codes | take 3
        status       message     
             100 Continue        
             101 Switchi⋯otocols 
             102 Processing      

    To use an array literal as a source:

    .. code-block::  custsqlite

      ;from [{ col1=1, col2='abc' }, { col1=2, col2='def' }]
         col1    col2 
               1 abc  
               2 def  

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _prql_group:

group *key_columns* *pipeline*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  PRQL transform to partition rows into groups

  **Parameters**
    * **key_columns\*** --- The columns that define the group
    * **pipeline\*** --- The pipeline to execute over a group

  **Examples**
    To group by log_level and count the rows in each partition:

    .. code-block::  custsqlite

      ;from lnav_example_log | group { log_level } (aggregate { count this })
      log_level  COUNT(*)  
      debug              1 
      info               1 
      warn               1 
      error              1 

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _prql_join:

join *\[side:inner\]* *table* *condition*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  PRQL transform to add columns from another table

  **Parameters**
    * **side** --- Specifies which rows to include
    * **table\*** --- The other table to join with the current rows
    * **condition\*** --- The condition used to join rows

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _prql_select:

select *expr*
^^^^^^^^^^^^^

  PRQL transform to pick and compute columns

  **Parameters**
    * **expr\*** --- The columns to include in the result set

  **Examples**
    To pick the 'b' column from the rows:

    .. code-block::  custsqlite

      ;from [{a=1, b='abc'}, {a=2, b='def'}] | select b
       b  
      abc 
      def 

    To compute a new column from an input:

    .. code-block::  custsqlite

      ;from [{a=1}, {a=2}] | select b = a * 2
          b      
               2 
               4 

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _prql_sort:

sort *expr*
^^^^^^^^^^^

  PRQL transform to sort rows

  **Parameters**
    * **expr\*** --- The values to use when ordering the result set

  **Examples**
    To sort the rows in descending order:

    .. code-block::  custsqlite

      ;from [{a=1}, {a=2}] | sort {-a}
          a      
               2 
               1 

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _stats_average_of:

stats.average_of *col*
^^^^^^^^^^^^^^^^^^^^^^

  Compute the average of col

  **Parameters**
    * **col\*** --- The column to average

  **Examples**
    To get the average of a:

    .. code-block::  custsqlite

      ;from [{a=1}, {a=1}, {a=2}] | stats.average_of a
      1.3333333333333333

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _stats_by:

stats.by *col* *values*
^^^^^^^^^^^^^^^^^^^^^^^

  A shorthand for grouping and aggregating

  **Parameters**
    * **col\*** --- The column to sum
    * **values\*** --- The aggregations to perform

  **Examples**
    To partition by a and get the sum of b:

    .. code-block::  custsqlite

      ;from [{a=1, b=1}, {a=1, b=1}, {a=2, b=1}] | stats.by a {sum b}
          a      COALESC⋯(b), 0) 
               1               2 
               2               1 

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _stats_count_by:

stats.count_by *column*
^^^^^^^^^^^^^^^^^^^^^^^

  Partition rows and count the number of rows in each partition

  **Parameters**
    * **column** --- The columns to group by

  **Examples**
    To count rows for a particular value of column 'a':

    .. code-block::  custsqlite

      ;from [{a=1}, {a=1}, {a=2}] | stats.count_by a
          a        total    
               1          2 
               2          1 

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _stats_hist:

stats.hist *col* *\[slice:'1h'\]* *\[top:10\]*
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Count the top values per bucket of time

  **Parameters**
    * **col\*** --- The column to count
    * **slice** --- The time slice
    * **top** --- The limit on the number of values to report

  **Examples**
    To chart the values of ex_procname over time:

    .. code-block::  custsqlite

      ;from lnav_example_log | stats.hist ex_procname
          tslice             v        
      2017-02⋯:00.000 {"gw":3,"hw":1} 

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _stats_sum_of:

stats.sum_of *col*
^^^^^^^^^^^^^^^^^^

  Compute the sum of col

  **Parameters**
    * **col\*** --- The column to sum

  **Examples**
    To get the sum of a:

    .. code-block::  custsqlite

      ;from [{a=1}, {a=1}, {a=2}] | stats.sum_of a
      4

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`utils_distinct`

----


.. _prql_take:

take *n_or_range*
^^^^^^^^^^^^^^^^^

  PRQL command to pick rows based on their position

  **Parameters**
    * **n_or_range\*** --- The number of rows or range

  **Examples**
    To pick the first row:

    .. code-block::  custsqlite

      ;from [{a=1}, {a=2}, {a=3}] | take 1
      1

    To pick the second and third rows:

    .. code-block::  custsqlite

      ;from [{a=1}, {a=2}, {a=3}] | take 2..3
          a      
               2 
               3 

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`, :ref:`utils_distinct`

----


.. _utils_distinct:

utils.distinct *col*
^^^^^^^^^^^^^^^^^^^^

  A shorthand for getting distinct values of col

  **Parameters**
    * **col\*** --- The column to sum

  **Examples**
    To get the distinct values of a:

    .. code-block::  custsqlite

      ;from [{a=1}, {a=1}, {a=2}] | utils.distinct a
          a      
               1 
               2 

  **See Also**
    :ref:`prql_aggregate`, :ref:`prql_append`, :ref:`prql_derive`, :ref:`prql_filter`, :ref:`prql_from`, :ref:`prql_group`, :ref:`prql_join`, :ref:`prql_select`, :ref:`prql_sort`, :ref:`prql_take`, :ref:`stats_average_of`, :ref:`stats_by`, :ref:`stats_count_by`, :ref:`stats_hist`, :ref:`stats_sum_of`

----

