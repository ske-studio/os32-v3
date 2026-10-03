CHECK_PAR_ORDER += 084:check-net-link-host
# net/link.c (ワイヤ v2) + kapi/kapi_host.c (KAPI v51) のホスト TDD (票 N1 段 4)。
# 実物のソースを #include し、NIC / cli-sti / 100Hz タイマ / ディスパッチャだけを
# 贋物にする。対向は **実 Agent** (host_agent.py を UNIX ソケットで子プロセス起動)
# か台本。記録は tools/tests/n1_tdd.md。
check-net-link-host:
	python3 -B tools/tests/test_net_link.py --target

.PHONY: check-net-link-host
