---

# Feel free to add content and custom Front Matter to this file.

# To modify the layout, see https://jekyllrb.com/docs/themes/#overriding-theme-defaults

layout: top
---

![Screenshot of lnav](/assets/images/lnav-front-page.png){:
style="float: right; max-width: 50%"}

<div id="top-description">
A log file viewer for the terminal
</div>

<div id="intro">
<p>Merge, tail, search, filter, and query log files with ease.</p>

<p>No server. No setup. Still featureful.</p>

<div id="playground-box">
<h4>Try it out:</h4>

<code>
<span class="prompt">$</span>
<a href="ssh://playground@demo.lnav.org">ssh playground@demo.lnav.org</a>
</code>
</div>
</div>

<div class="dlrow-left">
<dl>
<dt>Easy to Use</dt>
<dd>
<div style="width: 45%; float: right">
Just point <b>lnav</b> at a directory and it will take care of the rest.
File formats are automatically detected and compressed files are unpacked
on the fly.
Online help and previews for operations make it simpler to level up your
experience.
</div>

<div style="width: 50%; float: left; padding-left: 1em">
<video autoplay muted loop playsinline style="width: 90%; border-radius: 8px">
<source src="assets/images/lnav-open-help.mp4" type="video/mp4">
</video>
</div>
</dd>
</dl>
</div>

<div class="dlrow-right">

<dl>
<dt>Performant</dt>
<dd>
<b>lnav</b> can outperform standard terminal tools when processing log files.
The following chart compares CPU/memory usage when working with a
<a href="https://docs.lnav.org/en/latest/performance.html">3.3GB access log</a>.
The chart was also generated using lnav's
<a href="https://docs.lnav.org/en/latest/sqlext.html">SQLite Interface</a>.
<div>
<img src="assets/images/lnav-perf.png" alt="Chart comparing lnav's performance against standard tools" />
</div>
</dd>
</dl>

</div>
