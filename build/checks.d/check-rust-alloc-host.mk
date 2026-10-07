CHECK_PAR_ORDER += 117:check-rust-alloc-host
# f8: shared GlobalAlloc alignment, raw-base release and overflow.
check-rust-alloc-host:
	python3 -B tools/tests/test_rust_alloc.py $(MUT)
.PHONY: check-rust-alloc-host
