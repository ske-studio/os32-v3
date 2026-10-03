CHECK_PAR_ORDER += 038:check-memmap-host
# 記録: tools/tests/memmap_tdd.md (票 docs/archive/kernel_v21/TASK_KSTACK_USER.md)
#
#   check-memmap       実ツリーの地図を検査する。帯どうしの重なり・範囲の逆転・
#                      カーネル本体の予算超過・memmap.h の値を写している場所
#                      (build/os32.ld / kernel/kentry.asm / SDK) のずれ・
#                      docs/02_memory.md の鮮度。**kernel.map が要る**ので
#                      カーネルを組んでいないと 2 で止まる。
#   check-memmap-host  合成した地図で道具と自己診断の挙動を見る。実ツリーの
#                      番地に依存しないので、番地を動かしても腐らない。
# 2026-09-17 (決裁 D1/D2) の配置で両方とも緑になったので check: の列に入れた。
check-memmap-host:
	python3 -B tools/tests/test_memmap_gen.py $(MUT)
	python3 -B tools/tests/test_memmap_boot.py $(MUT)

.PHONY: check-memmap-host
