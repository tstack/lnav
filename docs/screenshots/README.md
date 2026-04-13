# Screenshot generation

This directory holds the tooling for regenerating the static screenshots
referenced from `docs/03_features.md`. Animated GIFs are out of scope.

## Prerequisites

- [`freeze`](https://github.com/charmbracelet/freeze) — renders a terminal
  capture to SVG and PNG:

  ```shell
  brew install charmbracelet/tap/freeze
  ```

- [`zellij`](https://zellij.dev/) — terminal multiplexer; runs lnav in a
  real PTY so its TUI initializes correctly:

  ```shell
  brew install zellij
  ```

- A working `lnav` binary. By default `generate.sh` looks for
  `build/dev/src/lnav` relative to the repo root (the CMake dev build) and
  falls back to whatever `lnav` is on `PATH`. To force a specific binary:

  ```shell
  LNAV_BIN=/path/to/lnav ./generate.sh
  ```

## Usage

Regenerate all screenshots:

```shell
./generate.sh
```

Regenerate one or more by name:

```shell
./generate.sh timeline hist
```

Available shot names: `multi_file`, `hist`, `timeline`, `before_pretty`,
`after_pretty`, `query`.

Output goes into `../assets/images/` as both `.svg` (primary) and `.png`
(fallback). The `<picture>` blocks in `03_features.md` reference both.

## Styling

All shots share the appearance defined in `freeze.json` (font, padding,
theme, window chrome). Tweak that file to adjust the look of every
screenshot at once.

## Updating the set of shots

Each screenshot is defined as a `shot_<name>` function in `generate.sh`. To
add a new shot:

1. Add a `shot_<name>` function that invokes `render <slug> "<lnav command>"`.
2. Append the name to the `ALL_SHOTS` array.
3. Reference the new image from `03_features.md` as a `<picture>` block.
