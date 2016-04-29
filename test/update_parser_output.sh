#!/usr/bin/env bash

test_dir=`dirname $0`

for fn in ${test_dir}/datafile_simple.*; do
    echo "Checking $fn"
    ./drive_data_scanner -p $fn
done

for fn in ${test_dir}/log-samples/sample-*; do
    echo "Checking $fn"
    ./drive_data_scanner -p -l $fn
done
