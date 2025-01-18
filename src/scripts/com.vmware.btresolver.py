#!/usr/bin/env python3

import sys
import html
import time
import json
import urllib.request

sys.stderr.write("reading stdin\n")
inj = json.load(sys.stdin)
sys.stderr.write("reading stdin done\n")

if False:
    print("Hello, World!")
    sys.exit()

RESOLVING_SERVICE_URL = "https://btresolver.lvn.broadcom.net:443/"
DEFAULT_TIMEOUT = 10

req_url = "%s%s" % (RESOLVING_SERVICE_URL, "async_resolve_text_bts")
log_msg = inj['log_msg']
index = log_msg.find('[context]')
if index != -1:
    log_msg = log_msg[index:]
body = json.dumps([log_msg])
req = urllib.request.Request(req_url, body.encode('utf-8'))
resp = urllib.request.urlopen(req, timeout=DEFAULT_TIMEOUT)
resolve_content = json.loads(resp.read())

sys.stderr.write("resolve %s\n" % resolve_content)
if not resolve_content['success']:
    print(resolve_content['errorString'])
    sys.exit(1)

time.sleep(0.5)

delay = 1

done = False
while not done:
    get_url = "%s%s" % (RESOLVING_SERVICE_URL, "get_task")
    body = resolve_content['returnValue']['TaskIds'][0]
    get_req = urllib.request.Request(get_url, body.encode('utf-8'))
    get_resp = urllib.request.urlopen(get_req, timeout=DEFAULT_TIMEOUT)
    get_content = json.loads(get_resp.read())
    sys.stderr.write("get %s\n" % get_content)
    if get_content['returnValue']['state'] == 'RUNNING':
        if get_content['returnValue']['exception'] is None:
            time.sleep(delay)
            if delay < 10:
                delay = delay * 2
        else:
            print("<pre>\n%s</pre>" % html.escape(get_content['returnValue']['exception']))
            done = True
    elif get_content['returnValue']['state'] == 'COMPLETED':
        if get_content['returnValue'].get('output') is not None:
            print("<pre>\n%s</pre>" % html.escape(get_content['returnValue']['output']))
        elif get_content['returnValue'].get('exception') is not None:
            print("<pre>\n%s</pre>" % html.escape(get_content['returnValue']['exception']))
        done = True
