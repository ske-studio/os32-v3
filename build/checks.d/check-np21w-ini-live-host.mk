CHECK_PAR_ORDER += 043:check-np21w-ini-live-host
# NP21/W の ini ライブ変更。ケースと変異はこの専用列で回す
# (票: tools/tests/np21w_ini_live_tdd.md)。
check-np21w-ini-live-host:
	python3 -B tools/tests/test_np21w_ini_live.py $(MUT)

.PHONY: check-np21w-ini-live-host
