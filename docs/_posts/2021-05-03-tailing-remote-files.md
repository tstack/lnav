---
layout: post
title: Tailing files on remote hosts
excerpt: Native support for tailing logs on machines accessible via SSH
---

*(This change is in [**v0.10.0+**](https://github.com/tstack/lnav/releases/tag/v0.10.0-beta1))*

One of the new features in the upcoming v0.10.0 release of lnav is support
for tailing log files on remote hosts via SSH.  This feature allows you to
view local files and files from multiple remote hosts alongside each other
in the log view.  The only setup required is to ensure the machines can be
accessed via SSH without any interaction, meaning the host key must have
been previously accepted and public key authentication configured.  Opening
a remote file is then simply a matter of specifying the location using the
common scp syntax (i.e. `user@host:/path/to/file`).

When lnav accesses a remote host, it transfers an agent (called the
"tailer") to the host to handle file system requests from lnav.  The agent
is an [αcτµαlly pδrταblε εxεcµταblε](https://justine.lol/ape.html) that
should run on most X86 Operating Systems.  The agent will monitor the
files of interest and synchronize their contents back to the host machine.
In addition, the agent can be used to satisfy interactive requests for
TAB-completion of remote file paths and previewing directory and file
contents.

The following asciicast shows lnav opening log files on MacOS and FreeBSD:

<script id="asciicast-fblzf1Ir5Rr0b5wMGEJBb95ye"
        src="https://asciinema.org/a/fblzf1Ir5Rr0b5wMGEJBb95ye.js"
        async>
</script>
