# gfx/gfx_core.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `gfx/gfx_core.c`

RAM CLIENT 300 KiB と BAR1+[0,4B000h) の DISPLAY を SURFACE 台帳へ別登録し、`bb_base/bb_size/bb_pitch` と framebuffer API は CLIENT を返す。**PEGC と GF の CLIENT は一方だけ予約**し、600 KiB の二重確保を避ける（F10）。
モード一覧は 1 行、`gfx_init_200` の packed=480 選択を保つ。
**`GFX=geforce` は `GUI≠1` のとき許す**（F5・N4）。起動予約前に既存の設定読取りで判定し、GUI=1 なら拒否して pc98 を選ぶ。GUI 省略時は既定値 0（`O/init/kernel.c:521`）。最初の `gfx_init`（GUI または CUI の描画アプリ）で、承認待ち無しに devinit を一度行う（N5、`O/gfx/gfx_core.c:960,1014`）。
これで再起動時は GF devinit に入らず 98 CUI へ戻れる。起動時上書きキー等の新しい脱出機構や auto 優先候補化は足さない（`O/init/kernel.c:343–366,577–582`）。
prepare は識別/資源確認だけ。init で `video_start`→初回転送→98 セル確認→route01 確認まで行い、初回 enter は無操作。void init 後の probe=0 で既存失敗出口へ渡す（`O/gfx/gfx_core.c:932–945`）。
**GF 初期化失敗時は pc98 へ落とす**（同:937）。ただし legacy decode 解放未確認では planar 初期化/98 VRAM 書込みを行わず、gfx_started=0、既存 serial/再起動経路へ留める。安全条件を init/init_200 と故障出口に通す。
表示中の転送/palette/再 enter 失敗も stop→route00→既存 GUI 終了境界→面の再結線へ進む。使用中の GUI の下で寸法の違う BB へ即時に差し替えず、KAPI の void 型は維持する。

GUI=1拒否・GUI≠1許可（省略時0）、GUI 起動・CUI 描画アプリ起動の各場合で、起動 1 回につき最初の gfx_init で 1 回だけ devinit、dirty/pitch/palette、CLIENT は PEGC/GF 一方だけ、pc98 fallback、decode不明時98無書込み、shutdown→再init、部品欠落/KDEBUG=0。NP21/W は GF 無し拒否と98/PEGC/Cirrus退行

## 索引

- [./](../../../README.md)
