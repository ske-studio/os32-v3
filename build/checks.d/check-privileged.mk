CHECK_PAR_ORDER += 028:check-privileged
# 意図した #GP 試験だけ理由・個数つきで許可し、残りは strict で拒否する。
check-privileged:
	@python3 -B tools/tests/test_check_privileged.py
	@python3 tools/check_privileged.py --strict

.PHONY: check-privileged
