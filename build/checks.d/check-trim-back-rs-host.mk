# T2g g4: shared Rust fixture, real loop/allocator/kernel and STOP/WM trace.
CHECK_PAR_ORDER += 120:check-trim-back-rs-host
check-trim-back-rs-host:
	$(call host32_check,test_trim_back_rs.py)

.PHONY: check-trim-back-rs-host
