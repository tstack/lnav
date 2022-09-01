---
layout: post
title: Playground and Tutorial
excerpt: Try lnav without having to install anything
---

To make it easier to try out **lnav**, I've deployed an ssh-based playground
and tutorial.  You can SSH as follows to try them out:

```console
$ ssh playground@demo.lnav.org
$ ssh tutorial1@demo.lnav.org
```

<script id="asciicast-HiiUMMmRKZh0uCVKm1Uw8WLlw"
        src="https://asciinema.org/a/HiiUMMmRKZh0uCVKm1Uw8WLlw.js"
        async>
</script>

The playground has a couple of example logs to play with.  The tutorial
tries to guide you through the basics of navigating log files with lnav.
The server is running on the free-tier of [fly.io](https://fly.io), so
please be kind.

This effort was inspired by the `git.charm.sh` SSH server and by the
[fasterthanli.me](https://fasterthanli.me/articles/remote-development-with-rust-on-fly-io)
post on doing remote development on fly.io.
