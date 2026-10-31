# tools/tests/test_ext2_read_bound.py — 読取回帰試験

> 状態: **現行 (2026-10-31)**

`境界 fixture`（ext2_read_bound_host.md） の 6 case を host で実行し、
ext2_file.c の i386-elf GNU11・-Werror コンパイルも確認する。
変異は stream の種別検査（親 .c）と穴／エラー伝播（read_run.inc）を対象にし、
移動先の実装もそのまま取り込んで実行時に失敗させる。
