CHECK_PAR_ORDER += 042:check-np21w-ctl-host
# NP21/W の停止・起動 (tools/np21w_ctl.py)。ケースと変異はこの専用列で回す。
check-np21w-ctl-host:
	python3 -B tools/tests/test_np21w_ctl.py $(MUT)

.PHONY: check-np21w-ctl-host
