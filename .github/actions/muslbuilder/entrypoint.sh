#!/bin/sh

# If arguments are provided, execute them; otherwise, open a shell.
if [ "$#" -gt 0 ]; then
    exec "$@"
else
    exec sh
fi
