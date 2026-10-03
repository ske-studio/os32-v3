CHECK_PAR_ORDER += 105:check-fs-kind-callers-host
# 種別が「分からない」とき cp / mv / rm が断る (TASK_FS_TYPE §3)。実物の
# cmd_fs_shared.c + cmd_file.c を #include し、sys_stat と sys_ls の両方を IO にして、
# 呼び出し元 5 箇所が「不明」をファイルと読まず、open / mkdir / rename / unlink を
# 呼ばないことを受け手で見る。§6 は継承バグ台帳の `cp -r` — 失敗する経路で
# 宛先に空のディレクトリを残さない (収集してから mkdir / mkdir の戻り値を見る /
# 既に在るディレクトリへの上書きコピーだけは通す)。--mutate は否定側。
# 記録は tools/tests/fs_kind_callers_tdd.md。
check-fs-kind-callers-host:
	python3 -B tools/tests/test_fs_kind_callers.py --target $(MUT)

.PHONY: check-fs-kind-callers-host
