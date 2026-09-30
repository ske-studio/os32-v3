#!/bin/sh
# sync_docs_to_win.sh — OS32 のドキュメントを Windows 側 (C:\WATCOM\docs\os32) にミラーする
#
# 正はこのリポジトリの docs/ (+ README.md, CLAUDE.md)。WSL 側のファイルは Windows から
# 読みにくいので、逆方向の sync_hwdocs.sh と対にして Markdown 等をそのまま写す。
# docs/hw/ (ハードウェア資料の取り込みミラー) は元が Windows 側にあるので写さない。
# 出力先は sync_hwdocs.sh が取り込み時に除外している (往復ループ防止)。
set -eu
SRC="$(cd "$(dirname "$0")/.." && pwd)"
DST="${OS32_DOCS_WIN:-/mnt/c/WATCOM/docs/os32}"
[ -d "$(dirname "$DST")" ] || { echo "ERROR: $(dirname "$DST") が無い (Windows 側の資料ディレクトリ)"; exit 1; }
mkdir -p "$DST"
rsync -a --delete --exclude='hw/' --exclude='__pycache__/' "$SRC/docs/" "$DST/docs/"
cp "$SRC/README.md" "$SRC/CLAUDE.md" "$DST/"
cat > "$DST/README_MIRROR.md" <<EOT
# os32 — OS32 リポジトリのドキュメントミラー (読み取り専用)

正は WSL の \`$SRC\` (git 管理)。\`tools/sync_docs_to_win.sh\` (\`make docs-win\`) で
写しているので、**ここを直接編集しない** (次の同期で消える)。

- \`docs/\` — リポジトリの docs/ そのまま (\`docs/INDEX.md\` が目次)。docs/hw は含まない
- \`README.md\` \`CLAUDE.md\` — リポジトリ最上位の 2 つ
- 最終同期: $(date '+%Y-%m-%d %H:%M')
EOT
echo "synced -> $DST: $(find "$DST" -type f | wc -l) files, $(du -sh "$DST" | cut -f1)"
