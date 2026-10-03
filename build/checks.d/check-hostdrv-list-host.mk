CHECK_PAR_ORDER += 071:check-hostdrv-list-host
# hdrv_list_dir の列挙ループ (票 H1 の「I/O 失敗を成功にしない」/ 対象
# 「HostDrv のエラー処理」)。実物の fs/hostdrv_list_rules.inc を #include し、
# hostdrv_query_dir に当たる 1 件取得だけを贋物にして、(a) 途中で負値 /
# (b) 件数上限での打ち切り が VFS_OK で返らないことを見る。
# 記録は tools/tests/h1_tdd.md。
check-hostdrv-list-host:
	python3 -B tools/tests/test_hostdrv_list.py --target

.PHONY: check-hostdrv-list-host
