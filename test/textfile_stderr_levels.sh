#!/bin/bash

echo "info message one"
echo "info message two"
sleep 1
echo "error: something went wrong" > /dev/stderr
echo "warning: be careful" > /dev/stderr
