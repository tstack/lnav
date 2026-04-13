#!/usr/bin/env python3
"""Spawn a TUI program (lnav) in a PTY, let pyte emulate the terminal,
and render the final screen to SVG.

Key wrinkles for this to work with notcurses-based TUIs:
  - pyte must answer DA/DSR/CPR queries or notcurses blocks forever.
    We override `write_process_input` so responses go back to the child.
  - We close the master PTY to send SIGHUP; lnav unwinds cleanly on
    that, whereas SIGTERM sometimes leaves helper processes around.
"""

from __future__ import annotations

import argparse
import fcntl
import html
import os
import pty
import select
import signal
import struct
import sys
import termios
import time

import pyte
import pyte.charsets as _cs


# DEC Special Graphics map: when the stream selects G0 = `0`, printable
# ASCII is rewritten via this table (pyte already ships it; we reuse it
# but apply the translation ourselves because pyte ignores `ESC(0` once
# `use_utf8` is true, and we need utf8 on for lnav's multi-byte output).
_DEC_MAP = _cs.MAPS["0"]


class _DecTranslator:
    """Stateful translator that rewrites `ESC(0 ... ESC(B` spans to
    their Unicode box-drawing equivalents.  State persists across
    chunks so a split sequence (e.g. a chunk ending mid-ESC) doesn't
    corrupt the output."""

    # States: 0 = normal, 1 = saw ESC, 2 = ESC(, 3 = CSI (ESC[...), 4 = OSC (ESC]...)
    S_NORMAL = 0
    S_ESC = 1
    S_PAREN = 2  # saw ESC + '(' or ')'
    S_CSI = 3
    S_OSC = 4

    def __init__(self) -> None:
        self.state = self.S_NORMAL
        self.dec = False        # DEC Special Graphics active
        self.paren_mode = ""    # '(' or ')' awaiting charset code
        self.pending = ""       # carry-through for escape sequence prefix

    def feed(self, text: str) -> str:
        out: list[str] = []
        for c in text:
            if self.state == self.S_NORMAL:
                if c == "\x1b":
                    self.state = self.S_ESC
                    self.pending = c
                elif self.dec and 0x20 <= ord(c) <= 0x7e:
                    out.append(_DEC_MAP[ord(c)])
                else:
                    out.append(c)
            elif self.state == self.S_ESC:
                self.pending += c
                if c in "()":
                    self.paren_mode = c
                    self.state = self.S_PAREN
                elif c == "[":
                    self.state = self.S_CSI
                elif c == "]":
                    self.state = self.S_OSC
                else:
                    # Other ESC sequence — emit and return to normal.
                    out.append(self.pending)
                    self.pending = ""
                    self.state = self.S_NORMAL
            elif self.state == self.S_PAREN:
                # charset code byte
                if self.paren_mode == "(":
                    if c == "0":
                        self.dec = True
                    elif c in "BUV":
                        self.dec = False
                    else:
                        out.append(self.pending + c)
                # G1 (')') selection: preserve as-is, we don't model G1.
                else:
                    out.append(self.pending + c)
                self.pending = ""
                self.state = self.S_NORMAL
            elif self.state == self.S_CSI:
                self.pending += c
                # Final byte is in 0x40..0x7e
                if 0x40 <= ord(c) <= 0x7e:
                    out.append(self.pending)
                    self.pending = ""
                    self.state = self.S_NORMAL
            elif self.state == self.S_OSC:
                self.pending += c
                if c == "\x07":
                    out.append(self.pending)
                    self.pending = ""
                    self.state = self.S_NORMAL
                elif len(self.pending) >= 2 and \
                        self.pending[-2:] == "\x1b\\":
                    out.append(self.pending)
                    self.pending = ""
                    self.state = self.S_NORMAL
        return "".join(out)


class _AnsweringByteStream(pyte.ByteStream):
    """ByteStream variant that (a) answers DA/DSR queries back to the
    child and (b) translates DEC Special Graphics inline so we can stay
    in UTF-8 mode for lnav's multi-byte output."""

    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self._dec = _DecTranslator()

    def feed(self, data: bytes) -> None:  # type: ignore[override]
        text = self.utf8_decoder.decode(data) if self.use_utf8 \
            else "".join(map(chr, data))
        pyte.Stream.feed(self, self._dec.feed(text))


