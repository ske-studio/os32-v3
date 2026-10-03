CHECK_PAR_ORDER += 079:check-cfg-host
# libos32cfg / cfg コマンドのホスト TDD (票 S2-C)。実 SQLite + 実 kapi_db.c + RAM backend。
check-cfg-host:
	python3 -B tools/tests/test_cfg.py

.PHONY: check-cfg-host
