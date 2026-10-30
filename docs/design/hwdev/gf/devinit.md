# hwdev/gf/devinit.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/devinit.c`

不一致・未知 ID・範囲不成立では GF を使わず pc98 を維持する。BAR1 の窓長と実 VRAM 容量は別で、既存 PFB 容量確認・表示面範囲も維持する。変換は `P2V_IO` と既存写像 API（[C5]、`O/gfx/gfx_core.c:305–327`）。
順番は **route00→ID/定数長/base/資源/写像確認→Command=0x0102 設定・生の 16bit 読戻し確認→ROM 採取・解読→A2 の legacy 無効化/解錠→main0〜4→基準値採取→mode 計算→共有列→必須 CHECK→初回転送/セル→route01**。A1 は本番の全経路から外す。
**N1**：資源確認後・A2 前に `pci_cfg_write16` で Command=0x0102 を一度書き、生の 16bit 読みで一致を確認する。冷起動時の 0x0106 をここで正し、不一致は既存失敗出口へ進む。BAR には書かず、Status の W1C を含む DWORD RMW はしない（`D/GF6200_4A_DESIGN.md:55,87`、`O/kdebug/gfinit_plan_c.c:230,272`）。
PROM は BAR0 から既存の二重読み/header/checksum/既知 script 検査で採取する。x86 は解析せず、全段 hash 固定も追加しない。未知 ROM/命令・処理失敗は再試行せず S0 相当へ進む。
本番全体のソフト上限は既存 `GF_INIT_EXEC_US` の 60 秒を採用（`O/kdebug/gfinit_exec.h:4`）。期限超過は既存失敗出口へ進むが、戻らない MMIO に壁時計上限は保証できない。
共有 parser は一つ。診断用出力/接続・lifetime/stages・owner/lease は元の場所に残す。
KDEBUG=0 fake 起動、初回 Command 書込みは 1 回・BAR 書込みは 0、0x0106→0x0102 の 16bit 確認と不一致時の停止、A2/main順・期限・各失敗出口、選択中 gfinit 拒否、再 devinit 無し
初期化中 ROM は最大既存 GF_ROM_MAX、runner の有限作業域を使い、limits/基準値を値コピーした後に ROM と作業域を解放する。本番再 modeset は ROM に依存しない。
