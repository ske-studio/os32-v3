CHECK_PAR_ORDER += 087:check-e82557-eeprom-host
# 82557 EEPROM: real pin sequencer, 93C46/66 clocked model,
# independent minimum-time T / exact-sequence S and isolated mutants.
# Record: tools/tests/e82557_eeprom_tdd.md
check-e82557-eeprom-host:
	python3 -B tools/tests/test_e82557_eeprom.py --target $(MUT)

.PHONY: check-e82557-eeprom-host
