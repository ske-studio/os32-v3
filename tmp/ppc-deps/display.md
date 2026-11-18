# PowerPC KAPI 表示の分離案

調査基点は detached HEAD `bd2d6eb7f`。以下の行番号はこの木。計画 §3 の本線作業は **5a-i → 5a-ii → 5b → 5c の4段**。5c の照会先行は PPC 側だけで切り出せる。指定の `lease-bundle/DESIGN.final.md` は参照先に存在せず、既存の契約と `docs/design/exec/lease.md:39–63` を代用した。静的調査のみ。preflight、ビルド、比較、NP21/W、QEMU は依頼どおり未実施。

## gfx 22 本 — 5a-i、その場で境界を作る

- `drivers/video/pc98/gfx_vram.c:87–167` の `gfx_add_dirty_rect` は共通候補。クリップは既存の `gfx_clip_screen`（`gfx/gfx_internal.h:8–19`）。追加、包含、近接マージ、満杯時の圧縮を一つに保ち、内部の挿入関数はクリップ後の矩形と画面寸法を受ける形にする。`_merge_prev_dirty`（同 VRAM ファイル :220–227）は列を合成する部分を共通候補にする。前フレーム列を合成するかどうかは flip 経路の判断。32px 丸めと近接閾値を独立に扱い、この段は PC-98 の現行値を固定する。
- 後端に残すのは `_flush_dirty_queue` の4プレーン転送・scroll wrap・バイト集計（:172–215）、`_flip_page` のページ切替と I/O 集計（:232–239）、`gfx_diag_px` の PEGC 観測（:25–43）、`pc98_splash_raster_table` への呼出し（:348）。VSYNC とページ操作の実体は既に `platform/pc98/gfx_init.c:41–48`。矩形の消費を切り出す際は転送側に矩形列を読み取り専用で渡し、消去は呼出し元へ寄せる。
- 共通へ出す予定の `gfx_v1_present_rect`（:15–22）、`_present_dirty_packed`（:61–82）、`_present_dirty` の方針（:241–264）、`gfx_present_dirty` / `gfx_present_nosync` / `gfx_present` / `gfx_present_rect` / `gfx_present_raster`（:270–351）はこの段では現地で整理する。raster の入力検査と packed の通常提示への委譲を共通候補にし、プレーン転送後の再生は機種側。planar の `pc98_present_rect` が dirty 追加から同期提示へ再入する現行経路を維持する（`drivers/video/pc98/backend_pc98.c:99–103`）。bridge の CLIENT 読/DISPLAY 書の順、診断の呼出し回数、packed の1提示1 commit、planar raster の空列時 commit も比較対象。
- 5a-ii で動かす dirty TU をこの段の末尾で `drivers/video/pc98/` 内に独立させ、`build/features/pc98-devices.mk:3` で選ぶ。x86 リンク順は `build/sources/order.mk:16` で確定しておく。関数分割と呼出し変更があるため、x86 関数差0を条件にできない。
- NP21/W は 9801 planar 200/400 行、flip 有無、PEGC packed を確認。負座標、右下端、空矩形、包含・近接・列満杯、スクロール周回、交互ページへ残る旧画素を検査。planar 非 flip の sync だけ VSYNC 待ち、flip の両方は切替、packed は従来の HAL 転送。raster は line 0 と後続色、IF=0・同期失敗時の静止提示、転送量・commit・診断を観測。PPC はこの段で結線せず、後続 QEMU の比較基準は現行の起動・FB コンソール。

## gfx — 5a-ii、dirty TU の純移動

- 5a-i で独立させた `gfx_add_dirty_rect` と矩形挿入・列合成の TU を `gfx/` に移す。仮名は `gfx/gfx_dirty.c`。確定は `docs/SRC_MOVE.md` §3。既存の列の実体（`gfx/gfx_core.c:42–43`）をそのまま使う。VRAM 転送、flip、VSYNC、raster 再生は移動対象外。
- `build/features/pc98-devices.mk:3` の抽出 TU を `build/sources/common.mk:5` の `C_COMMON_EXEC` へ移し、`FEATURE_PC98_DEVICES` によるこの TU の選択を外す。PC-98 装置全体の制約（`build/select.mk:58–66`）は維持。両像は同 :266–279 の既存選択で拾い、x86 の order は 5a-i の位置を維持する。PC98_DEVICES=0/EXEC=1 も含め、単独 TU に機種シンボルの未定義参照がないことを確認する。
- x86 関数差0の対象。比較元は 5a-i 完了点。`tools/build_compare.py:240–273` は .o のパスで対にするので、新旧のパスを対応させて `objdump -drw` を比較する。関数差0と像のバイト一致を別々に判定する。一致なら追加 NP21/W は不要（`docs/tasks/agents/ROLES.md:112`）。PPC は共通コンパイル・リンクの確認が先で、実画面の受入は 5b。

