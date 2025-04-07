#!/bin/bash
#
# This script executes the command passed to it inside lnav's Docker build environment.
# The repository root is mounted to the container, and the working directory inside
# the container matches the current directory on the host.
#
# Usage:
#   ./docker-run.sh <command> [args...]
#
# Examples:
#   ./docker-run.sh ./autogen.sh                   # Run autogen.sh inside the container
#   ./docker-run.sh ../configure --enable-release  # Run configure script
#   ./docker-run.sh make -j4                       # Build the project
#   ./docker-run.sh                                # Open an interactive shell in the container
#

WORKSPACE_ROOT=$(cd "$(dirname "$0")" && pwd)
CONTAINER_WS="/lnav"

# Compute the current directory's relative path to the workspace root.
RELATIVE_DIR=$(realpath --relative-to="$WORKSPACE_ROOT" "$(pwd)")
CONTAINER_WORKDIR="${CONTAINER_WS}/${RELATIVE_DIR}"

docker run -it --rm \
    --user $(id -u):$(id -g) \
    --volume ${WORKSPACE_ROOT}:${CONTAINER_WS} \
    --workdir "${CONTAINER_WORKDIR}" \
    tstack/lnav-build \
    "$@"
