---
layout: post
title: "Visual filter editor"
excerpt: A friendlier way to interact with filters.
---

*(This change is in v0.8.5+)*

A visual filter editor has been added to make it easier to create, edit,
enable, disable, and delete filters.  In the log or text views, pressing `TAB`
will open the filter editor panel.  While the panel is in focus, the following
hotkeys can be used:

- `i` - Create an IN filter that will only show lines that match the given
        regular expression.
- `o` - Create an OUT filter that will hide lines that match the given regular
        expression.
- `Space` - Toggle the filter between being enabled and disabled.
- `Enter` - Edit the selected filter.
- `Shift+D` - Delete the filter.
- `t` - Switch a filter from an IN to an OUT or vice-versa.
- `f` - Globally enable or disable filtering.

When editing a filter, the main view will highlight lines that portion of the
lines that match the given regular expression:

- Lines that match an OUT filter are highlighted with red;
- Lines that match an IN filter are highlighted with green.

You can also press `TAB` to complete words that are visible in the main view.

<script id="asciicast-tcHeLbqVImRVcxWTYIrm3v6bw"
        src="https://asciinema.org/a/tcHeLbqVImRVcxWTYIrm3v6bw.js"
        async>
</script>
