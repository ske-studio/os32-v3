#!/bin/bash
# Manual AST alignment audit (kernel|user|all); parse failure is nonzero.
set -eu
cd "$(dirname "$0")/.."
PYTHONPATH="$PWD/tools${PYTHONPATH:+:$PYTHONPATH}" exec python3 -m clang_ast.align "$@"