# Night Owl-inspired palette — matches the colors lnav's default theme
# emits, so the shot looks right out of the box.
PALETTE = {
    "black":   "#011627",
    "red":     "#ef5350",
    "green":   "#22da6e",
    "yellow":  "#addb67",
    "blue":    "#82aaff",
    "magenta": "#c792ea",
    "cyan":    "#21c7a8",
    "white":   "#d6deeb",
    "brown":   "#b39554",
}

DEFAULT_FG = "#d6deeb"
DEFAULT_BG = "#011627"
WINDOW_BG  = "#171717"


def resolve_color(name: str, is_bg: bool) -> str:
    if not name or name == "default":
        return DEFAULT_BG if is_bg else DEFAULT_FG
    if name in PALETTE:
        return PALETTE[name]
    if len(name) == 6 and all(c in "0123456789abcdefABCDEF" for c in name):
        return "#" + name.lower()
    return DEFAULT_BG if is_bg else DEFAULT_FG


class _AnsweringScreen(pyte.Screen):
    """pyte Screen that routes DA/DSR/CPR responses back to the child."""

    def __init__(self, cols: int, rows: int, fd: int) -> None:
        super().__init__(cols, rows)
        self._fd = fd

    def write_process_input(self, data: str) -> None:
        try:
            os.write(self._fd, data.encode("utf-8"))
        except OSError:
            pass


def spawn(argv: list[str], cols: int, rows: int) -> tuple[int, int]:
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.setdefault("TERM", "xterm-256color")
        os.environ["COLORTERM"] = "truecolor"
        os.environ["COLUMNS"] = str(cols)
        os.environ["LINES"] = str(rows)
        os.environ.pop("NO_COLOR", None)
        try:
            os.execvp(argv[0], argv)
        except Exception as exc:
            sys.stderr.write(f"exec failed: {exc}\n")
            os._exit(127)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    return pid, fd


def capture(argv: list[str], cols: int, rows: int,
            settle: float, quiet: float,
            debug: bool = False,
            raw_sink: bytearray | None = None) -> pyte.Screen:
    pid, fd = spawn(argv, cols, rows)
    screen = _AnsweringScreen(cols, rows, fd)
    stream = _AnsweringByteStream(screen)

    start = time.time()
    last_read = start
    min_capture = start + 0.5
    deadline = start + settle
    total = 0
    while True:
        now = time.time()
        if now >= deadline:
            if debug:
                print(f"[{now - start:.2f}s] deadline", file=sys.stderr)
            break
        if now > min_capture and (now - last_read) > quiet:
            if debug:
                print(f"[{now - start:.2f}s] quiet "
                      f"(last read {now - last_read:.2f}s ago, "
                      f"total={total}B)", file=sys.stderr)
            break
        r, _, _ = select.select([fd], [], [], 0.1)
        if not r:
            continue
        try:
            data = os.read(fd, 65536)
        except OSError:
            break
        if not data:
            break
        total += len(data)
        if raw_sink is not None:
            raw_sink.extend(data)
        stream.feed(data)
        last_read = time.time()
        if debug:
            print(f"[{last_read - start:.2f}s] +{len(data)}B "
                  f"(total={total}B)", file=sys.stderr)

    # Closing the master fd sends SIGHUP to the child, which lnav
    # handles cleanly.  Fall back to SIGKILL if the child hangs around.
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
        except ProcessLookupError:
            pass
        try:
            os.waitpid(pid, 0)
        except (ChildProcessError, OSError):
            pass

    return screen


