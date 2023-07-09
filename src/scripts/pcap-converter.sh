#!/bin/bash

# Check that tshark is installed and return a nice message.
if ! command -v tshark; then
  echo "pcap support requires 'tshark' v3+ to be installed" > /dev/stderr
  exit 1
fi

# Use tshark to convert the pcap file into a JSON-lines log file
exec tshark -T ek -P -V -t ad -r $2
