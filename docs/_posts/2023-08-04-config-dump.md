---
layout: post
title: Tooling for troubleshooting configuration
excerpt: Getting the final configuration and the sources of values
---

*(This change is in **v0.12.0+**)*

Inspired by [this blog post about reporting configuration file locations](https://utcc.utoronto.ca/~cks/space/blog/sysadmin/ReportConfigFileLocations)
and the [ensuing HackerNews commentary](https://news.ycombinator.com/item?id=36465886).
I've added the `config get` and `config blame` management commands for getting the
final configuration and the source of each property in the configuration, respectively.
I had previously added the file locations used by **lnav** in the `lnav -h` output as
recommended by the blog post.  But, the HN comments made a good case for adding the
the other troubleshooting tooling as well.

If you would like to try out these new commands, you need to run lnav with the `-m`
option to switch to the "management" mode.  For example, just running lnav with this
flag will print out the available operations:

```console
$ lnav -m
✘ error: expecting an operation to perform
 = help: the available operations are:
          • config: perform operations on the lnav configuration
          • format: perform operations on log file formats
          • piper: perform operations on piper storage
          • regex101: create and edit log message regular expressions using regex101.com
```

Executing `config get` will print out the final configuration that lnav is operating
with as JSON:

```console
$ lnav -m config get
```

If you would like to know the source of the value for each property, you can use
the `config blame` command, like so:

```console
$ lnav -m config blame | tail
/ui/theme-defs/solarized-light/vars/black -> solarized-light.json:15
/ui/theme-defs/solarized-light/vars/blue -> solarized-light.json:21
/ui/theme-defs/solarized-light/vars/cyan -> solarized-light.json:22
/ui/theme-defs/solarized-light/vars/green -> solarized-light.json:23
/ui/theme-defs/solarized-light/vars/magenta -> solarized-light.json:19
/ui/theme-defs/solarized-light/vars/orange -> solarized-light.json:17
/ui/theme-defs/solarized-light/vars/red -> solarized-light.json:18
/ui/theme-defs/solarized-light/vars/semantic_highlight_color -> solarized-light.json:24
/ui/theme-defs/solarized-light/vars/violet -> solarized-light.json:20
/ui/theme-defs/solarized-light/vars/yellow -> solarized-light.json:16
```

In the above output, "solarized-light.json" file is built into the lnav
executable and is not from the file system.
