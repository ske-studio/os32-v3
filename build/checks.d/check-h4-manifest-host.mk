CHECK_PAR_ORDER += 103:check-h4-manifest-host
# 配備マニフェストと世代の確認 (票 H4、docs/archive/shell/TASK_H4.md §2-1〜§2-3)。
# **読む側**は H1 / H2 / H3 と同じく実物の userland/system/hsync.c を #include し、
# 贋 FS の /host/.deploy/manifest.txt に票が挙げた壊し方を注入して回す。
# 名札が無い配備元が今までどおり動くこと、壊れた名札を捨てること、断るのが
# `--expect-build` かつ全体同期のときだけであること、**名札が読めないことを
# 「一致」と扱わない**こと、名札を信じて内容比較を省かないことを見る。
# format=2 (票 TASK_KAPI_DATA_FIELDS) で名札に kapi= / kapi_version= が入り、
# 配置違い・版が新しい・確かめられない名札は既定で断る (KAPI の門、case_kapi)。
# 既存の H4 の段は `--force-kapi` を付けて回す。
# **書く側** (tools/hostdrv_deploy.py) は一時ディレクトリだけで回し、全件成功の
# 後にだけ書くこと・失敗したら既にある名札を消すこと・一時ファイル + 置き換え・
# --no-manifest を見る。両層が同じ名札を指していることは静的に突き合わせる。
# --mutate は否定側 (壊れた名札を一致と扱う版 / 名札の CRC で比較を省く版など)。
# 記録は tools/tests/h4_manifest_tdd.md。
check-h4-manifest-host:
	python3 -B tools/tests/test_h4_manifest.py --target $(MUT)
	python3 -B tools/tests/test_hostdrv_manifest.py $(MUT)

.PHONY: check-h4-manifest-host
