CHECK_PAR_ORDER += 900:check-ime-display-host

check-ime-display-host:
	$(call host32_check,test_ime_display.py)

.PHONY: check-ime-display-host
