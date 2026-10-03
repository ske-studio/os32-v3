CHECK_PAR_ORDER += 031:check-ne2000-ring

# NE2000 受信リング計算のホスト試験 (I/O 無し)。make check から呼ばれる。
# I/O の実動作試験を代替しない (docs/tasks/network/PLAN.md §6)。
check-ne2000-ring:
	@mkdir -p $(BUILD_OUT)
	@gcc $(C_STD) -Wall -Wextra -DNE2K_HOST_TEST -Idrivers \
	    -o $(BUILD_OUT)/ne2000_ring_test tools/tests/ne2000_ring_test.c drivers/ne2000_ring.c
	@$(BUILD_OUT)/ne2000_ring_test

.PHONY: check-ne2000-ring

