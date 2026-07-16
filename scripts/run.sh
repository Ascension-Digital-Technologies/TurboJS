#!/usr/bin/env sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
COMMAND=${1:-build}
shift 2>/dev/null || true
exec python3 "$SCRIPT_DIR/$COMMAND.py" "$@"
