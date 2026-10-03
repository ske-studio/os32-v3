CHECK_PAR_ORDER += 116:check-nano-adapter-host
# T2f f1b: real nano, private SDK adapter and opt-in link gate.
check-nano-adapter-host:
	$(call host32_check,test_nano_adapter.py)

