CHECK_PAR_ORDER += 102:check-hsync-h2-host
# hsync の置換安全化 (票 H2、docs/archive/shell/TASK_H2.md §2-3 / §2-4)。H1 / H3 と
# 同じく実物の userland/system/hsync.c を #include し、贋 FS に O_EXCL /
# sys_rename (公開の前に失敗 / 公開の後に失敗 / 判定不能) / ROFS / st_nlink /
# api->version を持たせて回す。公開の判定が **st_ino** であること、NOSYS で
# 直接上書きへ落ちないこと、予約名 .hs~ の掃除が st_nlink を見ないこと、
# 保護が予約より優先されること、KAPI v53 未満を既定で断ることを見る。
# 記録は tools/tests/hsync_h2_tdd.md。
check-hsync-h2-host:
	python3 -B tools/tests/test_hsync_h2.py --target $(MUT)

.PHONY: check-hsync-h2-host