def render_svg(screen: pyte.Screen, cols: int, rows: int,
               font_size: float = 14.0,
               line_height_ratio: float = 1.2,
               char_width: float = 8.4,
               padding: tuple[float, float, float, float] = (16, 20, 20, 20),
               title_bar: float = 28.0) -> str:
    pt_top, pt_right, pt_bot, pt_left = padding
    lh = font_size * line_height_ratio
    cw = char_width
    term_w = cw * cols
    term_h = lh * rows
    width = pt_left + term_w + pt_right
    height = title_bar + pt_top + term_h + pt_bot
    term_x = pt_left
    term_y = title_bar + pt_top

    # macOS traffic-light colors.
    tl_red    = "#ff5f57"
    tl_yellow = "#febc2e"
    tl_green  = "#28c840"
    title_bg  = "#2a2a2a"
    title_stroke = "#1a1a1a"

    out: list[str] = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>')
    out.append(
        f'<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{width:.2f}" height="{height:.2f}" '
        f'viewBox="0 0 {width:.2f} {height:.2f}">'
    )
    out.append('<defs>')
    out.append('  <filter id="shadow" x="-10%" y="-10%" width="120%" height="130%">')
    out.append('    <feDropShadow dx="0" dy="12" stdDeviation="16" flood-opacity="0.5"/>')
    out.append('  </filter>')
    # Clip path for the title bar so only the top corners round.
    out.append(
        f'  <clipPath id="titleClip"><rect x="0" y="0" '
        f'width="{width:.2f}" height="{title_bar:.2f}"/></clipPath>'
    )
    out.append('</defs>')
    # Full window — rounded on all corners, with shadow.
    out.append(
        f'<rect x="0" y="0" width="{width:.2f}" height="{height:.2f}" '
        f'fill="{WINDOW_BG}" rx="10" ry="10" filter="url(#shadow)"/>'
    )
    # Title bar — draw a full rounded rect but clipped to the title-bar
    # region so only the top corners appear rounded.
    out.append(
        f'<g clip-path="url(#titleClip)">'
        f'<rect x="0" y="0" width="{width:.2f}" height="{title_bar + 10:.2f}" '
        f'fill="{title_bg}" rx="10" ry="10"/>'
        f'</g>'
    )
    # Hairline divider under the title bar.
    out.append(
        f'<line x1="0" y1="{title_bar:.2f}" x2="{width:.2f}" y2="{title_bar:.2f}" '
        f'stroke="{title_stroke}" stroke-width="1"/>'
    )
    # Traffic lights — three 12px circles, 8px apart, 14px from left, centered vertically.
    tl_cy = title_bar / 2
    tl_r = 6.0
    tl_gap = 8.0
    tl_x0 = 14.0 + tl_r
    for cx, fill in [(tl_x0, tl_red),
                     (tl_x0 + 2 * tl_r + tl_gap, tl_yellow),
                     (tl_x0 + 4 * tl_r + 2 * tl_gap, tl_green)]:
        out.append(
            f'<circle cx="{cx:.2f}" cy="{tl_cy:.2f}" r="{tl_r:.2f}" '
            f'fill="{fill}" stroke="rgba(0,0,0,0.2)" stroke-width="0.5"/>'
        )
    # Window title.
    out.append(
        f'<text x="{width / 2:.2f}" y="{tl_cy + 4:.2f}" '
        f'fill="#c0c0c0" font-family="-apple-system, BlinkMacSystemFont, '
        f'\'SF Pro Text\', \'Helvetica Neue\', Helvetica, Arial, sans-serif" '
        f'font-size="12px" font-weight="600" text-anchor="middle">lnav</text>'
    )
    # Terminal background.
    out.append(
        f'<rect x="{term_x:.2f}" y="{term_y:.2f}" '
        f'width="{term_w:.2f}" height="{term_h:.2f}" fill="{DEFAULT_BG}"/>'
    )
    out.append(
        '<g font-family="JetBrains Mono, JetBrainsMono Nerd Font, Menlo, Monaco, '
        'Consolas, DejaVu Sans Mono, monospace" '
        f'font-size="{font_size}px">'
    )

    # Backgrounds — one rect per run of same bg.
    for y in range(rows):
        row = screen.buffer[y]
        x = 0
        while x < cols:
            ch = row[x]
            bg = resolve_color(ch.bg, True) if not ch.reverse else resolve_color(ch.fg, False)
            if bg == DEFAULT_BG and not ch.reverse:
                x += 1
                continue
            start_col = x
            while x < cols:
                c2 = row[x]
                b2 = resolve_color(c2.bg, True) if not c2.reverse else resolve_color(c2.fg, False)
                if b2 != bg:
                    break
                x += 1
            rx = pt_left + start_col * cw
            ry = term_y + y * lh
            rw = (x - start_col) * cw
            # Overlap each rect 0.5px below to avoid sub-pixel
            # antialiasing gaps between adjacent rows.
            out.append(
                f'<rect x="{rx:.2f}" y="{ry:.2f}" '
                f'width="{rw:.2f}" height="{lh + 0.5:.2f}" fill="{bg}"/>'
            )

    # Vertical bars (U+2502) — draw as SVG lines so adjacent-row scrollbar
    # chars join continuously without font-metric gaps.
    for col in range(cols):
        y = 0
        while y < rows:
            ch = screen.buffer[y][col]
            if ch.data != "│":
                y += 1
                continue
            start_row = y
            fg = resolve_color(ch.fg, False) if not ch.reverse \
                else resolve_color(ch.bg, True)
            while y < rows:
                c2 = screen.buffer[y][col]
                if c2.data != "│":
                    break
                fg2 = resolve_color(c2.fg, False) if not c2.reverse \
                    else resolve_color(c2.bg, True)
                if fg2 != fg:
                    break
                y += 1
            x_mid = pt_left + col * cw + cw / 2
            y1 = term_y + start_row * lh
            y2 = term_y + y * lh
            out.append(
                f'<line x1="{x_mid:.2f}" y1="{y1:.2f}" '
                f'x2="{x_mid:.2f}" y2="{y2:.2f}" '
                f'stroke="{fg}" stroke-width="1"/>'
            )

    # Underlines — standalone lines so spans across color changes stay
    # continuous and trailing-space underlines aren't lost.
    underline_y_offset = font_size * 0.95
    for y in range(rows):
        row = screen.buffer[y]
        x = 0
        baseline = term_y + y * lh + font_size * 0.85
        while x < cols:
            if not row[x].underscore:
                x += 1
                continue
            start_col = x
            fg = resolve_color(row[x].fg, False) if not row[x].reverse \
                else resolve_color(row[x].bg, True)
            while x < cols and row[x].underscore:
                x += 1
            x1 = pt_left + start_col * cw
            x2 = pt_left + x * cw
            ly = baseline + (font_size * 0.12)
            out.append(
                f'<line x1="{x1:.2f}" y1="{ly:.2f}" '
                f'x2="{x2:.2f}" y2="{ly:.2f}" '
                f'stroke="{fg}" stroke-width="1"/>'
            )

    # Foregrounds — one text element per run of same style.
    for y in range(rows):
        row = screen.buffer[y]
        x = 0
        baseline = term_y + y * lh + font_size * 0.85
        while x < cols:
            ch = row[x]
            fg = resolve_color(ch.fg, False) if not ch.reverse else resolve_color(ch.bg, True)
            style_key = (fg, ch.bold, ch.italics)
            start_col = x
            buf: list[str] = []
            while x < cols:
                c2 = row[x]
                fg2 = resolve_color(c2.fg, False) if not c2.reverse else resolve_color(c2.bg, True)
                if (fg2, c2.bold, c2.italics) != style_key:
                    break
                d = c2.data
                # U+2502 is drawn as an SVG line above; replace with a
                # space here so it doesn't render twice.
                buf.append(' ' if not d or d == "│" else d)
                x += 1
            text = ''.join(buf).rstrip()
            if not text:
                continue
            attrs = [
                f'x="{pt_left + start_col * cw:.2f}"',
                f'y="{baseline:.2f}"',
                f'fill="{fg}"',
                'xml:space="preserve"',
                f'textLength="{len(text) * cw:.2f}"',
                'lengthAdjust="spacing"',
            ]
            if ch.bold:
                attrs.append('font-weight="bold"')
            if ch.italics:
                attrs.append('font-style="italic"')
            out.append(f'<text {" ".join(attrs)}>{html.escape(text)}</text>')

    out.append('</g>')
    out.append('</svg>')
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cols", type=int, default=120)
    ap.add_argument("--rows", type=int, default=40)
    ap.add_argument("--settle", type=float, default=5.0,
                    help="Max seconds to wait before giving up")
    ap.add_argument("--quiet", type=float, default=0.8,
                    help="Stop once output has been quiet this long")
    ap.add_argument("--out", required=True, help="Output SVG path")
    ap.add_argument("--debug-ansi", help="If set, write the raw captured bytes here")
    ap.add_argument("--debug", action="store_true")
    ap.add_argument("cmd", nargs="+", help="Program + args to run")
    args = ap.parse_args()

    raw_sink = bytearray() if args.debug_ansi else None
    screen = capture(args.cmd, args.cols, args.rows,
                     args.settle, args.quiet,
                     debug=args.debug, raw_sink=raw_sink)
    if raw_sink is not None and args.debug_ansi:
        with open(args.debug_ansi, "wb") as f:
            f.write(raw_sink)

    svg = render_svg(screen, args.cols, args.rows)
    with open(args.out, "w") as f:
        f.write(svg)
    print(f"wrote {args.out} ({len(svg)} bytes)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
