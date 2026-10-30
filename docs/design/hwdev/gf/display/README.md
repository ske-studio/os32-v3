# hwdev/gf/display/ — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/`

2D/3D 加速、TV/DVI、head B 表示、任意 timing、高解像度・true color、EDID 自動モード選択、ページフリップ、VGA BIOS の x86 実行・解析は作らない。
カーソルは初版では GF ハードカーソルを非表示にするだけで、GUI のソフトウェア描画を使う。FIFO は表示用の設定を扱い、描画エンジンのコマンド FIFO は作らない。

VBE の対応は機能の比較であり INT 10h 互換 ABI を提供する意味ではない。VBE に独立した機能が無い内部操作は「02 の内部」または「無し」と明記する。
生成列の並びをこれらのファイルに分散複製しない。
VBE 4F0A（保護モード入口、p.56）は実装しない。blank、FIFO、カーソル、PC-98 の出力リレーを VBE Core のサービス数に数えて水増ししない。
値計算の再実装はしない。modeset の順番・待ち・条件は `gf_load_state` だけが所有する。
mode_bit と zero 初期化は共有、集約関数は呼出しだけ。palette:112–123 と模様 row:124–141 は診断に残す。
## 索引


### 個別設計書

- [access](access.md)
- [baseline](baseline.md)
- [blank](blank.md)
- [calc](calc.md)
- [checks](checks.md)
- [cursor](cursor.md)
- [dac_format](dac_format.md)
- [extended](extended.md)
- [fifo](fifo.md)
- [format](format.md)
- [load_row](load_row.md)
- [modes](modes.md)
- [modeset](modeset.md)
- [output](output.md)
- [palette](palette.md)
- [pitch](pitch.md)
- [pll_program](pll_program.md)
- [quiesce](quiesce.md)
- [start](start.md)
- [state](state.md)
- [timing](timing.md)
