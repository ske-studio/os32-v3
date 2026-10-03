CHECK_PAR_ORDER += 029:check-arch-asm
# カーネル側 C ソースの hlt / cli / sti 直書き検査 (移植性の準備、順序 2)。
# これらは include/io.h の原始命令 (_halt / _idle / _stop / _enable /
# _disable / irq_save / irq_restore) 経由で使い、arch 差し替えの境界を
# io.h 1 枚に閉じ込める。切り出せない箇所は asm の直前に ARCH-ASM-OK と
# 理由を書く。
check-arch-asm:
	@python3 tools/check_arch_asm.py
	@python3 tools/tests/test_clang_ast.py $(MUT)

.PHONY: check-arch-asm