## gfx — 5b、提示と残りの窓口

**提示の口。** 共通 TU（仮名 `gfx/gfx_present.c`）に上記の5つの公開 present 関数、`gfx_v1_present_rect`、`_present_dirty_packed`、`_present_dirty` の共通制御を置く。矩形転送は既存 `GfxBackend.present_rect(int x, int y, int w, int h)`（`include/gfx_hal.h:78`）を使う。固定名の小さい機種口は既存 `include/display_provider.h` に追加する案。宣言は次の3つで足りる。

```c
void display_provider_planar_flush(const GFX_Rect *rects, int count);
int display_provider_present_raster(GFX_RasterPalTable *table);
void display_provider_present_diag(void);
```

- flush は画面内・planar 転送単位に整列済みの矩形を同期消費し、参照を保持しない。呼出し元が列を管理し、空列は無動作。既存の void KAPI を維持。待ち・flip は既存 `display_wait_vsync` / `display_flip_pages` を使う。raster は有効な件数の借用表を転送済みの像に適用し、成功0、入力不正 INVAL、未対応 NOSYS、同期等のエラーは既存再生器に従う。公開 void 入口では静止像を保つ。diag は既存 `gfx_diag_first_present` に加えて従来の同期/全画面/rect 位置だけから呼ぶ。
- 実体は PC-98 の `gfx_vram.c` に flush・raster・diag の既存本体を残す。PC98_DEVICES=0 の EXEC 像が選択可能なら、装置なしの同名実体を機種側で排他的に選ぶ。PowerPC の `platform/powermac/display_provider.c` は flush/diag を無動作、raster を NOSYS とし、通常の dirty 提示は既存 OF の packed 転送へ到達させる。無対応 raster でも有効な表なら通常 dirty 提示。`gfx_add_dirty_rect` と全画面提示に選択モードの実寸を使い、PC-98 は従来値・丸め、OF は dirty の端を画面内に保つ。OF の MMIO 書込単位は既存後端に任せる。
- `platform/powermac/display_provider.c:49–104` の INDEX8 RAM shadow と MMIO ledger を再利用。`drivers/video/of_fb/backend_of_fb.c:39–72,82–94` は pitch 付き INDEX8 の同形式転送、ネイティブの32-bit store、保護行回避。色形式変換を足す必要はない。端のワードには隣接 shadow 画素が入る。QEMU では端ワード、ピッチ余白、保護行、画面右下を検査し、論理矩形外に別の未提示変更を置いたときの扱いを確認する。OF の `present` は現状カウンタを加算しないため、実に転送したワードのバイト数と、空転送を除く commit の集計を 5b で結線する。dirty 一括の commit は1回。`_present_dirty_packed` の補正で空転送が加算されないことも確認する。
- 残り16本の共通実体は、初期化2・終了・画面 owner（`gfx/gfx_core.c:345–362`）、scroll（`gfx/gfx_scroll.c:12`）、palette 設定・画面情報・HW fill/blit・stats・palette lease（`gfx/gfx_core_api.inc:28–147`）、palette 取得（`gfx/palette.c:71` の `palette_get`）、framebuffer（`gfx/gfx_core_framebuffer.inc:49` の `gfx_get_framebuffer`）、HW submit/get（`gfx/gfx_hw_submit.c:71,98`）、OSRI（`kapi/kapi_osri.c:9`）。6本の提示/dirty と合わせ22本。既存 `gfx_mode_pick` は planar 200 行が無ければ既定モードを返す（`include/gfx_hal.h:148–157`）。PPC では OF 実寸を報告し、200行対応を広告しない案。HW fill/blit の未提供は NOSYS、submit/get は個別コマンドと経路の実能力を検査する。palette と scroll の void 呼出しは現行後端の動作を確認してから公開。OF palette コールバックは無動作で、lease 範囲も空。framebuffer は RAM CLIENT のみを確認し、DISPLAY の MMIO を貸さない。
- `C_COMMON_EXEC` に新提示 TU と `gfx_hw_submit.c` / `gfx_hw_mono_cpu.c` を入れ、後2本を `C_COMMON_PC98_PENDING` から除く。OSRI は VFS 上で動くが、`gfx/gfx_osri_view.c:10–15,24–49` は PCM・serialfs・表示の排他条件と planar 定寸を前提とする。PPC の OF には `osri_show` がない。最小案は `kapi/kapi_osri.c` / `gfx/gfx_osri.c` / `gfx/gfx_osri_view.c` の3本を `C_COMMON_EXEC` へ移し、PCM/serialfs を共通から直接要求する箇所だけを既存 display provider に `int display_provider_osri_busy(void);` として抽出する。PC-98 は現在の3条件を評価して busy なら1、なければ0、任意機能無しの像ではその条件を0にする。PPC は同ファイルに0の後端を置き、OF 非対応は共通の `osri_show` 検査で NOSYS とする。PC-98 の BUSY 優先順は維持する。実表示の OSRI 移植は別判断。この案では新しい FEATURE は不要。`build/select.mk:19,266–279` の EXEC 選択で両像に通し、order の既存項目を保って提示 TU を追加する。
- NP21/W は 5a-i と同じ提示経路に加え、通常表示から GUI/CUI 復帰、OSRI 再生・abort・BUSY と復元、HW 未対応/対応各ボードでの戻り値を確認。QEMU は有効 OF/FB 未成立の両方で起動、既知 INDEX8 の全画面・rect・dirty/sync/nosync/raster、使用寸法と pitch、owner、終了後の CUI、未対応能力を確認。可視の画素列を観測し、純 RAM の書換えだけで合格にしない。x86 関数差0を条件にできない。選択変更だけの小分けは既存リンク順と実体が同じなら差0を狙える。

