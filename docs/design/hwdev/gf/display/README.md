# hwdev/gf/display/ — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/`

2D/3D 加速、TV/DVI、head B 表示、任意 timing、高解像度・true color、EDID 自動モード選択、ページフリップ、VGA BIOS の x86 実行・解析は作らない。
カーソルは初版では GF ハードカーソルを非表示にするだけで、GUI のソフトウェア描画を使う。FIFO は表示用の設定を扱い、描画エンジンのコマンド FIFO は作らない。

VBE の対応は機能の比較であり INT 10h 互換 ABI を提供する意味ではない。VBE に独立した機能が無い内部操作は「02 の内部」または「無し」と明記する。
下表の最終ファイルは `hwdev/gf/display/<語幹>.c`（上位サービスと PC-98 接続は例外を明記）。生成列の並びをこれらのファイルに分散複製しない。
VBE 4F0A（保護モード入口、p.56）は実装しない。blank、FIFO、カーソル、PC-98 の出力リレーを VBE Core のサービス数に数えて水増ししない。
B の timing/format/pitch/fifo/extended/output/cursor/dac_format 等は既存 calc の機械的分割で各ファイルにする（F9）。値計算の再実装はしない。modeset の順番・待ち・条件は `gf_load_state` だけが所有する。
最初は対応する文区間を元位置の `.inc` として一度ずつ残す。独立 `.c` 化時に各機能を一つにまとめ、mode_bit と zero 初期化は共有、集約関数は呼出しだけ。palette:112–123 と模様 row:124–141 は診断に残す。
## 索引

- [hwdev/gf/](../README.md)
