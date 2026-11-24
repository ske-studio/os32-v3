#!/bin/sh
# Require all providers assigned to the selected integration stage in the ELF.
set -eu
case "${1:-}" in IA|IS|IL) ;; *) echo 'usage: av_no_weak.sh IA|IS|IL [kernel.elf [symbol-prefix]]' >&2; exit 2;; esac
"${NM:-nm}" -g --defined-only "${2:-build/out/kernel.elf}" |
    awk -v stage="$1" -v prefix="${3:-}" 'NR == FNR {if ($1 == stage && index($2, prefix) == 1) {required[$2] = 1; found = 1} next}
        $2 == "T" {delete required[$3]}
        END {if (!found) exit 1; for (name in required) {print "missing strong definition:", name; failed = 1}; exit failed}' \
        tools/checks/av_wiring.txt -
