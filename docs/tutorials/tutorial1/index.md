---
name: tutorial1
steps:
  - move-to-error:
      description: "Move to an error"
      view_ptr: /selection
      view_value: 6
      notification: |
        Press `e`/`Shift+E` to move through the
        <span class="-lnav_log-level-styles_error">errors</span>
      comment: |
        You found the error!
        [Log formats](https://docs.lnav.org/en/latest/formats.html#format-file-reference)
        can define the log levels for a given message.
        The [theme](https://docs.lnav.org/en/latest/config.html#theme-definitions) defines
        how the levels are displayed.
    move-to-warning:
      description: "Move to a warning"
      notification: |
        Press `w`/`Shift+W` to move through the
        <span class="-lnav_log-level-styles_warning">warnings</span>
      view_ptr: /selection
      view_value: 3
      comment: |
        You found the warning! The scrollbar on the right is highlighted
        to show the position of
        <span class="-lnav_log-level-styles_warning">warnings</span> and
        <span class="-lnav_log-level-styles_error">errors</span> in this
        view.
  - search-for-term:
      description: "Search for something"
      notification: "Press `/` to search for '1AF9...'"
      view_ptr: /search
      view_value: 1AF9293A-F42D-4318-BCDF-60234B240955
    move-to-next-hit:
      description: "Move to the next hit"
      notification: "Press `n`/`Shift+N` to move through the search hits"
      view_ptr: /selection
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
      notification: "Press `>` to move horizontally to view the search hit"
      view_ptr: /left
      view_value: 582
  - move-to-half-hour:
      description: "Move to the next half-hour"
      notification: "Press `3`/`Shift+3` to move through the half-hour marks"
      view_ptr: /selection
      view_value: 34
      comment: |
        This file is in the _glog_ format and timestamps consist of the
        year, month, and day squished together.  This log message's
        timestamp is March 22nd, 2017.  You can see the timestamp for
        the top line in the view in the breadcrumb bar.  Next, go to the
        log messages for the following day using `:goto March 23` or the
        breadcrumb bar above.
    move-to-timestamp:
      description: "Move to a given timestamp"
      notification: "Move to '**March 23**' using `:goto` or the breadcrumb bar"
      view_ptr: /selection
      view_value: 79
      comment: |
        Many different timestamp formats are recognized as well as
        relative times, like `+1h` or `-2h`.
---
# Tutorial 1

Welcome to the first _interactive_ **lnav** tutorial!

This tutorial will guide you through the basics of navigating log files.
Pressing `q` will display an example log file to try out commands on.
Pressing `y` will return you to the next step in the tutorial.

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
↗↗↗ status bar for tips on what you need to do next. Now, press `q` to
switch to the log view and begin navigating the sample log file.

## Step 2

To search for text in files, you can press `/` to enter the search
prompt.  To make it easier to search for text that is on-screen, you
can press `TAB` to complete values that are shown on screen.  For
example, to search for the UUID "1AF9293A-F42D-4318-BCDF-60234B240955"
that is in one of the error messages, you can enter "1AF9" and then
press `TAB` to complete the rest of the UUID.

Press `q` to switch to the log view and try searching for the UUID.

## Step 3

To move to a particular time in the logs, you have a few options:

* The number keys can be used to move to messages at the ten-minute
  marks within an hour.  For example, pressing `2` will move to the
  first message after the next twenty-minute mark, pressing `3`
  will move to the next half-hour mark, and so on.
* Pressing `` ` `` to focus on the breadcrumb bar, then you
  can press `TAB` (or right-arrow) to move to the time crumb.
  With the time crumb selected, you can then type in an absolute
  or relative time.  Or, you can use the up and down arrow keys
  to select a preset relative time.
* Pressing `:` will activate the command prompt, then you can use
  the `:goto` command to move to a given timestamp (or line number).

Press `q` to switch to the log view and try moving to different
times.

## Conclusion

That's all for now, thanks for your time! Visit the
[downloads](https://lnav.org/downloads) page to find out how to
download or install **lnav** for your system. The full
documentation is available at https://docs.lnav.org

Press `q` to switch to the log view and then press `q` again to
exit **lnav**.

## Colophon

The source for this tutorial is available here:

https://github.com/tstack/lnav/tree/master/docs/tutorials/
