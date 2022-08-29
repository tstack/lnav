---
name: tutorial1
steps:
  - move-to-error:
      description: "Move to an error"
      view_ptr: /top
      view_value: 6
      notification: "Press e/Shift+E to move through the errors"
      comment: |
        You found the error!
        [Log formats](https://docs.lnav.org/en/latest/formats.html#format-file-reference)
        can define the log levels for a given message.
        The [theme](https://docs.lnav.org/en/latest/config.html#theme-definitions) defines
        how the levels are displayed.
    move-to-warning:
      description: "Move to a warning"
      notification: "Press w/Shift+W to move through the warnings"
      view_ptr: /top
      view_value: 3
      comment: |
        You found the warning! The scrollbar on the right is highlighted
        to show the position of
        <span class="-lnav_log-level-styles_warning">warnings</span> and
        <span class="-lnav_log-level-styles_error">errors</span> in this
        view.
  - search-for-term:
      description: "Search for something"
      notification: "Press / to search for '1AF9...'"
      view_ptr: /search
      view_value: 1AF9293A-F42D-4318-BCDF-60234B240955
    move-to-next-hit:
      description: "Move to the next hit"
      notification: "Press n/Shift+N to move through the search hits"
      view_ptr: /top
      view_value: 53
      comment: |
        The matching text in a search is highlighted in
        <span class="-lnav_styles_search">reverse-video</span>.
        However, the text is not always on-screen, so the bar on the
        left will also be highlighted. You can then press `>` to
        move right to the next (horizontal) search hit. Pressing
        `<` will move left to the previous (horizontal) hit or all
        the way back to the start of the line.
    move-right:
      description: "Move to the right"
      notification: "Press > to move horizontally to view the search hit"
      view_ptr: /left
      view_value: 150
---
# Tutorial 1

Welcome to the first _interactive_ **lnav** tutorial!

This tutorial will guide you through the basics of navigating log files.

## Step 1

Finding errors quickly is one of the main use-cases for **lnav**.  To
make that quick and easy, **lnav** parses the log messages in log files
as they are loaded and builds indexes of the errors and warnings.  You
can then use the following hotkeys to jump to them in the log view:

| Key       | Action                                                                           |
|-----------|----------------------------------------------------------------------------------|
| `e`       | Move to the next <span class="-lnav_log-level-styles_error">error</span>         |
| `Shift+E` | Move to the previous <span class="-lnav_log-level-styles_error">error</span>     |
| `w`       | Move to the next <span class="-lnav_log-level-styles_warning">warning</span>     |
| `Shift+W` | Move to the previous <span class="-lnav_log-level-styles_warning">warning</span> |

To complete this step in the tutorial, you'll need to navigate to the
errors and warnings in the sample log file. You can check the upper-right
status bar for tips on what you need to do next. Now, press `q` to switch
to the log view and begin navigating the sample log file.

## Step 2

To search for text in files, you can press `/` to enter the search
prompt.  To make it easier to search for text that is on-screen, you
can press `TAB` to complete values that are shown on screen.  For
example, to search for the UUID "1AF9293A-F42D-4318-BCDF-60234B240955"
that is in one of the error messages, you can enter "1AF9" and then
press `TAB` to complete the rest of the UUID.

Press `q` to switch to the log view and try searching for the UUID.

## Conclusion

That's all for now, visit https://lnav.org/downloads to find how to
download/install a copy of lnav for your system.  The full documentation
is available at https://docs.lnav.org.

## Colophon

The source for this tutorial is available here:

https://github.com/tstack/lnav/tree/master/docs/tutorial/tutorial1
