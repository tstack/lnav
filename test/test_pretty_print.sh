#! /bin/bash

echo '2015-04-18T13:16:30.003 8.8.8.8 <foo>8.8.8.8</foo>9 8.8.8.8<1054 198.51.100.1546 544.9.8.7 98.542.241.99 19143.2.5.6' | \
    run_test ${lnav_test} -n -c ":switch-to-view pretty"

check_output "pretty print not able to properly grok ipv4?" <<EOF
2015-04-18T13:16:30.003 8.8.8.8 (google-public-dns-a.google.com)
<foo>8.8.8.8 (google-public-dns-a.google.com)</foo>
9 8.8.8.8 (google-public-dns-a.google.com)<1054 198.51.100.1546 544.9.8.7 98.542.241.99 19143.2.5.6
EOF
