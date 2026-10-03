CHECK_PAR_ORDER += 066:check-sh-shell-host
# userland/shell/sh_redraw.inc の行再描画 (実装レビュー blocker 1 — GUI 中は
# コンソール座標が動かない) と userland/shell/cmd_script.c の source 中 exit
# (blocker 2 / D2(d))。どちらも実物のソースを tools/tests/sh_shell_host.c が
# そのまま #include する。記録は tools/tests/t9_tdd.md。
check-sh-shell-host:
	python3 -B tools/tests/test_sh_shell.py

.PHONY: check-sh-shell-host
