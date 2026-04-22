#!/usr/bin/env bash
#
# Regenerate the static screenshots used by docs/03_features.md.
#
# Each shot is captured by running lnav inside a PTY we manage with
# pyte (see render.py), then rendered to SVG.  No multiplexer or
# third-party renderer is involved.
#
# Requirements:
#   - Python 3.9+
#   - pyte (installed into docs/screenshots/.venv on first run)
#   - A working lnav binary (build/dev/src/lnav is preferred; falls
#     back to whatever is on PATH).
#
# Usage:
#   ./docs/screenshots/generate.sh              # regenerate all
#   ./docs/screenshots/generate.sh timeline     # regenerate one by name

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${REPO_ROOT}/docs/assets/images"
TEST_DIR="${REPO_ROOT}/test"
VENV="${SCRIPT_DIR}/.venv"
RENDER="${SCRIPT_DIR}/render.py"
export TZ="UTC"

LNAV_BIN="${LNAV_BIN:-${REPO_ROOT}/build/dev/src/lnav}"
if [[ ! -x "${LNAV_BIN}" ]]; then
    LNAV_BIN="$(command -v lnav)"
fi

# Bootstrap venv with pyte if it isn't there yet.
if [[ ! -x "${VENV}/bin/python" ]]; then
    echo "==> Creating venv at ${VENV}"
    python3 -m venv "${VENV}"
    "${VENV}/bin/pip" install --quiet --upgrade pip
    "${VENV}/bin/pip" install --quiet pyte
fi

PY="${VENV}/bin/python"

mkdir -p "${OUT_DIR}"

COLS="${SCREENSHOT_COLS:-120}"
ROWS="${SCREENSHOT_ROWS:-40}"
SETTLE="${SCREENSHOT_SETTLE:-5}"
QUIET="${SCREENSHOT_QUIET:-1.0}"

DEBUG_FLAGS=()
if [[ "${SCREENSHOT_DEBUG:-0}" != "0" ]]; then
    DEBUG_FLAGS+=(--debug)
fi

render() {
    local slug="$1"; shift
    local out="${OUT_DIR}/${slug}.svg"
    echo "==> Generating ${slug}"
    local debug_ansi=()
    if [[ "${SCREENSHOT_DEBUG:-0}" != "0" ]]; then
        debug_ansi=(--debug-ansi "${OUT_DIR}/${slug}.ansi")
    fi
    "${PY}" "${RENDER}" \
        --cols "${COLS}" --rows "${ROWS}" \
        --settle "${SETTLE}" --quiet "${QUIET}" \
        --out "${out}" \
        ${DEBUG_FLAGS[@]+"${DEBUG_FLAGS[@]}"} \
        ${debug_ansi[@]+"${debug_ansi[@]}"} \
        -- "${LNAV_BIN}" "$@"
}

shot_multi_file() {
    render "lnav-multi-file2" \
        "${TEST_DIR}/logfile_docker_compose*"
}

shot_hist() {
    render "lnav-hist" \
        -c ":switch-to-view histogram" \
        -c ":goto 70" \
        ~/snaplogic/Tectonic/run/log/slsched_m_main.json

#        "${TEST_DIR}/logfile_access_log.0"
}

shot_timeline() {
    render "lnav-timeline" \
        -c ":switch-to-view timeline" \
        -c ":goto 40" \
        "${TEST_DIR}/logfile_strace_log.2"
}

shot_timeline_metrics() {
    # A synthetic syslog stream with five requests from distinct
    # worker PIDs — syslog_log uses `<procname>[<pid>]` as the
    # thread identifier, so each worker shows up as its own timeline
    # row without any extra plumbing.  The paired CSV holds
    # gauge-style cpu_pct / rss samples whose values track the
    # expensive operations (the "report" and "export" workers),
    # making the correlation between logs and metrics visible at a
    # glance.  Longer settle/quiet overrides are needed because
    # metric-sample collection runs on each rebuild and pushes the
    # final-frame deadline out.
    SETTLE=10 QUIET=2.0 \
    render "lnav-timeline-metrics" \
        -c ":switch-to-view timeline" \
        -c ":hide-in-timeline thread logfile" \
        -c ":timeline-metric cpu_pct" \
        -c ":timeline-metric rss" \
        -c ":goto 1" \
        "${SCRIPT_DIR}/demo.log" \
        "${SCRIPT_DIR}/demo_metrics.csv"
}

shot_before_pretty() {
    render "lnav-before-pretty" \
        "${TEST_DIR}/logfile_vami.0"
}

shot_after_pretty() {
    render "lnav-after-pretty" \
        -c ":switch-to-view pretty" \
        "${TEST_DIR}/logfile_vami.0"
}

shot_query() {
    render "lnav-query" \
        -c ";SELECT c_ip, count(*), sum(sc_bytes) AS total FROM access_log GROUP BY c_ip ORDER BY total DESC" \
        "${TEST_DIR}/logfile_shop_access_log.0"
}

ANIMATE="${SCRIPT_DIR}/animate.py"

animate() {
    local slug="$1"; shift
    local steps_file="$1"; shift
    local out="${OUT_DIR}/${slug}.svg"
    echo "==> Animating ${slug}"
    "${PY}" "${ANIMATE}" \
        --cols "${COLS}" --rows "${ROWS}" \
        --steps "${steps_file}" \
        --out "${out}" \
        ${DEBUG_FLAGS[@]+"${DEBUG_FLAGS[@]}"} \
        -- "${LNAV_BIN}" "$@"
}

shot_tab_complete() {
    animate "lnav-tab-complete" \
        "${SCRIPT_DIR}/steps_search.tsv" \
        "${TEST_DIR}/logfile_syslog.0"
}

shot_sql_syntax() {
    animate "lnav-syntax-highlight" \
        "${SCRIPT_DIR}/steps_sql_syntax.tsv" \
        "${TEST_DIR}/logfile_syslog.0"
}

ALL_SHOTS=(multi_file hist timeline timeline_metrics before_pretty after_pretty query tab_complete sql_syntax)

if [[ $# -eq 0 ]]; then
    for s in "${ALL_SHOTS[@]}"; do
        "shot_${s}"
    done
else
    for arg in "$@"; do
        if ! declare -f "shot_${arg}" >/dev/null; then
            echo "error: unknown shot '${arg}'" >&2
            echo "available: ${ALL_SHOTS[*]}" >&2
            exit 1
        fi
        "shot_${arg}"
    done
fi

echo "done"
