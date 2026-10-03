CHECK_PAR_ORDER += 081:check-install-recover-host
# install --recover-settings / --revert-settings のホスト TDD (票 S3-I)。実 SQLite + 実 kapi_db.c + RAM backend。
check-install-recover-host:
	python3 -B tools/tests/test_install_recover.py

.PHONY: check-install-recover-host
