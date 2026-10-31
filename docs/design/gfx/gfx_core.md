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

## D1 で移した設計 (MEMMAP_V3・VER_INFO_DESIGN・API_CONTRACTS・GFX_MODES_DESIGN)

**probe の前に BB の量を決める**
確保済みの BB をそのまま使う
物理連続のまま持つ
スプラッシュには同じ面を lease し、終了で lease だけ返る。

BB は起動時 (§4-5 の識別 + 予約 + BB 確保の段、live AS = 0、D33 — probe より前) に池から連続確保 (§2-2)。

| BB (planar 128KB / PEGC 300KB) | gshell が CPL=0・恒等で `rep movs` | 起動時 (live AS = 0) に確保し gshell が永続 owner で保持 (R4・R5) |

- gfx_ver_state は gfx_core.c 内で g_backend / gfx_started にアクセス。query 成功時に backend 種別を返す。
- active=1 は現在寸法（gfx_current_height）、active=0 は選択済み GUI の目標寸法。既存 gfx_screen_info の契約は変えない。
- gfx_kind は内部 enum UNKNOWN=0/PC98=1/PEGC=2/CIRRUS=3。backend の文字列や weak シンボルの非搭載を UI に漏らさない。

> [要確認] gfx_kind は現行 include/gfx_hal.h で GFX_VER_GEFORCE=4 も定義する。

`screen_info() -> GFX_ScreenInfo`

`gfx_hw_fill_rect` /
`gfx_hw_blit`。CPU バックエンドでは `OS32_ERR_NOSYS` (-10) を返し、

```c
/* gfx_core.c のみ: probe/prepare の順序は既存選択関数に残す。 */
static int gfx_select_mode(GfxBackend *be, int legacy200);
```

- GfxBackend は既に非 const の実体。g_backend の const ポインタは維持し、選択関数へ実体を渡して selected_mode だけ更新する。

- query の幅・形式・bpp は表と形式記述から、高さだけは gfx_current_height から埋める。動的 flags・リース規則は維持する。

> [要確認] query の高さは後続 B1 の gfx_started ? gfx_current_height : gfx_backend_mode(be)->height が現行 gfx/gfx_core.c の式。未開始時まで現在高さを使う記述とは異なる。

- 宣言以外の clip・転送 pitch・clear 範囲と query の幅は gfx_backend_mode() から得る。init 時に mode->height を現在高さへ反映し、query の高さは必ず gfx_current_height を読む。
- gfx_current_height が現在高さの唯一の正典。全ドライバの init は選択行の height をここへ書く。shutdown は400へ戻し selected_mode も default に戻す。
- selected_mode は設定用の行であり shutdown 後の現在高さではない。gfx_vram/gfx_scroll と CirrusModuleContext の高さポインタも維持する。

| 条件 | 選択規則（現状維持） |
|---|---|
| GFX=pc98 | probe を呼ばず標準 16 色、gfx_init は 400、gfx_init_200 は 200 |
| GFX=pegc / cirrus | 指定だけ probe。失敗・未リンクは直接 pc98、もう一方へ試し直さない |
| auto / 不正 pref | Cirrus → PEGC → pc98 の最初の成功 |
| PEGC 成功 | 640×480 PACKED8、gfx_init_200 でも同じ。起動時 24/31kHz による 400 選択は無い |
| Cirrus 成功 | 640×480 PACKED8、gfx_init_200 でも同じ |
| init 後 probe 失敗 | 既存の cleanup を経て pc98。要求が legacy200 なら 200、それ以外 400 |
| prepare | 選択だけ先行、PEGC は同期を変えない。Cirrus の既存 init→shutdown の時機も維持 |

- モード選択は実体の probe 成功後・init/prepare の前。boot reserve は候補表を参照せず、今の容量定数と型板を使う。

- 登録後の graphics 境界だけで Cirrus の表を読む。G6b はその選択点で既存予約容量を照合し、未登録表への参照を一切行わない。
- gfx_mode_pick は通常 default、legacy200 は planar の既存 200 行だけ選ぶ。面積最大・色数最大の探索はしない。
- 未知形式/空表/容量不足はハード設定前に不成立とし、pc98 へ戻す。pc98 自体の資源不足は描画未開始の既存経路へ。

- **(確定の B1 で差し替え)** query の高さは `gfx_started ? gfx_current_height : gfx_backend_mode(be)->height`。pc98 は 200→shutdown→query=400、packed (PEGC・Cirrus) は GUI を抜けた後も init の前も 480 (今と同じ)。他の format/flags は従来の backend 問合せのまま。

## 4. 選択モードと容量を分けて大きさを決める

- フレームバッファの表示幅は mode、高さは gfx_current_height、row/pitch は形式＋mode。物理先頭・窓・キャッシュ属性は現行ドライバ/台帳を使う。
- packed 使用量 = pitch×height、planar 論理使用量 = pitch×height×4。全乗算は u32、容量との比較前に超過検査。

- PC98 CLIENT の収納間隔は既存 32,000 bytes、4 面合計 128,000 bytes、予約は 32 頁=128 KiB。
- 200 モードでも plane_offset={0,32000,64000,96000} と予約 32 頁を維持。200 の使用量 64,000 bytes と混同しない。
- PC98 DISPLAY は非連続 4 記録・各 32 KiB のまま。表示端以降の VRAM padding は clear しない。

- PEGC CLIENT は候補識別時に 75 頁=300 KiB を予約、DISPLAY は既存リニア窓内の 300 KiB。

- **起動時の予約と面の型板は変更しない**。gfx_core.c:313–359,442–445 は memmap 等の現行容量定数を使い続ける。
- 登録済みの mode を選ぶ時だけ、計算した pitch×height×planes と既存 CLIENT/DISPLAY の容量を照合する。PC98 は固定 plane_offset の最終端でも照合する。
- init のたびに追加確保・移動しない。型板を表から生成する変更、boot の候補表最大値計算は範囲外。
- gfx_kernel_framebuffer は選択後に mode の幅と現在高さを使い、未開始時は boot PC98 の既存寸法を維持する。
- SURFACE の PC98 200 時の収納高さ 400 は維持し、公開 framebuffer の高さ 200 と区別する（gfx_core.c:192–199）。
- lease は既存の台帳 npages と plane_offset をそのまま使用。exec/lease.c と exec/surface_query.h は変更不要。
- 再 init の既存 revoke→bind→完了処理と面世代を維持する。モードポインタを更新しただけで古いリースを再利用しない。

確定の B1 の高さの式 (`gfx_started ? gfx_current_height : gfx_backend_mode(be)->height`) は gfx_core.c の static な `gfx_started` を使う

98 の 16 色のバックエンドは init の関数を持たない (gfx_core.c が init=NULL を見て GDC を初期化する)。
