CHECK_PAR_ORDER += 037:check-memory-host
check-memory-host:
	python3 -B tools/tests/test_physmem.py
	python3 -B tools/tests/test_paging_bounds.py $(MUT)
	python3 -B tools/tests/test_paging_bounds.py --rebuild nonmaster
	python3 -B tools/tests/test_paging_bounds.py --rebuild rollback
	python3 -B tools/tests/test_app_band_pde.py $(MUT)
	python3 -B tools/tests/test_pgalloc_model.py
	python3 -B tools/tests/test_pgalloc_range.py
	python3 -B tools/tests/test_highram_stage.py
	python3 -B tools/tests/test_memory_boot.py $(MUT)
	python3 -B tools/tests/test_ledger.py $(MUT)
	python3 -B tools/tests/test_lease.py $(MUT)
	python3 -B tools/tests/test_exec_r1.py $(MUT)
	python3 -B tools/tests/test_kstop.py $(MUT)
	python3 -B tools/tests/test_device_reservation.py $(MUT)
	python3 -B tools/tests/test_sbrk_tier.py $(MUT)
	python3 -B tools/tests/test_app_bb_overlap.py $(MUT)
	python3 -B tools/tests/test_gfx_boot.py $(MUT)

.PHONY: check-memory-host
