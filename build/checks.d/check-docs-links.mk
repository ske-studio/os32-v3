CHECK_PAR_ORDER += 017:check-docs-links
# 文書のリンク切れ検査 (lychee の薄い包み)。相対パスの実在と見出しアンカーの
# 実在を見る。900 リンクで 0.03 秒なので `check` の列に入れてある。
# lychee (cargo install lychee) が無い環境では SKIP して終了コード 0。
check-docs-links:
	@python3 tools/check_docs_links.py

.PHONY: check-docs-links
