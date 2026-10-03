CHECK_PAR_ORDER += 055:check-rshell-serial-host
# ホスト道具 tools/rshell_serial.py の「応答の識別」。実機の rshell と
# 話すときに **EOT の対応が 1 つずれる** 事故を止める (票 TASK_SERIAL_VFAST
# の Codex レビュー往復 3 ⑤⑥)。見るのは 3 つ: エコー行を **行全体** で
# 比べること (`> version` を `ver` の応答と読まない)、`exit` がエコーを
# 出さない例外、`ver` の成功を **本文 (`Build:`) + エコー + EOT** の 3 つで
# 判定すること (先行コマンドの EOT を拾っただけで成功と読まない)。
# **タイミングで起きる事故なので実機では「たまたま通る」** — 規則そのものを
# ここで固定する。pyserial もシリアルポートも要らない。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/serial_vfast_tdd.md。
check-rshell-serial-host:
	python3 -B tools/tests/test_rshell_serial.py $(MUT)

.PHONY: check-rshell-serial-host
