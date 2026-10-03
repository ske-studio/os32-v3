CHECK_PAR_ORDER += 090:check-v86-gcap-host
# `v86 -g` の記録器と引数の判定 (kernel/v86_gcap_math.c、票 TASK_PEGC480_REALHW §3 段 1)。
# 実機の ROM の INT 18h AH=31h/30h を V86 で呼び、その間の OUT を畳まずに積む。
# 見るのは: 通すポートの表 (09A8h / 09A0h / 60h〜7Ah の偶数 / A0h〜A6h)、
# 打ち切る命令 (INS/OUTS と 66h 付きの IN/OUT EAX、バイト形は 66h でも通す)、
# 列の順序・seq (IN も数える)・幅での値の切り方・溢れ (513 件目で overflow、
# 実機へは通し続ける)・IN の表、幅で ops の口 (8 / 16) を選ぶこと、
# AH=31h の値から並び (NP21/W の bit2 / Bible の bit3) と 640x480 の AH=30h を
# 決める表 (**両方・どちらでもない・印のまま**)。**NP21/W でも実機でも狙って
# 踏めない分岐**。ゲストのバイト列が正本の asm と同じかも見る (nasm が要る)。
# --target は i386-elf で kernel/v86_gcap.c ごと通す。
check-v86-gcap-host:
	python3 -B tools/tests/test_v86_gcap.py --target $(MUT)

.PHONY: check-v86-gcap-host
