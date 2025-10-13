---
title: lnav external-access server
---


## lnav

The Logfile Navigator, *lnav* for short, is a log file viewer for the terminal.

This server provides remote access to an lnav instance.  See
https://docs.lnav.org for more information.

### API Test Harness

#### Poll

The `/api/poll` endpoint performs a long-poll that will return when the state of
the TUI has changed. You can click the "Poll" button to perform a poll and
see the result.

<button id="poll">Poll</button>

<div id="poll-result-container">
<p>
Last Response Time: <div id="poll-time"></div>
<p>
Last Response:
<pre><code id="poll-result" class="language-json">null</code></pre>
</div>

#### Exec

The `/api/exec` endpoint executes an lnav script and returns the result.

<label for="exec-input">Script:</label>
<textarea id="exec-input" rows="10" cols="80">
:echo Hello, World!
</textarea>
<button id="exec">Execute</button>

<div id="exec-result-container">
<pre id="exec-result"></pre>
</div>

<script src="assets/js/lnav-api-test.js"></script>