## gfx2 29 本 — 5c、照会を先行し資源回収から表示まで

**先行公開できる9本。** `gfx2_adapter_count`、`gfx2_adapter_info`、`gfx2_adapter_gen`、`gfx2_head_info`、`gfx2_connector_info`、`gfx2_connector_edid`、`gfx2_mode_count`、`gfx2_mode_get`、`gfx2_caps`。共通実体は `gfx/v2/enum.c:5–91`、`gfx/v2/backend_reg.c:26–32`、`gfx/v2/caps.c:36–51`。照会は面を確保せず、`gfx2_owner_release` に依存しない。EDID は未対応の照会として公開可。現行共通処理は0ブロックなら shim の NOSYS より前に INVAL を返し、出力を変えない（`enum.c:54–66`、`drivers/video/of_fb/of_fb_v2.c:215–222`）。実能力の先行は8本で、9本目の EDID に成功データはない。 `gfx2_check_op` も資源を確保しないが、現行の shape 判定は GART を CPU 可視とみなし OF caps の fmt/pool を照合しない（`gfx/v2/check_op.c:4–9,38–48,56–78`）。先行組には入れず、非対応 pool/format と実 submit の整合を取ってから公開する。

- 先行の選択変更は `sdk/kapi_target_ppc.py:182–212,227–243` と生成の入力だけ。 `C_COMMON_EXEC` に gfx/v2 と gfx/sync の強実体が既にあり、OF shim は `build/platform/powermac.mk:7`、boot の結線は `platform/powermac/ppc_kernel.c:370–406` と `gfx/gfx_core.c:61–68`。 `sdk/gen_kapi.py:520,666` で不在表と include を確認し、既存 slot 番号は維持する。kboot-sh の FB 経路成立時に adapter 1、head/connector 0、実寸 INDEX8 1モード、ID 1、palette 無し、実際の sync caps を確認する。FB 未成立時は count 0、無効 ID/出力サイズ/範囲、世代、EDID 出力未更新を確認。connector 種別は現在、共通 shim が OF に VGA を返す（`enum.c:107–120`）。
- 本線は `exec/exec_gfx.h:4–15` の owner 回収と emergency の条件を分ける。共通の既存口 `int gfx2_owner_release(struct addrspace *as);`（`gfx/v2/gfx2_exec.h:6`）を EXEC の実装として `exec/exec_finish.inc:35` から使う。有効な終了 AS を取り、map/lock/scan refs/面を回収、0か poison の IO を返す（`gfx/v2/owner_release.c:29–67`）。 `build/sources/hooks.mk:6–10` の gfx 不在単位から owner stub を除き、既存 gfx hook は emergency に限定。 `gfx/gfx_emergency.c` と `exec_gfx_return_emergency(u32 owner, int reason)` は従来の PC98_PENDING 選択で保持できる。PPC で v1 所有を公開する際は `exec/exec_finish.inc:216–223` の owner 喪失と表示復帰を別途検証する。surface の4本は 29本の外だが同段の前提。 `exec/surface_query.c`、`exec/system_surface.c`、`exec/lease.c` は EXEC 選択済み。 `HOOK_surface` だけ EXEC に合わせ、共通 lease を使う。 `build/select.mk:264–279` の hook と選択の組立ては再利用し、`FEATURE_PC98_PENDING` を PPC 全体で有効化しない。
- 残り20本は `gfx2_check_op`、面 `gfx2_surface_create/free/desc`・`gfx2_screen_surface`、`gfx2_lock/unlock`、`gfx2_submit`・`gfx2_fence_wait/query`、表示 `gfx2_mode_set`・`gfx2_flip`・`gfx2_vblank_wait`・`gfx2_palette`・`gfx2_gamma`・`gfx2_cursor_image/move/show`・`gfx2_dpms`・`gfx2_present`。すべて `gfx/v2/` の既存窓口を使う。PC-98/各ボードの ops は既存のまま。PPC は `platform/powermac/display_provider.c:14–35` の bind/publish と `drivers/video/of_fb/of_fb_v2.c:59–93,131–167,224–229` の INDEX8 SYSMEM コピー・CPU 描画経路・即時退役を使う。対応外の VRAM/GART、EDID、palette、vblank、gamma、cursor、DPMS は共通入力検査と shim のエラーを受入で区別する。
- 表示公開前に2点を揃える。① OF の `GfxMode.hw_mode` は現状0（`backend_of_fb.c:30–31`）、enum は0を1にするが restore は生の値を mode_set へ渡す（`gfx/v2/enum.c:131–134`、`gfx/v2/display.c:174–195`）。OF の登録値を1にそろえ、restore 時の conn mask と DISPLAY alias が `surface_copy` の SYSMEM 条件を満たすことも確認する。 `surface_copy` の adapter NULL 検査は `a->v1` 参照より先に置く。② `gfx2_present`（`gfx/v2/display.c:142–171`）は sync が DIRECT/無しなら0、SHADOW でも `gfxsync_write_rows` まで。現在 OF sync target は RAM（`display_provider.c:25–35`）。既存の起動時コンソールは `gfx_console_present` から flip を直接使うため（`gfx/gfx_console.c:88–98`）、起動時の実画面表示だけではこの slot を検証できない。必要なら既存 `GfxBackendV2` 契約に任意の `int (*present_rect)(void *ctx, u32 head, const struct gfx2_surf *s, int x, int y, int w, int h);` を追加し、共通がクリップ・所有権検査済みの矩形を渡す。OF shim は借用面と実寸/pitchを検証し `of_fb_copy` へ、0/INVAL/STALE を返す。共通は成功時に対象 dirty を処理し、失敗時は保持する。既存 ops の版・部品との互換を確認し、OF にだけ効く内部の任意フックで足りるなら公開 HAL 構造の変更を避ける。汎用 sync の書込先を MMIO に替える方式は `display_provider.c:28–30` の転送条件と矛盾する。
- NP21/W では元の gfx2/lease の世代、旧 SID 拒否、map/lock/scanout を保持した終了と復帰を確認。QEMU では列挙後に面作成→lock/unlock→CPU fill/copy submit→fence→読取 lock/unlock→表示→free、異寸法・世代違い・所有者違いと強制終了後の再確保、mode_set/flip/present の実画素、console 復帰と保護行を確認。surface 側は query→lease→unlease、束の失敗巻戻し、世代 STALE、MMIO 貸出拒否。OF 異寸法比較の変異と、owner 回収を外す変異をゲストで RED にする。先行の PPC 公開表と PPC のみの backend 差は x86 関数差0を狙える。共通の照会・present・復帰の実装を変える小段は差0を条件にしない。hook 分離だけは x86 で同じ呼出しに展開される限り差0を狙う。
- 公開の都度 `tools/ppc/test_kapi_binding.py:24–30,221–227` の残存 slot 固定値・group-empty 変異と未定義許可を更新する。公開済み slot の実体への結線と、残存 slot の型別応答を別に確認する。

## 本線またはユーザーの判断が要る点（4件）

1. 5a-ii/5b の TU 名と移動表。既存 order 上の位置と x86 の比較時のパス対応も同時に確定する。
2. 5b の v1 表示契約。200行指定の既定モードへのフォールバック、無可変 palette の void API、OF の端ワードを隣接 shadow から反映する範囲を受入仕様にするか。
3. 5b の OSRI。OF では NOSYS として先に共通選択するか、packed 再生の設計が決まるまで当該 slot を保留するか。
4. 5c の OF 可視提示。既存 HAL ops の互換を保てる内部フックの置き場と、部分提示で残る dirty をどう保持するかを確定する。
