CHECK_PAR_ORDER += 024:check-lzss-pkg-host
# Python writer/reader and real ILP32 pkg_parse/pkg_extract, deterministic seeds.
check-lzss-pkg-host:
	$(call host32_check,test_lzss_pkg.py)

.PHONY: check-lzss-pkg-host
