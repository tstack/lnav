#!/usr/bin/env python3

import os

os.makedirs("naughty", 0o700, True)
with open('naughty/file-with-escape-sequences-\x1b[31mred\x1b[m-color.txt', 'w+') as fi:
    fi.write('boo')

with open('naughty/file-with-escape-sequences-\x1b[31mred\x1b[m-color.log', 'w+') as fi:
    fi.write('2012-07-02 10:22:40,672:DEBUG:foo bar baz\n')

try:
    with open(b'naughty/text-file-with-invalid-utf-\xc3\x28', 'w+') as fi:
        fi.write('boo')

    with open(b'naughty/log-file-with-invalid-utf-\xc3\x28', 'w+') as fi:
        fi.write('2015-04-24T21:09:39.296 25376]ERROR:somemodule:Something very INFOrmative.')
except OSError as e:
    pass

with open('naughty/file-with-hidden-text.txt', 'w+') as fi:
    fi.write('Hello, \x1b[30;40mWorld!\x1b[m!\n')
    fi.write('Goodbye, \x1b[37;47mWorld!\x1b[m!\n')
    fi.write('That is not\b\b\ball\n')

with open('naughty/file-with-terminal-controls.txt', 'w+') as fi:
    fi.write('time for a reset \x1bckapow\n')
    fi.write('ding dong! \x07\n')
