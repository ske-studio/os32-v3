CHECK_PAR_ORDER += 192:check-font-source-host
# font の read lease (T7b-b1)。S1 は実物の fs/vfs_fd.c の VFS 部 (vfs_open_font /
# validate / read_at / close_font、公開 FD 経路の締め出し、vfs_close_owned、
# pinned の BUSY 判定、FD 予算)。S2 は kernel/font_source.c の lease 表・app の門
# (public KAPI font_source_* の実体)・shell 側の段 C/D。--target は
# i386-elf -Werror のコンパイル (vfs_fd.c と font_source.c)、--mutate は写しの上で変異するので並列で回せる。
.PHONY: check-font-source-host
check-font-source-host:
	python3 -B tools/tests/test_font_source.py --target $(MUT)
