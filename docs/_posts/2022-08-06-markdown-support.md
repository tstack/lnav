---
layout: post
title: Markdown Support
excerpt: A side effect of fancier help text
---

*(This change will be in the upcoming v0.11.0 release)*

As part of the effort to polish the lnav TUI, I wanted to make the builtin
help text look a bit nicer. The current help text is a plain text file with
some ANSI escape sequences for colors. It's not easy to write or read. Since
Markdown has become a dominant way to write this type of document, I figured
I could use that and have the side benefit of allowing lnav to read Markdown
docs. Fortunately, the [MD4C](https://github.com/mity/md4c) library exists.
This library provides a nice event-driven parser for documents instead of
just converting directly to HTML. In addition, document structure is now
shown/navigable through the new breadcrumb bar at the top. I think the
result is pretty nice:

<script id="asciicast-2hx3UiyzOHQXBQOBf31ztKvHc"
        src="https://asciinema.org/a/2hx3UiyzOHQXBQOBf31ztKvHc.js"
        async>
</script>

## Viewing Markdown Files

Files with an `.md` suffix will be considered as Markdown and will be
parsed as such. As an example, here is lnav displaying its README.md file:

<script id="asciicast-iw4rwddZNGCe3v8DyOfItERG9"
        src="https://asciinema.org/a/iw4rwddZNGCe3v8DyOfItERG9.js"
        async>
</script>
