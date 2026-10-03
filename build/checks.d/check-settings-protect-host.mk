CHECK_PAR_ORDER += 069:check-settings-protect-host
# 通常配備が /etc/settings.db* を作らない・上書きしない・消さない (票 S0-D / D0)。
# temp dir + mock だけで走り、sudo / mount / 実配備は試験側が遮断する。記録は tools/tests/s0_tdd.md 節 D。
check-settings-protect-host:
	python3 -B tools/tests/test_deploy_protect.py
	python3 -B tools/tests/test_hsync_protect.py

.PHONY: check-settings-protect-host
