---
layout: post
title: "Support for tagging and commenting on log messages"
excerpt: Annotate log messages with your thoughts.
---

*(This change is in v0.8.4+)*

If you have been wanting to add notes to log messages you might be interested
in, the new [`:comment`](https://docs.lnav.org/en/latest/commands.html#comment-text)
and [`:tag`](https://docs.lnav.org/en/latest/commands.html#tag-tag) commands.
These commands add a comment or tag(s) to the top message in the log view.
The comments and tags are saved in the session, so they will be restored
automatically when the file is reopened.  These annotations can be searched
for using the regular search prompt and can be accessed in the
[log tables](https://docs.lnav.org/en/latest/sqlext.html#log-tables) using the
`log_tags` and `log_comment` columns.

<script id="asciicast-yRTcQd2VMv3QZVs5597OyAAxI"
        src="https://asciinema.org/a/yRTcQd2VMv3QZVs5597OyAAxI.js"
        async>
</script>
