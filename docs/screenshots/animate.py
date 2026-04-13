#!/usr/bin/env python3
"""Drive an lnav session through a scripted keystroke sequence and
emit an animated SVG that cycles through the captured frames."""

from __future__ import annotations

import argparse
import copy
import os
import select
import signal
import sys
import time

import pyte

# Reuse the PTY + pyte + SVG-render infrastructure.
import render as R


def drain(stream: pyte.ByteStream, fd: int, duration: float, quiet: float,
          raw_sink: bytearray | None = None) -> None:
    """Read from `fd` for at most `duration` seconds, but bail early
    once the child has been idle for `quiet` seconds."""
    start = time.time()
    deadline = start + duration
    last_read = start
    min_capture = start + 0.15
    while True:
        now = time.time()
        if now >= deadline:
            return
        if now > min_capture and (now - last_read) > quiet:
            return
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            continue
        try:
            data = os.read(fd, 65536)
        except OSError:
            return
        if not data:
            return
        if raw_sink is not None:
            raw_sink.extend(data)
        stream.feed(data)
        last_read = time.time()


def snapshot(screen: pyte.Screen) -> pyte.Screen:
    """Deep-copy just the buffer we need for rendering.  A full
    deepcopy of the Screen is slow; we only need the cell grid."""
    snap = pyte.Screen(screen.columns, screen.lines)
    for y in range(screen.lines):
        for x in range(screen.columns):
            snap.buffer[y][x] = screen.buffer[y][x]
    return snap


def run(argv: list[str], cols: int, rows: int,
        steps: list[tuple[float, bytes, str]],
        initial_settle: float, initial_quiet: float,
        step_settle: float, step_quiet: float,
        debug: bool = False) -> list[tuple[pyte.Screen, str]]:
    """Spawn lnav, capture an initial frame, then replay each
    (wait-after-input, bytes, label) step.  Returns a list of
    (screen, label) tuples."""
    pid, fd = R.spawn(argv, cols, rows)
    screen = R._AnsweringScreen(cols, rows, fd)
    stream = R._AnsweringByteStream(screen)

    frames: list[tuple[pyte.Screen, str]] = []
    drain(stream, fd, initial_settle, initial_quiet)
    frames.append((snapshot(screen), "initial"))
    if debug:
        print(f"  frame 0: initial", file=sys.stderr)

    for i, (wait_after, data, label) in enumerate(steps, 1):
        if data:
            try:
                os.write(fd, data)
            except OSError as exc:
                print(f"  write failed: {exc}", file=sys.stderr)
                break
        # Pure-wait steps (empty data) hold for the full duration so
        # late-arriving renders (e.g. async syntax-highlight parsers
        # that update the screen after a debounce) get captured.
        q = step_quiet if data else wait_after + 1.0
        drain(stream, fd, wait_after, q)
        frames.append((snapshot(screen), label))
        if debug:
            print(f"  frame {i}: {label}", file=sys.stderr)

    # Clean up
    try:
        os.close(fd)
    except OSError:
        pass
    for _ in range(30):
        try:
            wpid, _ = os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            break
        if wpid:
            break
        time.sleep(0.05)
    else:
        try:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        except (ProcessLookupError, ChildProcessError):
            pass

    return frames


_KEY_NAMES = {
    b"\t": "Tab",
    b"\r": "Enter",
    b"\n": "Enter",
    b"\x1b": "Esc",
    b" ": "Space",
    b"\x7f": "⌫",
}


def human_key(data: bytes) -> str:
    if data in _KEY_NAMES:
        return _KEY_NAMES[data]
    try:
        s = data.decode("utf-8")
    except UnicodeDecodeError:
        return data.hex()
    if len(s) == 1 and s.isprintable():
        return s
    return data.hex()


