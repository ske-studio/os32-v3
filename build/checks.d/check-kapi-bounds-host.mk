CHECK_PAR_ORDER += 001:check-kapi-bounds-host
# KAPI coordinates, indices and counts: real targets, private-copy mutants.
check-kapi-bounds-host:
	python3 -B tools/tests/test_kapi_bounds.py $(MUT)
	python3 -B tools/tests/test_gfx_bounds.py $(MUT)

