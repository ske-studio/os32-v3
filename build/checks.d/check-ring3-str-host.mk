CHECK_PAR_ORDER += 063:check-ring3-str-host
# exec/ring3_str.c — KAPI が CPL=3 へ **返す** 文字列の置き場 (票 T9 §12 R1)。
# カーネル帯には USER ビットが無いので、sys_getcwd がそのまま返すと CPL=3 の
# 呼び手が #PF で畳まれる。写し先 (トランポリンページの空き) の番地の式と
# 経路の分岐をホストで踏む。記録は tools/tests/t9_tdd.md。
check-ring3-str-host:
	python3 -B tools/tests/test_ring3_str.py

.PHONY: check-ring3-str-host
