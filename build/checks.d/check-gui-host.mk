CHECK_PAR_ORDER += 080:check-gui-host
# libos32gui の os32gui_cfg_* wrapper の分岐 (票 S2-W)。C の実体は贋物。
check-gui-host:
	python3 -B tools/tests/test_gui_reattach.py $(MUT)
	cargo test --manifest-path userland/rust/libos32gui/host_tests/Cargo.toml --target x86_64-unknown-linux-gnu --offline

.PHONY: check-gui-host
