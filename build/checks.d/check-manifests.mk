CHECK_PAR_ORDER += 022:check-manifests
# 配備マニフェストと app.conf の参照先を検査する (要 make all)
check-manifests:
	@python3 tools/check_manifests.py

.PHONY: check-manifests
