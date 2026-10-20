CHECK_PAR_ORDER += 194:check-rshell-peek-host
# ホスト道具 tools/rshell_peek.py (rshell_serial.py の `peek`、TASK_KPEEK §3
# 段 T3)。偽の応答・ELF・VK32・nm で: 行の連番・和・端数行・ok 行の len と
# 要求長・crc32・frames・err 行の照合 (違えば rc 1 でファイル無し)、64 KB の
# 分割、記号は ELF の os32_build_commit と VK32 の CRC が `ver` と 2 つとも
# 合うときだけ (K4)。--mutate は写しの上で変異させるので並列で回せる。
check-rshell-peek-host:
	python3 -B tools/tests/test_rshell_peek.py $(MUT)

.PHONY: check-rshell-peek-host
