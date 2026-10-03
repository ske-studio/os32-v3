CHECK_PAR_ORDER += 016:check-kapi-out
# KAPI の出力ポインタ宣言 (sdk/kapi.json の "out") と、そこから生成される
# 書き込み可検査 (票 docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md 受入 G1)。
# 見るのは 2 つ: **非 const のポインタ引数があるのに "out" が無いエントリで
# 生成が落ちること** (書き忘れがそのまま穴になるため) と、len / size / unit の
# 解釈と検査順 (**全範囲を検査してから target を呼ぶ**)。合成した kapi.json を
# 一時ディレクトリで回すので実物の生成物には触らない (check-par で回せる)。
# 否定側は `python3 -B tools/tests/test_kapi_out.py --mutate` (生成器を書き換えて
# 戻すので check では回さない)。記録は tools/tests/kapi_out_tdd.md。
check-kapi-out:
	@python3 -B tools/tests/test_kapi_out.py

.PHONY: check-kapi-out
