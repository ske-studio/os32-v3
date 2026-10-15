CHECK_PAR_ORDER += 088:check-e82557-desc-host
# LB2 byte serialization / stop evidence. Record: tools/tests/e82557_desc_tdd.md
check-e82557-desc-host:
	python3 -B tools/tests/test_e82557_desc.py --target $(MUT)

.PHONY: check-e82557-desc-host
