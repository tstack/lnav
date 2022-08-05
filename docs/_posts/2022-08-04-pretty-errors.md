---
layout: post
title: Pretty error messages
excerpt: Error message improvements
---

*(This change will be in the upcoming v0.11.0 release)*

Taking a page from compilers like rustc, I've spent some time
improving error messages to make them look nicer and be more
helpful. Fortunately, SQLite has improved their error reporting
as well by adding
[sqlite3_error_offset()](http://sqlite.com/c3ref/errcode.html).
This function can point to the part of the SQL statement that
was in error. As an example of the improvement, a SQL file
that contained the following content:

```sql

-- This is a test
SELECT abc),
rtrim(def)
FROM mytable;
```

Would report an error like the following on startup in v0.10.1:

```text
error:/Users/tstack/.config/lnav/formats/installed/test.sql:2:near ")": syntax error
```

Now, you will get a clearer error message with a syntax-highlighted
code snippet and a pointer to the part of the code that has the
problem:

![Screenshot of a SQL error](/assets/images/lnav-sql-error-msg.png)

Inside the TUI, a panel has been added at the bottom to display these
long-form error messages. The panel will disappear after a short
time or when input is received. Here is an example showing an error
for an invalid regular expression:

<script id="asciicast-lmYMLZsB02WbSO8VEz4aVLXa1"
        src="https://asciinema.org/a/lmYMLZsB02WbSO8VEz4aVLXa1.js"
        async>
</script>
