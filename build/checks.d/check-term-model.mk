CHECK_PAR_ORDER += 034:check-term-model
# 独立端末モデルのホスト試験。guest用Cargo設定を避けるためrootから実行。
# ゲストクロスリンク・描画・CUI統合の検証ではない。
check-term-model:
	cargo test --manifest-path userland/libos32term/Cargo.toml --target x86_64-unknown-linux-gnu --offline
	cargo check --manifest-path userland/libos32term/Cargo.toml --lib --target x86_64-unknown-linux-gnu --offline

.PHONY: check-term-model
