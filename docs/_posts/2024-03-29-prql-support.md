---
layout: post
title: Support for the PRQL in the database query prompt
excerpt: >-
  PRQL is a database query language that is pipeline-oriented
  and easier to use interactively
---

The v0.12.1 release of lnav includes support for
[PRQL](https://prql-lang.org).  PRQL is a database query language
that has a pipeline-oriented syntax.  The main advantage of PRQL,
in the context of lnav, is that it is easier to work with
interactively compared to SQL.  For example, lnav can provide
previews of different stages of the pipeline and provide more
accurate tab-completions for the columns in the result set.  I'm
hoping that the ease-of-use will make doing log analysis in lnav
much easier.

You can execute a PRQL query using the existing database prompt
(press `;`).  A query is interpreted as PRQL if it starts with
the [`from`](https://prql-lang.org/book/reference/data/from.html)
keyword.  After `from`, the database table should be provided.
The table for the focused log message will be suggested by default.
You can accept the suggestion by pressing TAB.  To add a new stage
to the pipeline, enter a pipe symbol (`|`), followed by a
[PRQL transform](https://prql-lang.org/book/reference/stdlib/transforms/index.html)
and its arguments.  In addition to the standard set of transforms,
lnav provides some convenience transforms in the `stats` and `utils`
namespaces.  For example, `stats.count_by` can be passed one or more
column names to group by and count, with the result sorted by most
to least.

As you enter a query, lnav will update various panels on the display
to show help, preview data, and errors.  The following is a
screenshot of lnav viewing a web access log with a query in progress:

![Screenshot of PRQL in action](/assets/images/lnav-prql-preview.png)

The top half is the usual log message view.  Below that is the online
help panel showing the documentation for the `stats.count_by` PRQL
function.  lnav will show the help for what is currently under the
cursor.  The next panel shows the preview data for the pipeline stage
that precedes the stage where the cursor is.  In this case, the
results of `from access_log`, which is the contents of the access
log table.  The second preview window shows the result of the
pipeline stage where the cursor is located.

There is still a lot of work to be done on the integration and PRQL
itself, but I'm very hopeful this will work out well in the long
term.  Many thanks to the PRQL team for starting the project and
keeping it going, it's not easy competing with SQL.
