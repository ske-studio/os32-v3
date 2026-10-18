CHECK_PAR_ORDER += 149:check-pcm-module-host

check-pcm-module-host:
	python3 -B tools/tests/test_pcm_module.py $(MUT)

.PHONY: check-pcm-module-host
