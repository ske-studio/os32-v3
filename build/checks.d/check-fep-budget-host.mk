CHECK_PAR_ORDER += 152:check-fep-budget-host
# T4a-4: bundled SQLite/MEMSYS5 and real IME, host memory counts only.
check-fep-budget-host:
	$(call host32_check,test_fep_budget.py)
