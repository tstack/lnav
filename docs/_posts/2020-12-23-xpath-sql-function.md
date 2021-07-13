---
layout: post
title: Drilling down into XML snippets
excerpt: The new "xpath()" table-valued SQL function.
---

*(This change is in [**v0.10.0+**](https://github.com/tstack/lnav/releases/tag/v0.10.0-beta1))*

XML snippets in log messages can now be queried using the
[`xpath()`](https://docs.lnav.org/en/latest/sqlext.html#xpath-xpath-xmldoc)
table-valued SQL function.  The function takes an
[XPath](https://developer.mozilla.org/en-US/docs/Web/XPath), the XML snippet
to be queried, and returns a table with the results of the XPath query.
For example, given following XML document:

```xml
<msg>Hello, World!</msg>
```

Extracting the text value from the `msg` node can be done using the following
query:

```sql
SELECT result FROM xpath('/msg/text()', '<msg>Hello, World!</msg>')
```

Of course, you won't typically be passing XML values as string literals, you
will be extracting them from log messages.  Assuming your log format already
extracts the XML data, you can do a `SELECT` on the log format table and join
that with the `xpath()` call.  Since it can be challenging to construct a
correct `xpath()` call, lnav will suggest calls for the nodes it finds in any
XML log message fields.  The following asciicast demonstrates this flow:

<script id="asciicast-x89mrk8JPHBmB4pTbaZvTt8Do"
        src="https://asciinema.org/a/x89mrk8JPHBmB4pTbaZvTt8Do.js"
        async>
</script>

The implementation uses the [pugixml](https://pugixml.org) library.
