---
layout: post
title: Cursor Mode
excerpt: Move around the main view using a cursor
---

*(This change is in [**v0.11.2+**](https://github.com/tstack/lnav/releases/tag/v0.11.2-rc3))*

The major change in the v0.11.2 release is the addition of a "cursor mode"
for the main view.  Instead of focusing on the top line for interacting
with **lnav**, a cursor line is displayed and interactions focus on that.
The arrow keys and the hotkeys that jump between bookmarks, like search
hits and errors, now move the focused line instead of scrolling the view.
To help provide context for what you're looking at, large jumps will keep
the focused line in the middle of the view.  Smaller movements, like
moving the cursor above the top line, will scroll the view a small amount
so as not to be jarring.

You can enable/disable cursor mode interactively by pressing `CTRL` + `x`.
Or, you can permanently enable cursor mode by running the following
`:config` command:

```
:config /ui/movement/mode cursor
```

<script async
        id="asciicast-d94CmxlGM01I0L5HNn9qDn917"
        src="https://asciinema.org/a/d94CmxlGM01I0L5HNn9qDn917.js">
</script>
