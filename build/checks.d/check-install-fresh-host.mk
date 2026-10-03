CHECK_PAR_ORDER += 082:check-install-fresh-host
# install (無印) の通常インストール経路のホスト TDD (票 S3I2-I)。実 install.c + KAPI の贋物、--target で IdeInfo 96B の表明。
check-install-fresh-host:
	python3 -B tools/tests/test_install_fresh.py --target

.PHONY: check-install-fresh-host
