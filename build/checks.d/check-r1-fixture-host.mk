# T2h stop hooks, isolated images, and isolated NHD tool contract.
CHECK_PAR_ORDER += 180:check-r1-fixture-host
check-r1-fixture-host: export RUST_LZ4_LIB := $(RUST_LZ4_LIB)
check-r1-fixture-host:
	$(call host32_check,test_r1_fixture.py)
	python3 tools/tests/test_nhd_deploy_profile.py --set $(BUILD_OUT)/deploy-set.json $(if $(filter 1,$(MUTATE)),--mutate)

.PHONY: check-r1-fixture-host