def render_animated_svg(frames: list[tuple[pyte.Screen, str]],
                        steps: list[tuple[float, bytes, str]],
                        cols: int, rows: int,
                        per_frame: float,
                        hold_last: float) -> str:
    """Emit an SVG where each frame is a <g> with CSS keyframe
    animation driving its visibility in sequence, looping forever."""
    lay = R.layout(cols, rows)
    n = len(frames)
    durations = [per_frame] * (n - 1) + [hold_last]
    starts: list[float] = []
    t = 0.0
    for d in durations:
        starts.append(t)
        t += d
    total = t

    # Build per-frame CSS keyframes that hold each frame visible over
    # its [start, start+dur) window and hidden everywhere else.  Using
    # `visibility` (not `display`) so the elements stay laid out — this
    # avoids reflow artifacts in some renderers.
    styles: list[str] = []
    styles.append(".lnav-frame { visibility: hidden; }")
    for i, d in enumerate(durations):
        start_pct = (starts[i] / total) * 100
        end_pct = ((starts[i] + d) / total) * 100
        # Three-stop keyframe: hidden until start, visible through end, hidden after.
        # Use a tiny epsilon (0.0001) so transitions are instant.
        parts = []
        if start_pct > 0:
            parts.append(f"0% {{ visibility: hidden; }}")
            parts.append(f"{start_pct - 0.0001:.4f}% {{ visibility: hidden; }}")
        parts.append(f"{start_pct:.4f}% {{ visibility: visible; }}")
        if end_pct < 100:
            parts.append(f"{end_pct - 0.0001:.4f}% {{ visibility: visible; }}")
            parts.append(f"{end_pct:.4f}% {{ visibility: hidden; }}")
            parts.append(f"100% {{ visibility: hidden; }}")
        else:
            parts.append(f"100% {{ visibility: visible; }}")
        styles.append(f"@keyframes lnav-f{i} {{ " + " ".join(parts) + " }")
        styles.append(
            f".lnav-f{i} {{ "
            f"animation: lnav-f{i} {total:.3f}s infinite; "
            f"animation-timing-function: step-end; "
            f"}}"
        )

    # Build the cumulative keypress badge sequence for each frame.
    # Frame 0 has no keys; frame i>=1 has the keys from steps[0..i-1].
    key_displays: list[str] = [""] * len(frames)
    acc: list[str] = []
    for i, (_, data, _) in enumerate(steps, start=1):
        acc.append(human_key(data))
        if i < len(frames):
            key_displays[i] = " ".join(acc)

    # Extend the SVG viewport to add a strip below the window for the
    # keypress badge.  Leave room for the window's drop shadow too.
    badge_strip = 56.0          # total vertical space reserved below chrome
    badge_gap = 28.0            # shadow clearance between chrome bottom and text
    badge_font_size = 20.0
    full_width = lay["width"]
    full_height = lay["height"] + badge_strip
    badge_y = lay["height"] + badge_gap + badge_font_size * 0.7

    # svg_header emits a fixed width/height from lay; rebuild the header
    # to use the expanded height instead.
    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (f'<svg xmlns="http://www.w3.org/2000/svg" '
         f'width="{full_width:.2f}" height="{full_height:.2f}" '
         f'viewBox="0 0 {full_width:.2f} {full_height:.2f}">'),
        '<defs>',
        '  <filter id="shadow" x="-10%" y="-10%" width="120%" height="130%">',
        '    <feDropShadow dx="0" dy="12" stdDeviation="16" flood-opacity="0.5"/>',
        '  </filter>',
        (f'  <clipPath id="titleClip"><rect x="0" y="0" '
         f'width="{lay["width"]:.2f}" height="{lay["title_bar"]:.2f}"/></clipPath>'),
        '</defs>',
    ]
    out.append("<style>\n" + "\n".join(styles) + "\n</style>")
    out += R.render_chrome(lay)
    # Keypress label strip below the window.
    badge_font = ('font-family="-apple-system, BlinkMacSystemFont, \'SF Pro Text\', '
                  '\'Helvetica Neue\', Helvetica, Arial, sans-serif" '
                  f'font-size="{badge_font_size}px" font-weight="500"')
    for i, kd in enumerate(key_displays):
        if not kd:
            continue
        out.append(
            f'<text class="lnav-frame lnav-f{i}" '
            f'x="{full_width / 2:.2f}" y="{badge_y:.2f}" '
            f'fill="#c8c8d0" {badge_font} '
            f'text-anchor="middle">{kd}</text>'
        )
    out.append(R.text_group_open(lay))
    for i, (screen, label) in enumerate(frames):
        out.append(f'<g class="lnav-frame lnav-f{i}"><!-- frame {i}: {label} -->')
        out += [f'  {s}' for s in R.render_frame(screen, lay)]
        out.append('</g>')
    out.append('</g>')
    out.append('</svg>')
    return "\n".join(out) + "\n"


def parse_steps(spec: str) -> list[tuple[float, bytes, str]]:
    """Parse a simple step spec.  Each line is:

        wait_seconds \\t bytes (python-escape-decoded) \\t label

    Example:
        0.4\\t/\\tsearch prompt
        0.3\\tv\\ttype v
        0.6\\t\\x09\\tTAB
    """
    out: list[tuple[float, bytes, str]] = []
    for raw_line in spec.splitlines():
        line = raw_line.rstrip("\n")
        if not line or line.lstrip().startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        wait = float(parts[0])
        data = parts[1].encode("utf-8").decode("unicode_escape").encode("latin-1")
        label = parts[2] if len(parts) > 2 else f"step ({parts[1]!r})"
        out.append((wait, data, label))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cols", type=int, default=120)
    ap.add_argument("--rows", type=int, default=40)
    ap.add_argument("--initial-settle", type=float, default=6.0)
    ap.add_argument("--initial-quiet", type=float, default=1.2)
    ap.add_argument("--step-settle", type=float, default=2.0,
                    help="Max seconds to wait after each keystroke")
    ap.add_argument("--step-quiet", type=float, default=0.8,
                    help="Stop waiting after each keystroke once output "
                         "has been quiet this long")
    ap.add_argument("--per-frame", type=float, default=0.7)
    ap.add_argument("--hold-last", type=float, default=2.5)
    ap.add_argument("--out", required=True)
    ap.add_argument("--steps", required=True,
                    help="Path to a step file (see parse_steps)")
    ap.add_argument("--debug", action="store_true")
    ap.add_argument("cmd", nargs="+")
    args = ap.parse_args()

    with open(args.steps) as f:
        steps = parse_steps(f.read())
    if not steps:
        print("error: no steps parsed", file=sys.stderr)
        return 2
    frames = run(args.cmd, args.cols, args.rows, steps,
                 args.initial_settle, args.initial_quiet,
                 args.step_settle, args.step_quiet,
                 debug=args.debug)

    svg = render_animated_svg(frames, steps, args.cols, args.rows,
                              per_frame=args.per_frame,
                              hold_last=args.hold_last)
    with open(args.out, "w") as f:
        f.write(svg)
    print(f"wrote {args.out} ({len(svg)} bytes, {len(frames)} frames)",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
