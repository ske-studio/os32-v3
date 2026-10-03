CHECK_PAR_ORDER += 129:check-display-cleanup-host
# Display cleanup regression; record: tools/tests/display_cleanup_tdd.md.
check-display-cleanup-host:
	python3 -B tools/tests/test_display_cleanup.py $(MUT)

.PHONY: check-display-cleanup-host
