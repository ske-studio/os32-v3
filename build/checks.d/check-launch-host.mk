CHECK_PAR_ORDER += 062:check-launch-host
# exec/launch.c の起動要求表 (票 T9 D3)。実物のソースを exec/appslot.c と同じ
# 翻訳単位で走らせ (要求者・子の所有・token の照合は AppSlot を引く)、同じ
# ソースが i386-elf-gcc -Werror でも通ることを見る。記録は tools/tests/t9_tdd.md。
check-launch-host:
	python3 -B tools/tests/test_launch.py

.PHONY: check-launch-host
