# hwdev/gf/devinit.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/devinit.c`

不一致・未知 ID・範囲不成立では GF を使わず pc98 を維持する。BAR1 の窓長と実 VRAM 容量は別で、既存 PFB 容量確認・表示面範囲も維持する。変換は `P2V_IO` と既存写像 API（[C5]、`O/gfx/gfx_core.c:305–327`）。
順番は **route00→ID/定数長/base/資源/写像確認→Command=0x0106 設定・生の 16bit 読戻し確認→ROM 採取・解読→A2 の legacy 無効化/解錠→main0〜4→基準値採取→mode 計算→共有列→必須 CHECK→層1 start→初回転送/セル→route01**。A1 は本番の全経路から外す。
**N1**：資源確認後・A2 前に `gf_master_enable` で Command=0x0106 を 16bit 書込み・読戻しし、Bus Master を有効にする。不一致は失敗出口へ進む。latency timer が 16 未満なら cache line を保持して 64 に変更し読戻す。BAR には書かず、Status の W1C を含む DWORD RMW はしない。層1 start の先頭でも毎回同じ enable を行い、停止確認後は Command=0x0102 にする（`docs/decisions/realhw/GF_FEAT1_DESIGN.md` §3）。
起動前の prepare は資源・写像確認後、PCI Command の BusMaster だけを 16bit RMW で解除し（0106h→0102h）、変更時に 1 行記録する。nouveau `nouveau_drm.c:881` は probe で有効化するが、OS32 は prepare と start を分け、N1 まで STOP transport の前提を保つ（PM 決定 2026-11-04）。foundation の時計・待ちは、foundation から production への adapter を介して display.io と同じ production の時計・待ちへ結ぶ。
PROM は BAR0 から既存の二重読み/header/checksum/既知 script 検査で採取する。x86 は解析せず、全段 hash 固定も追加しない。未知 ROM/命令・処理失敗は再試行せず S0 相当へ進む。
本番全体のソフト上限は既存 `GF_INIT_EXEC_US` の 60 秒を採用（`O/kdebug/gfinit_exec.h:4`）。期限超過は既存失敗出口へ進むが、戻らない MMIO に壁時計上限は保証できない。
共有 parser は一つ。診断用出力/接続・lifetime/stages・owner/lease は元の場所に残す。
KDEBUG=0 fake 起動、BAR 書込みは 0、初期 Command 0x0102 / 0x0106 から RUN 0x0106、停止後 0x0102、再開始後 0x0106 の 16bit 確認と読戻し不一致時の停止、latency の境界、A2/main順・期限・各失敗出口、選択中 gfinit 拒否、再 devinit 無し。
初期化中 ROM は最大既存 GF_ROM_MAX、runner の有限作業域を使い、limits/基準値を値コピーした後に ROM と作業域を解放する。本番再 modeset は ROM に依存しない。
