CHECK_PAR_ORDER += 213:check-ver-gfx-host

check-ver-gfx-host:
	$(call host32_check,test_ver_gfx.py)

.PHONY: check-ver-gfx-host
