CHECK_PAR_ORDER += 011:check-pcm-cs4231-host
# CS4231 (MATE-X PCM) の再生ドライバ (drivers/pcm_cs4231_math.c + pcm_cs4231.c)。
# 票 docs/tasks/v3/TASK_PCM_CS4231.md §2-3 / E1。
# **NP21/W では踏めない分岐がここの主目的**: 1 周回った観測 (同じ半分で位置が
# 戻る)、補充の余裕 (REFILL_MARGIN) を割った切り替え、drain の 3 段階、初期化列
# と停止列の**順序**、入口ガードでの装置アクセス **0 回**、close / reclaim が
# 各状態から **1 度だけ**解放すること。
# io.h だけ tools/tests/pcm_hostshim/io.h で差し替え、ポートと割り込み禁止を
# 模型へ回す (b8_hostdrv と同じ作法)。
# --target はカーネルと同じ i386-elf で I/O を出す側ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/pcm_cs4231_tdd.md。
check-pcm-cs4231-host:
	python3 -B tools/tests/test_pcm_cs4231.py --target $(MUT)

.PHONY: check-pcm-cs4231-host
