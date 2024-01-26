#! /usr/bin/env python3

# Copyright (c) 2013, Timothy Stack
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
# * Neither the name of Timothy Stack nor the names of its contributors
# may be used to endorse or promote products derived from this software
# without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import string
import readline
import itertools
import collections

TEST_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.dirname(TEST_DIR)
SRC_DIR = os.path.join(ROOT_DIR, "src")

addr_to_name = {}
name_to_addr = {}
element_lists = collections.defaultdict(list)
list_depth = {}
list_format = {}
breakpoints = set()


def completer(text, state):
    options = [x for x in itertools.chain(name_to_addr,
                                          element_lists,
                                          breakpoints)
               if x.startswith(text)]
    try:
        return options[state]
    except IndexError:
        return None


readline.set_completer(completer)

if 'libedit' in readline.__doc__:
    readline.parse_and_bind('bind ^I rl_complete')
else:
    readline.parse_and_bind('tab: complete')

input_line = ''
ops = []
for line in open("scanned.dpt"):
    if line.startswith("input "):
        input_line = line[6:-1]
    else:
        ops.append([x.strip() for x in line.split()])


def getstr(capture):
    start, end = capture.split(':')
    return input_line[int(start):int(end)]


def printlist(name_or_addr):
    if name_or_addr in name_to_addr:
        addr = name_to_addr[name_or_addr]
        print("% 3d (%s:%s) %s" % (list_depth.get(addr, -1), name_or_addr, addr, element_lists[addr]))
    elif name_or_addr in element_lists:
        addr = name_or_addr
        print("% 3d (%s:%s) %s" % (list_depth.get(name_or_addr, -1),
                                   addr_to_name.get(name_or_addr, name_or_addr),
                                   name_or_addr,
                                   element_lists[name_or_addr]))
    else:
        print("error: unknown list --", name_or_addr)

    if addr in list_format:
        print("    format -- appender(%s) term(%s) qual(%s) sep(%s) prefix_term(%s)" % tuple(list_format[addr]))


def handleop(fields):
    addr = fields[0]
    loc = fields[1].split(':')
    method_name = fields[2]
    method_args = fields[3:]

    if addr == '0x0':
        el = None
    else:
        el = element_lists[addr]

    if method_name == 'element_list_t':
        addr_to_name[addr] = method_args[0]
        name_to_addr[method_args[0]] = addr
        list_depth[addr] = int(method_args[1])
    elif method_name == '~element_list_t':
        del element_lists[addr]
    elif method_name == 'format':
        list_depth[addr] = int(method_args[0])
        list_format[addr] = method_args[1:]
    elif method_name == 'consumed':
        list_depth[addr] = -1
    elif method_name == 'push_back':
        el.append((method_args[0], getstr(method_args[1])))
    elif method_name == 'push_front':
        el.insert(0, (method_args[0], getstr(method_args[1])))
    elif method_name == 'pop_front':
        el.pop(0)
    elif method_name == 'pop_back':
        el.pop()
    elif method_name == 'clear2':
        el[::] = []
    elif method_name == 'splice':
        pos = int(method_args[0])
        other = element_lists[method_args[1]]
        start, from_end = list(map(int, method_args[2].split(':')))
        end = len(other) - from_end
        sub_list = other[start:end]
        del other[start:end]
        el[pos:pos] = sub_list
    elif method_name == 'swap':
        other = element_lists[method_args[0]]
        element_lists[method_args[0]] = el
        element_lists[addr] = other
    elif method_name == 'point':
        breakpoints.add(method_args[0])
    else:
        print("Unhandled method: ", method_name)


def playupto(length):
    addr_to_name.clear()
    name_to_addr.clear()
    element_lists.clear()
    list_depth.clear()
    for index in range(length):
        handleop(ops[index])


def find_prev_point(start, name):
    orig_start = start
    while start > 0:
        start -= 1;
        fields = ops[start]
        if fields[2] != 'point':
            continue
        if not name or fields[3] == name:
            return start + 1
    return orig_start + 1


def find_next_point(start, name):
    orig_start = start
    while start < len(ops):
        start += 1;
        fields = ops[start]
        if fields[2] != 'point':
            continue
        if not name or fields[3] == name:
            return start + 1
    return orig_start + 1


def printall():
    print(input_line)
    sorted_lists = [(list_depth.get(addr, -1), addr) for addr in element_lists]
    sorted_lists.sort()
    for _depth, addr in sorted_lists:
        printlist(addr)


index = len(ops)
last_cmd = ['']
watch_list = set()
while True:
    playupto(index)

    if index == 0:
        print("init")
    else:
        op = ops[index - 1]
        print("#%s %s" % (index - 1, op))
        if op[2] == 'push_back':
            print(getstr(op[4]))

    for list_name in watch_list:
        printlist(list_name)

    try:
        cmd = input("> ").split()
    except EOFError:
        print()
        break

    if not cmd or cmd[0] == '':
        cmd = last_cmd

    if not cmd or cmd[0] == '':
        pass
    elif cmd[0] == 'h':
        print('Help:')
        print('  q - quit')
        print('  s - Start over')
        print('  n - Next step')
        print('  r - Previous step')
        print('  b - Previous breakpoint')
        print('  c - Next breakpoint')
        print('  p - Print state')
        print('  w <var> - Add a variable to the watch list')
        print('  u <var> - Remove a variable from the watch list')
    elif cmd[0] == 'q':
        break
    elif cmd[0] == 's':
        index = 0
    elif cmd[0] == 'n':
        if index < len(ops):
            index += 1
    elif cmd[0] == 'r':
        if index > 0:
            index -= 1
    elif cmd[0] == 'b':
        if len(cmd) == 1:
            cmd.append('')

        index = find_prev_point(index - 1, cmd[1])
    elif cmd[0] == 'c':
        if len(cmd) == 1:
            cmd.append('')
        index = find_next_point(index - 1, cmd[1])
    elif cmd[0] == 'p':
        if len(cmd) > 1:
            printlist(cmd[1])
        else:
            printall()
    elif cmd[0] == 'w':
        watch_list.add(cmd[1])
    elif cmd[0] == 'u':
        if watch_list:
            watch_list.remove(cmd[1])
    else:
        print("error: unknown command --", cmd)

    printall()

    last_cmd = cmd
