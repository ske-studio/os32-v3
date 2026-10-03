CHECK_PAR_ORDER += 044:check-gshell-host
# gshell の入力部・WM をホスト ABI の代用 (host/mocks.rs) で走らせる。
# --mutate は票 KBD_NAV の K1 の変異 (host/integration.py の MUTATIONS) を
# 一時の写しに当てて RED を確かめる (ソースは書き換えない)。
check-gshell-host:
	python3 userland/gshell/host/integration.py $(MUT)

.PHONY: check-gshell-host
