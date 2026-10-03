CHECK_PAR_ORDER += 086:check-lan-bridge-host
# tools/lan_bridge.py (実機の NIC ↔ host_agent.py の橋、票 TASK_LAN_82557 L-D)。
# root も実 NIC も要らない — 橋の `--fake-nic` (NIC の差し替え口) に贋 OS32 を
# UNIX ソケットで繋ぎ、反対側には **実物の host_agent.py** を `--unix` で子プロセス
# 起動して、L0 (3 way HELLO) + PING が**両方向**通ることを見る。
# 見るのは 4 つ: FrameStream の枠付けが host_agent.py とバイト単位で同じか、
# EtherType 0x88B5 以外を Agent へ流さないか、逆向き (agent -> NIC) が生きているか、
# 実 NIC を開けないときに CAP_NET_RAW を名指して止まるか。
# **AF_PACKET の経路そのものはここでは踏めない** — 実機 + Ubuntu ノートで PM が見る。
# --mutate は写しの上で変異させるので並列 (check-par) で回せる。
# 記録は tools/tests/lan_bridge_tdd.md。
check-lan-bridge-host:
	python3 -B tools/tests/test_lan_bridge.py $(MUT)

.PHONY: check-lan-bridge-host
