---
title: lnav external-access server
---

# lnav External Access Server

The Logfile Navigator, *lnav* for short, is a log file viewer for the terminal.
This server provides a REST API that can be used to execute lnav scripts and
retrieve information about the current state of lnav's TUI.
It is intended to be used by other applications to remotely control an lnav
instance.
In addition, "apps" can also be installed to provide customized
interfaces for lnav functionality.

See the [External Access](https://docs.lnav.org/en/latest/extacc.html)
documentation for more details.

### Installed Apps

The following apps are currently installed:

``` { .lnav .eval-and-replace }
;SELECT
    group_concat(format('<li><a href="/apps/%s/">%s</a> - %s</li>',
                        name,
                        name,
                        encode(description, 'html')),
                 char(10)) AS items
   FROM lnav_apps
:echo
<ul>
${items}
</ul>
```
