CHECK_PAR_ORDER += 054:check-pci-bind-host
# PCI の結線表 (drivers/pci_bind_match.c)。票 TASK_HAL_WIRING §1-4。
# **NP21/W には PCI が無い**ので、この層はエミュレータでは 1 行も走らない。
# 一致規則 (4 欄の AND と「任意」)、DECLINE → 次の候補へ、QUARANTINE →
# **その BDF の探索を打ち切る**、理由が候補ごとに初期化されること、
# 線の様子を**読む時点で合成する** (結線後に隔離されても BOUND のまま
# line_state だけ変わる) を見る。probe は関数ポインタなので偽 driver で足りる。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/pci_bind_tdd.md。
check-pci-bind-host:
	python3 -B tools/tests/test_pci_bind.py --target $(MUT)

.PHONY: check-pci-bind-host
