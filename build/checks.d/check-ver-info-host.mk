CHECK_PAR_ORDER += 214:check-ver-info-host

# V12 is the aggregate acceptance gate for V1--V11, including existing readers.
check-ver-info-host: check-ver-version-host check-ver-layout-host check-ver-state-name-host \
                    check-ver-services-host check-ver-gfx-host check-ver-collect-host \
                    check-ver-format-host check-ver-api-host check-ver-shell-host \
                    check-rshell-peek-host check-rshell-recover-host
	python3 -B tools/tests/test_about_info.py $(MUT)
	$(call host32_check,test_ver_accept.py)

.PHONY: check-ver-info-host
