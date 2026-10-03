CHECK_PAR_ORDER += 072:check-fs-kind-host
# シェルの種別判定と cp -r の宛先階層 (票 H1 / 往復 3 の B5)。実物の
# cmd_fs_shared.c + cmd_file.c を #include し、sys_stat は正しいまま sys_ls だけを
# FULL / IO にして、列挙のエラーが「ディレクトリでない」に化けないことを見る。
check-fs-kind-host:
	python3 -B tools/tests/test_fs_kind.py --target

.PHONY: check-fs-kind-host
