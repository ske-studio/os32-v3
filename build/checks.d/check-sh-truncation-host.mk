CHECK_PAR_ORDER += 067:check-sh-truncation-host
# シェルが入力を黙って切り詰める経路 (票 docs/archive/shell/TASK_SH_TRUNCATION.md)。
# tools/tests/sh_truncation_host.c が userland/shell/main.c を丸ごと #include し、
# **登録表も execute_command も実物のまま**回す (sh_shell_host.c は
# execute_command をスタブにしているのでこの経路を試験できない)。
# 段 2 の時点で T1 (`if` の比較) と §2-1 (スクリプト中で断ったら打ち切る) が
# 入っている。否定側は `--mutate` (印を立てない / 消し忘れる / 対話でも
# 打ち切る 版などが RED になる)。記録は tools/tests/sh_truncation_tdd.md。
check-sh-truncation-host:
	python3 -B tools/tests/test_sh_truncation.py $(if $(mut_on),--mutate-rshell)

.PHONY: check-sh-truncation-host
