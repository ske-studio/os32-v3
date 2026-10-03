CHECK_PAR_ORDER += 035:check-term-render
# 純粋描画アダプタ。人工glyphによるホスト試験であり実ROM描画の検証ではない。
check-term-render:
	cargo test --manifest-path userland/libos32term_render/Cargo.toml --target x86_64-unknown-linux-gnu --offline
	cargo check --manifest-path userland/libos32term_render/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline

.PHONY: check-term-render
