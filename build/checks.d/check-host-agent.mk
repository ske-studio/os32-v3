CHECK_PAR_ORDER += 083:check-host-agent
# tools/host_agent.py v2 (ワイヤ v2 の Agent 側、票 N1 段 1)。贋 OS32 が
# フレームを直接組んで rid 台帳 / 3 way HELLO / 墓標 / 枯渇停止を踏む。
check-host-agent:
	python3 -B tools/tests/test_host_agent.py

.PHONY: check-host-agent
