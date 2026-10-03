CHECK_PAR_ORDER += 113:check-db-caller-host
# T2d d5: actual DB wrappers and output guards through caller copy/walk.
check-db-caller-host:
	$(call host32_check,test_db_caller.py)

.PHONY: check-db-caller-host
