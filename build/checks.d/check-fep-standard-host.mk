CHECK_PAR_ORDER += 153:check-fep-standard-host
# T4c-3: product ILP32 admission cap and real ext2 learning.
check-fep-standard-host:
	$(call host32_check,test_fep_standard.py)

.PHONY: check-fep-standard-host
