CHECK_PAR_ORDER += 057:check-lanfs-host
# Real SfsClient/SfsLan and serialfs_host.Server, fake NIC and virtual clock.
# Record: tools/tests/lanfs_tdd.md
check-lanfs-host:
	python3 -B tools/tests/test_lanfs.py --target $(MUT)

.PHONY: check-lanfs-host
