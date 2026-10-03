CHECK_PAR_ORDER += 085:check-host-lib-host
# libos32host + wget/lpr/hclip/hdate のホスト TDD (票 N3)。実物のソースを #include し、
# KAPI / libos32host の関数だけを贋物に。記録は tools/tests/n3_tdd.md。
check-host-lib-host:
	python3 -B tools/tests/test_host_lib.py --target

.PHONY: check-host-lib-host
