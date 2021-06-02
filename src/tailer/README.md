# Tailer

This directory contains the functionality for monitoring
[remote files](https://docs.lnav.org/en/latest/usage.html#remote-files).  The
name "tailer" refers to the binary that is transferred to the remote host that
takes care of tailing files and sending the contents back to the host that is
running the main lnav binary.  To ease integration with lnav's existing
functionality, the remote files are mirrored locally.  The tailer also
supports interactive use by providing previews of file contents and
TAB-completion possibilities.

## Files

The important files in this directory are:

- [tailer.main.c](tailer.main.c) - The main() implementation for the tailer.
- [tailer.looper.hh](tailer.looper.hh) - The service in the main lnav binary
  that transfers tailers to hosts and communicates with them.
- [tailer.h](tailer.h) and [tailerpp.hh](tailerpp.hh) - Utility libraries for
  the tailer protocol.
- tailer.ape - The [αcτµαlly pδrταblε εxεcµταblε](https://justine.lol/ape.html)
  build of the tailer.  This binary is produced by a GitHub Action and checked
  in so the build process doesn't need to be supported on lots of platforms.

## Flow

When a remote-path is passed to lnav, the
[file_collection.hh](../file_collection.hh) logic forwards the request to the
`tailer::looper` service.  This service makes two connections to the remote
host using the `ssh` command so that the user's custom configurations will be
used.  The first connection is used to transfer the "tailer.ape" binary and
make it executable.  The second connection starts the tailer and uses
stdin/stdout for a binary protocol and stderr for logging.  The tailer then
waits for requests to open files, preview files, and get possible paths for
TAB-completions.
