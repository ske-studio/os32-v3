CHECK_PAR_ORDER += 191:check-shell-service-host
# 常駐 shell の内部入口 (T7b-b2 S3a、exec/shell_service.c)。service 表の
# publish / retire、入口 wrapper の段 A (戻り番地が現行 shell text の中) と
# 段 B (文脈: open/close は shell の通常文脈、read/validate は通常文脈か登録済み
# WM 区間)、reload で旧 lease が STALE になることをホストで踏む。段 C/D
# (kernel/font_source.c) は試験側の贋物。--target は i386-elf -Werror の
# コンパイル、--mutate は写しの上で変異するので並列で回せる。
.PHONY: check-shell-service-host
check-shell-service-host:
	python3 -B tools/tests/test_shell_service.py --target $(MUT)
