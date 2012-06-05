#! /bin/bash

run_test ./scripty -n -e ${srcdir}/view_colors_output.0 -- \
	./drive_view_colors < /dev/null

on_error_fail_with "view colors are wrong?"
