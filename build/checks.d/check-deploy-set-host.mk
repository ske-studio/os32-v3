CHECK_PAR_ORDER += 158:check-deploy-set-host

# T2h E3 / E1-a: 正常対照、欠損・改変・相互照合の負例、写しでの変異。
check-deploy-set-host:
	python3 -B tools/tests/test_deploy_set.py $(MUT)

.PHONY: check-deploy-set-host
