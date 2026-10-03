CHECK_PAR_ORDER += 056:check-serialfs-host
# シリアル越しの /host (票 TASK_SERIAL_HOSTFS 部品 B、受入 T1 と T5 のホスト部分)。
# ゲスト側の実物 (fs/sfs_proto.c・fs/sfs_client.c・fs/serialfs.c、drivers/serial.c の
# ゲート、rshell の ESC と `sfs run` の行、hsync の vmkernel.old の判定) を偽の線と
# 仮想の時計で、ホスト側の実物 (tools/serialfs_host.py・tools/rshell_serial.py) を
# 偽のポートで回し、C と Python を相互に照合したうえで実時間のパイプで結合する。
# 障害の注入: 応答の喪失・遅延 (番号のずれ)・CRC 破損・番号 / セッション違い・
# ホストの停止・ごみの連続・再送で副作用が二重にならない・セッション中に rshell へ
# フレームが漏れない・`sfs run` の行の外で `cat` の本文のフレームに答えない。
# --target はカーネルと同じ i386-elf -Werror。--mutate は写しの上で変異 (組めない
# 変異は ERROR、恒等の対照を C と Python に 1 本ずつ)。記録は tools/tests/serialfs_tdd.md。
check-serialfs-host:
	python3 -B tools/tests/test_serialfs.py --target $(MUT)

.PHONY: check-serialfs-host
