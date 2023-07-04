#! /bin/bash

run_test ./scripty -n -e ${srcdir}/listview_output.0 -- \
    ./drive_listview < /dev/null

on_error_fail_with "listview output does not match?"

run_test ./scripty -n -e ${srcdir}/listview_output.1 -- \
    ./drive_listview -t 1 < /dev/null

on_error_fail_with "listview didn't move down?"

run_test ./scripty -n -e ${srcdir}/listview_output.2 -- \
    ./drive_listview -l 1 < /dev/null

on_error_fail_with "Listview didn't move right?"

run_test ./scripty -n -e ${srcdir}/listview_output.3 -- \
    ./drive_listview -t 1 -l 1 < /dev/null

on_error_fail_with "Listview didn't move left and right?"

run_test ./scripty -n -e ${srcdir}/listview_output.4 -- \
    ./drive_listview -y 1 -r 50 < /dev/null

on_error_fail_with "Listview doesn't start down one line?"

run_test ./scripty -n -e ${srcdir}/listview_output.5 -- \
    ./drive_listview -y 1 -r 50 -h -1 < /dev/null

on_error_fail_with "Listview isn't shorter?"

run_test ./scripty -n -e ${srcdir}/listview_output.6 -- \
    ./drive_listview -y 1 -r 50 -h -1 -t 1 < /dev/null

on_error_fail_with "Listview didn't move down (2)?"

###
# Cursor mode tests
###

# Cursor appears on first line
run_test ./scripty -n -e ${srcdir}/listview_output_cursor.0 -- \
    ./drive_listview -c < /dev/null

on_error_fail_with "Listview Cursor Mode: Didn't enable (not selectable)"

# Move down within visible area between top (at 0) and tail space
run_test ./scripty -n -e ${srcdir}/listview_output_cursor.1 -- \
    ./drive_listview  -r 20 -c -k jjjjj < /dev/null

on_error_fail_with "Listview Cursor Mode: Didn-t move cursor down?"

# Move up within visible area between top (at 0) and tail space
run_test ./scripty -n -e ${srcdir}/listview_output_cursor.2 -- \
    ./drive_listview  -r 20 -c -k jjjjjkk < /dev/null

on_error_fail_with "Listview Cursor Mode: Didn't move cursor up?"


# Scroll file when reaching tail space
run_test ./scripty -n -e ${srcdir}/listview_output_cursor.3 -- \
    ./drive_listview  -r 30 -h 5 -c -k jjjj < /dev/null

on_error_fail_with "Listview Cursor Mode: Didn't scroll down when reaching tail space?"

# Do not scroll up when moving up after reaching tail space
run_test ./scripty -n -e ${srcdir}/listview_output_cursor.4 -- \
    ./drive_listview  -r 30 -h 5 -c -k jjjjk < /dev/null

on_error_fail_with "Listview Cursor Mode: scrolled when moving up from tail space?"

# Page down move
run_test ./scripty -n -e ${srcdir}/listview_output_cursor.5 -- \
    ./drive_listview  -r 30 -h 10 -c -k '  ' < /dev/null

on_error_fail_with "Listview Cursor Mode: didn't moved down on page jump?"

# Page up move
run_test ./scripty -n -e ${srcdir}/listview_output_cursor.6 -- \
    ./drive_listview  -r 30 -h 10 -c -k '  b' < /dev/null

on_error_fail_with "Listview Cursor Mode: didn't moved up on page jump?"
