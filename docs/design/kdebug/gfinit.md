# kdebug/gfinit.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gfinit.c`

**診断 owner/lease は `kdebug/gfinit.c:70–74` のまま**（F7）。核から `int gf_production_selected(void)` 一つを export し、GFX=geforce の起動では gfinit の入口で拒否する。本番の成否や GUI 終了後にも拒否を保ち、診断と本番は設定変更＋再起動で入れ替える。S0/緊急復帰入口は止めない。
共有 parser は一つ。診断用出力/接続・lifetime/stages・owner/lease は元の場所に残す。
KDEBUG=0 fake 起動、初回 Command 書込みは 1 回・BAR 書込みは 0、0x0106→0x0102 の 16bit 確認と不一致時の停止、A2/main順・期限・各失敗出口、選択中 gfinit 拒否、再 devinit 無し

## 索引

- [hwdev/gf/](../hwdev/gf/README.md)
