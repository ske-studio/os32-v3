CHECK_PAR_ORDER += 131:check-kcg-boot-host
# T7a-1b: retained NOSYS slot and ROM-only reads, no low-memory access.
check-kcg-boot-host:
	$(call host32_check,test_kcg_boot.py)

.PHONY: check-kcg-boot-host
