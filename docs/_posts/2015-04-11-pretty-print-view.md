---
layout: post
title: "Pretty-print view in v0.7.3"
date:   2015-04-11 00:00:00
excerpt: Automatically reformat structured data with "SHIFT+P".
---

I wanted to call out the pretty-print feature in the latest release of lnav.
This idea came from a coworker of Suresh who was having a hard time trying to
read some unformatted XML in a log. They wanted the XML pretty-printed and were
hoping that could be done by just piping the message to xmlpp or the like. So,
first we implemented the 'pipe-to' and 'pipe-line-to' commands that will let you
pipe log messages to a command and then display the result inside of lnav. That
worked well enough, but pretty-printing is such a frequent operation that having
to execute a command was kind of a pain. It would also be nice if it worked for
a variety of text, like JSON or Python data. The solution we came up with was to
leverage the existing code for parsing log messages to create a simple
pretty-printer that should work for most data formats. Another benefit is that
the log message does not have to be well-formed for the printer to work, any
leading or trailing garbage shouldn't confuse things.

As an example, here is a screenshot of the log message with the unformatted XML
text with word-wrapping turned on:

![Screenshot of raw XML](/assets/images/lnav-before-pretty.png)

That's not very easy to read and it's hard to figure out the structure of the
message. Now, here is that same message after pressing SHIFT+P to switch to the
pretty-print view of lnav:

![Screenshot of pretty-printed XML](/assets/images/lnav-after-pretty.png)

The XML text is indented nicely and the usual syntax highlighting is applied.
Also notice that lnav will automatically try to lookup the DNS name for IP
addresses. Overall, I think it's a major improvement over the raw view.

This is a pretty simple feature but I have found it quite useful in the couple
weeks that it has been implemented. It's so useful that I'm kicking myself for
not having thought of it before.
