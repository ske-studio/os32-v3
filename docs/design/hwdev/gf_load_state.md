# hwdev/gf_load_state.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf_load_state.c`

**決定**：本番 modeset は nouveau の load_state に近い簡素な作りとし、試験 C と `gf_load_state`・生成列を同一ソースで使う。
初版は独立 DPMS・非零の表示先頭・HW cursor を提供せず、DPMS は modeset 内の OFF/ON だけ（U4 決定）。
| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| DPMS `dpms` | SR01→10ms→CR17→CR1A、modeset の OFF/ON は必須 | `dispnv04/crtc.c:175–234`、`dac.c:460` | 拡張 4F10、p.63 |
B は `int gf_load_row(struct gf_display_context *, const struct gfinit_mode *, u32 ordinal)` を `gf_load_io.row` へ接続し、section/wait も既存型を使う（`O/hwdev/gf_load_state.h:36–41`）。
分割/複製無し。唯一の生成元 `tools/gfinit_mode_tables.py:29` を維持。blank/DPMS の modeset 順序もここに保持。

## 移し替えた設計

`N/=R/nouveau/drivers/gpu/drm/nouveau/`、`V/=N/dispnv04/`、`E/=R/envytools/`、`X/=R/xf86-video-nv/src/`。各 `file:line` はこの接頭辞で解決する。
[HW3]：主手本は nouveau c03c1a1 の MIT 部（`V/crtc.c:1–24`, `V/hw.c:1–23`）、補助は envytools f102b82・xf86-video-nv 8234bce（`R/INDEX.md:6–9`）。メーカーの NV44A modeset 公開仕様は未取得。GPL 本体のコード・構造、ROM x86 コードは参照しない。

## 2. nouveau の modeset の経路
取得版 `nouveau_hw_load_state` は protect→**ramdac→ext→palette→vga**→unprotect（`V/hw.c:827–835`）。以下は取得版を正とする。prepareはQ→P（encoder→CRTC、`R/nouveau/drivers/gpu/drm/drm_crtc_helper.c:371,376`、PM追加の同版・寛容許諾の順序参照）。

## 3. OS32 の列
番地はBAR0相対hex、MMIOは32bit、CR/SR/GR/ARは8bit。`s(x)` はmain4後・Q前に採る値、`r(x)` はその操作直前値。CR=6013D4/D5、SR=C03C4/C5、GR=C03CE/CF、AR=6013C0/C1・FF reset=6013DA（`V/nvreg.h:129–139,177–187,298`）。

差分：analog専用なのでL1のTV timing/CTV・FP timing/dither/debug/marginの再ロード、L2のPVIDEO offset/limitは省く（`V/hw.c:495–516,519–523,533–535,684–689`）。TV_SETUP=0・FP_TG=OFF・PVIDEO_STOP=1の停止値を先に成立させ、停止中の未使用経路を設定しないため。

- Q の FP_ON 条件は (r(680848)&10000011)!=0、真の枝だけ OFF 書込み→50ms を置き、P/C の SR01 の後に各10msを置く（nouveau dfp.c:93–110、crtc.c:175–234）。

初期化・A4・VPLL診断・virtual 等が使う計画実行器は維持する。新Cのための汎用実行器、予算解析、S0予約枠、全段hash固定は作らない。

共有列はQ→P→S/D→PLL→拡張CR→palette/VGA→commit。値・順・FP条件・50/10ms待ちの正典を1本にし、C builderの列走査（`gfinit_plan_c.c:314`）をこの列の消費側にする。
試験CはP後のVRAM模様、前後CHECK、98セル、route01、RECをbuilderで付加する。paletteはmode入力、8色は既存mode側に残す。共有列は試験模様・REC・route・98セルを要求しない。
手本は `N/dispnv04/crtc.c:708` prepare、`:731` commit、`N/dispnv04/hw.c:827` load_state。新レジスタ列を推測で追加しない。
