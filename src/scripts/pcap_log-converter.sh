#!/bin/bash

# Check that tshark is installed and return a nice message.
if ! command -v tshark > /dev/null; then
  echo "pcap support requires 'tshark' v3+ to be installed" > /dev/stderr
  exit 1
fi

# We want tshark output to come in UTC
export TZ=UTC

# Use tshark to convert the pcap file into a JSON-lines log file
exec tshark -T ek -P -V -t ad -r $2
