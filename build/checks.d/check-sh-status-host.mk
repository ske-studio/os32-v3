CHECK_PAR_ORDER += 100:check-sh-status-host
# 終了コードの配線と `$?` (票 docs/archive/shell/TASK_EXIT_STATUS.md)。
# tools/tests/sh_status_host.c が userland/shell/main.c を丸ごと #include し、
# **登録表も execute_command も実物のまま**回す (受入 R2)。同じ 1 本を
# **2 通り** — 常駐 (exec_run + exec_last_result、KAPI v55) と
# -DSHELL_AS_APP (要求表経由) — にコンパイルして両方走らせる。種別ごとの
# 写像はビルドで実装が違うので、片方だけでは配線を見たことにならない。
# 否定側は `--mutate` (種別を値から作る / PATH 走査を値で止める /
# `exit` の値を捨てる / `set -e` を拾わない 版などが RED になる)。
# 記録は tools/tests/sh_status_tdd.md。
check-sh-status-host:
	python3 -B tools/tests/test_sh_status.py $(MUT)

.PHONY: check-sh-status-host
