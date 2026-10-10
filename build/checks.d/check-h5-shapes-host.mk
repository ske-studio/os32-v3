CHECK_PAR_ORDER += 152:check-h5-shapes-host
check-h5-shapes-host:
	$(call host32_check,test_h5_shapes.py)

.PHONY: check-h5-shapes-host
