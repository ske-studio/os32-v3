CHECK_PAR_ORDER += 070:check-hsync-h1-host
# hsync の同サイズ更新の検出 (票 H1、docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md
# §9 の A01〜A13)。実物の userland/system/hsync.c を #include し、KernelAPI だけを
# オンメモリの贋 FS に差し替えて回す。fs/hostdrv_stat_rules.inc (HostDrv の stat
# 失敗の是正) と CRC ストリーム核の既知ベクトルも同じ翻訳単位で見る。
# 記録は tools/tests/h1_tdd.md。
check-hsync-h1-host:
	python3 -B tools/tests/test_hsync_h1.py --target

.PHONY: check-hsync-h1-host
