CHECK_PAR_ORDER += 015:check-kapi-version
# 手書きされた KAPI バージョンが kapi.json とずれていないか検査する。
# ずれていると「どれが本当の版か」が分からなくなる。
check-kapi-version:
	@python3 tools/check_kapi_version.py

.PHONY: check-kapi-version
