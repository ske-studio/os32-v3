CHECK_PAR_ORDER += 136:check-guest-acceptance-host
# Offline acceptance index, PT0 comparison and HTTP TVDM parser; no emulator.
check-guest-acceptance-host:
	python3 -B tools/gen_guest_acceptance.py --check
	python3 -B tools/tests/test_guest_acceptance.py $(MUT)

.PHONY: check-guest-acceptance-host
