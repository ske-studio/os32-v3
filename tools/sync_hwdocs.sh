#!/bin/sh
# sync_hwdocs.sh — PC-98 ハードウェア資料のテキスト部分を docs/hw/ にミラーする
#
# 正は C:\WATCOM\docs (/mnt/c/WATCOM/docs)。/mnt/c は grep が遅く、リポジトリ内で
# 動くツール (emu_agent 等) からも見えないので、Markdown だけを手元へ写す。
# 資料は著作権物なので docs/hw/ は .gitignore で除外し、**絶対に git に入れない**。
# 画像・PDF・OCR の中間生成物 (600MB 超) は写さない。
set -eu
SRC="${HWDOCS_SRC:-/mnt/c/WATCOM/docs}"
DST="$(cd "$(dirname "$0")/.." && pwd)/docs/hw"
[ -d "$SRC" ] || { echo "ERROR: $SRC が無い (Windows 側の資料ディレクトリ)"; exit 1; }
mkdir -p "$DST"
# 除外は --include='*/' より前に置く (後ろだと全ディレクトリが先に採用されて効かない)。
# os32/ は tools/sync_docs_to_win.sh の出力先 (往復ループ防止)。
rsync -a --delete --delete-excluded --exclude='PDF/' --exclude='os32/' --exclude='__pycache__/' \
      --include='*/' --include='*.md' --exclude='*' \
      "$SRC/" "$DST/"
cat > "$DST/README.md" <<'EOT'
# docs/hw — PC-98 ハードウェア資料のローカルミラー (git 管理外)

正は `C:\WATCOM\docs` (`/mnt/c/WATCOM/docs`)。`tools/sync_hwdocs.sh` で Markdown だけを
写している。**著作権物なので git には含めない** (`.gitignore` の `/docs/hw/`)。

- `PC9800Bible/` — PC-9801 Bible (東京理科大) の章立て
- `undocumented/` — UNDOCUMENTED 9801/9821 Vol.2 の I/O ポート資料 (`io_*.md`)
- `reference_sources/` — 参考ソース (NP21/W, FreeDOS 等) のメモ
- 両者が矛盾したら UNDOCUMENTED を優先する (docs/INDEX.md)
EOT
echo "synced: $(find "$DST" -name '*.md' | wc -l) files, $(du -sh "$DST" | cut -f1)"
