CHECK_PAR_ORDER += 139:check-build-no-deploy
check-build-no-deploy:
	@python3 -B tools/tests/test_build_no_deploy.py

.PHONY: check-build-no-deploy
