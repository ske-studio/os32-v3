CHECK_PAR_ORDER += 036:check-t5a-host
# T5aの純粋状態・座標・所有権試験。guest.rsの実行は含まない。
check-t5a-host:
	cargo test --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml --target x86_64-unknown-linux-gnu --offline

.PHONY: check-t5a-host
