#!/bin/sh
# HEAD includes the invoking worktree's uncommitted changes (implementers do not commit).
exec python3 "$(dirname "$0")/build_compare.py" "$@"
