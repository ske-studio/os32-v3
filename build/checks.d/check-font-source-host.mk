CHECK_PAR_ORDER += 192:check-font-source-host
# font の read lease (T7b-b1)。S1 は実物の fs/vfs_fd.c の VFS 部 (vfs_open_font /
# validate / read_at / close_font、公開 FD 経路の締め出し、vfs_close_owned、
# pinned の BUSY 判定、FD 予算)。lease 表と KAPI の門は S2 で足す。--target は
# i386-elf -Werror のコンパイル、--mutate は写しの上で変異するので並列で回せる。
.PHONY: check-font-source-host
check-font-source-host:
	python3 -B tools/tests/test_font_source.py --target $(MUT)
