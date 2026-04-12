#!/usr/bin/env python3

import filecmp
import os
import shutil
import subprocess
import sys
from pathlib import Path

GREEN = "\033[0;32m"
RED = "\033[0;31m"
RESET = "\033[0m"


def prompt_yes_no(message):
    auto_approve = os.environ.get("AUTO_APPROVE", "")
    if auto_approve:
        return True
    while True:
        answer = input(f"{message} [y/n] ").strip().lower()
        if answer in ("y", "yes"):
            return True
        if answer in ("n", "no"):
            return False


def show_diff(expected_path, actual_path):
    subprocess.run(
        ["git", "diff", "--color-words", "--color=always", "-u",
         str(expected_path), str(actual_path)]
    )


def check_output(exp_path, actual_path, label, color):
    if not actual_path.exists():
        return
    if not exp_path.exists():
        print(f"{color}BEGIN{RESET} {actual_path}{label}")
        print(actual_path.read_text(), end="")
        print(f"{color}END{RESET}   {actual_path}{label}")
        if prompt_yes_no(f"Expected {label} is missing, update with the above?"):
            shutil.copy2(actual_path, exp_path)
        else:
            sys.exit(1)
    elif not filecmp.cmp(exp_path, actual_path, shallow=False):
        show_diff(exp_path, actual_path)
        if prompt_yes_no(f"Expected {label} is different, update with the above?"):
            shutil.copy2(actual_path, exp_path)
        else:
            sys.exit(1)


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <srcdir> <builddir>", file=sys.stderr)
        sys.exit(1)

    srcdir = Path(sys.argv[1])
    builddir = Path(sys.argv[2])
    expected_dir = srcdir / "expected"
    expected_am = expected_dir / "Makefile.am"

    expected_dir.mkdir(parents=True, exist_ok=True)

    builddir = builddir.resolve()
    cmd_files = sorted(builddir.glob("*.cmd"),
                       key=lambda p: p.stat().st_mtime, reverse=True)

    am_entries = []

    for cmd_file in cmd_files:
        print()
        print(f"Checking test {cmd_file}:")
        print(f"  {cmd_file.read_text()}", end="")

        stem = str(cmd_file)[:-len(".cmd")]
        base = os.path.basename(stem)
        exp_stem = expected_dir / base

        am_entries.append(f"    {base}.out \\")
        am_entries.append(f"    {base}.err \\")

        check_output(
            Path(f"{exp_stem}.out"),
            Path(f"{stem}.out"),
            "stdout", GREEN,
        )
        check_output(
            Path(f"{exp_stem}.err"),
            Path(f"{stem}.err"),
            "stderr", RED,
        )

    am_entries.sort()
    new_content = f"\ndist_noinst_DATA = \\\n" + "\n".join(am_entries) + "\n    $()\n"

    if not expected_am.exists() or expected_am.read_text() != new_content:
        expected_am.write_text(new_content)


if __name__ == "__main__":
    main()
