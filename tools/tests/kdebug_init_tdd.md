# kdebug init / loader (S1a)

票: [TASK_DBGREF](../../docs/tasks/v3/TASK_DBGREF.md) §3-4・§4・S1a。
`make check-kdebug-init-host` は実物 init を ILP32 で呼び、候補表の完全一致と
import 呼出しゼロを検査する。`--mutate` は kprintf 呼出し・候補未設定を RED にする。

実物 module.c / module_image.c と実際に生成した部品を組み合わせ、VFS/ledger
だけを偽物にして実行する。OFF、ON、open/stat/header read/owner/alloc/body read
失敗、img_valid、header/pair/ABI/CRC/reloc/import/export の拒否、init 失敗を検査。
全拒否で公開表ゼロ、close、ページ全返却、owner retire、診断一行を確認する。
正常では prefix を破壊しても body の関数だけが公開され、body ページだけが残る。

ゲストの実 ledger・boot.log と起動継続の判定は PM の G1 受入で行う。

## LB-1 ABI v2 / nic_trace

- 18 slots, slot 17 NULL, slot 18 relocated into the retained body; version 1 rejects with ABI_MISMATCH before init. S4 imports = 223.
- Trace harness includes actual nic_trace.c: seq/crc, append then sync, fresh TRY, malformed/overflow/stale marker, write/sync/short-write failure and sticky trace=off (12 scenarios plus fresh TRY).
- Mutants: omitted sync, failure latch, image CRC comparison, and fresh TRY reset must each fail.

## T4a-1

The real S4 image (235 imports, ops v2 / 18 slots) still reaches RUNNING and the
post-exec selftest, with zero imports during init. KDEBUG=0 performs no open or
owner allocation. Added loader cases reject future format, a different module
ID, wrong phase, missing PAIR, duplicate import slots, duplicate relocations,
duplicate exports and overlapping tables before init/publication. Rejected cases
retain zero public ops and reclaim pages/FD/owner through the existing harness.
For OFF/rejected images, init starts with a UD2 probe (outside relocations) so any
early call fails the test even though the real init has no side effects.
Guest operation and deployment remain assigned to later integration gates.
