# drivers/video/geforce/backend_geforce.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `drivers/video/geforce/backend_geforce.c`

GUI とグラフィック内コンソールは CPU 描画→HAL 転送。98 のハードウェア文字合成は GF に無い。通常のテキスト CUI は 98 側に残す。
`GfxBackend` の probe/prepare/init/query/present_rect/set_palette/enter/leave/shutdown/modes を埋める。fill_rect/blit は NULL、TEXT_OVERLAY・vsync・page-flip 能力は無し、CPU 転送の PACKED8 だけ。
パレットは既存固定色/lease 方針を通った OS32 の 256 色表を使い、診断 C の 8 色表を本番へ持ち込まない（`O/gfx/gfx.h:94–98`）。モード一覧は 1 行、`gfx_init_200` の packed=480 選択を保つ。
prepare は識別/資源確認だけ。init で `video_start`→初回転送→98 セル確認→route01 確認まで行い、初回 enter は無操作。void init 後の probe=0 で既存失敗出口へ渡す（`O/gfx/gfx_core.c:932–945`）。
順番は **route00→ID/定数長/base/資源/写像確認→Command=0x0102 設定・生の 16bit 読戻し確認→ROM 採取・解読→A2 の legacy 無効化/解錠→main0〜4→基準値採取→mode 計算→共有列→必須 CHECK→初回転送/セル→route01**。A1 は本番の全経路から外す。
route01 とセルは上層の切替後に判定し、共有列の終了条件に route01 を混ぜない。510/TEST sense/scratch、50C bit28・CR1A bit3/5・680A24 全bit の照合は復活させず、AR/DAC の副作用読取りも増やさない（`O/docs/POLICY_DEBUG.md:1585–1590`）。

GUI=1拒否・GUI≠1許可（省略時0）、GUI 起動・CUI 描画アプリ起動の各場合で、起動 1 回につき最初の gfx_init で 1 回だけ devinit、dirty/pitch/palette、CLIENT は PEGC/GF 一方だけ、pc98 fallback、decode不明時98無書込み、shutdown→再init、部品欠落/KDEBUG=0。NP21/W は GF 無し拒否と98/PEGC/Cirrus退行
