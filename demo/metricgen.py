#! /usr/bin/env python3
"""Emit a metrics_log-format CSV at 4 Hz for exercising `lnav -t`.

Usage:
    python3 metricgen.py > /tmp/metrics.csv &
    lnav -t /tmp/metrics.csv

Writes a timestamp + four synthetic metric columns (`cpu_pct`,
`mem_mb`, `disk_iops`, `queue_depth`).  The values drift with a small
random walk so the sparkline bars and spectrogram have shape to show.
Runs until Ctrl-C.  Each row is flushed immediately so the tail
follow-up sees every row without buffering.
"""
import datetime
import random
import signal
import sys
import time

HEADERS = ["timestamp", "cpu_pct", "mem_mb", "disk_iops", "queue_depth"]
INTERVAL_S = 0.25

# Starting values for the random walk.
state = {
    "cpu_pct": 20.0,
    "mem_mb": 2048.0,
    "disk_iops": 150.0,
    "queue_depth": 5.0,
}

# Per-metric step size (stddev of the per-tick delta) and valid range.
walk = {
    "cpu_pct": (2.0, 0.0, 100.0),
    "mem_mb": (64.0, 256.0, 16384.0),
    "disk_iops": (30.0, 0.0, 2000.0),
    "queue_depth": (1.0, 0.0, 64.0),
}


def step(name):
    val = state[name]
    sigma, lo, hi = walk[name]
    val += random.gauss(0.0, sigma)
    val = max(lo, min(hi, val))
    state[name] = val
    return val


def handle_sigterm(signum, frame):
    sys.exit(0)


signal.signal(signal.SIGTERM, handle_sigterm)
signal.signal(signal.SIGINT, handle_sigterm)

sys.stdout.write(",".join(HEADERS) + "\n")
sys.stdout.flush()

while True:
    ts = datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%S.%fZ")
    row = [
        ts,
        f"{step('cpu_pct'):.2f}",
        f"{step('mem_mb'):.0f}",
        f"{step('disk_iops'):.0f}",
        f"{step('queue_depth'):.0f}",
    ]
    sys.stdout.write(",".join(row) + "\n")
    sys.stdout.flush()
    time.sleep(INTERVAL_S)
