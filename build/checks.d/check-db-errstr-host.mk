CHECK_PAR_ORDER += 078:check-db-errstr-host
# TT2: API v50 の 25 ケースと所有者/寿命/障害注入 8 ケースも保持 (別 fixture、SQLite ビルド共有)。
# db_last_error / db_column_text の返り先 (票 TASK_DB_ERRSTR)。実 SQLite + 実 kapi_db.c。
# 見るのは「返り先が共有メモリの範囲内か」と「結果を上限まで書いても診断文が壊れないか」。
# --mutate は否定側 (上限の引き算を 1 か所戻す / カーネル番地のまま返す / NUL を置き忘れる)。
check-db-errstr-host:
	python3 -B tools/tests/test_db_errstr.py --target $(MUT)

.PHONY: check-db-errstr-host
