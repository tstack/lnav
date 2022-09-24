---
layout: post
title: VSCode Extension for lnav
excerpt: Syntax highlighting for lnav scripts
---

I've published a simple [Visual Studio Code extension for lnav](
https://marketplace.visualstudio.com/items?itemName=lnav.lnav)
that adds syntax highlighting for scripts.  The following is a
screenshot showing the `dhclient-summary.lnav` script that is
builtin:

![Screenshot of an lnav script](/assets/images/lnav-vscode-extension.png)

The lnav commands, those prefixed with colons, are marked as
keywords and the SQL blocks are treated as an embedded language
and highlighted accordingly.

If people find this useful, we can take it further and add
support for running the current script/snippet in a new lnav
process or even talking to an existing one.
