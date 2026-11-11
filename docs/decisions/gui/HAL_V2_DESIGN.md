> 出所: `~/os32-tmp/run/hal-v2/DESIGN.final.md` (Claude 起草、codex astra のレビュー 4 回 (第 4 回はユーザー承認) で確定、2026-11-09)。段の状態は票 TASK_HAL_V2 が持つ。
> 状態: **現行 (2026-11-09、2026-11-10 に Fable 5.2 のレビューを反映、2026-11-11 に main 761ca15f の実装に合わせ、実装の第 1 波の設計の欠けを埋め、G5 の GART を NV44A の nouveau の経路 (nv04 の頁表付きの PCI の DMA の物) に決めた)** — 表示 HAL v2 (形式 u32×2・32bpp、DX9c 程度の能力、2D の口、fence、面、GPU から見える RAM、adapter/head の列挙、CPU 直描きの同期バックエンド層) の確定設計。

# 表示 HAL v2 の詳細設計 (確定)

前提: シングルタスク・協調切替、9801 互換は見ない、カーネルは整数だけ。ユーザー決定は DECISIONS.md (11-08・11-09) が正。3D の口とオーバーレイは範囲外 (能力の欄だけ置く)。今の版 (GFX_ACCEL_HAL_DESIGN の H1〜H6・G1〜G3) は壊さず上に積む。

## r3→r4 の変更 (REVIEW.r3 の指摘ごと、現物は 4f971604)
- P1-1 fence の順: `gfx2_fence_issue` を廃し、`gfx2_fence_reserve` (番号だけ) → `be->emit` (描画) → `gfx2_fence_commit` (印 + note、失敗は喪失) の 3 段に。命令ごとに 1 番号 (§6-1・§11-2・§11-3)。
- P1-2 sync の ctx: adapter の `sync[2]` を廃し、C1 の `gfx2_sync_publish(kind, head, ctx)`・`gfx2_sync_get(a, head, &ctx, &mode)` と Y4 の `gfxsync_mode()` に。DIRECT/SHADOW の判定はこの 1 口だけ (§10・§11-2・§11-5)。
- P1-3 gart_range: `struct gf3_ctx` の末尾に範囲照会の callback と opaque を A0 が足し、設定 G1・実体 G5・利用 G8 (§11-6)。
- P1-4 別名: C4 の `gfx2_surf_alias`・`gfx2_surf_alias_update` (backing を借りるだけ、解放しない、更新で gen+1) を A0 に。C14 はこれだけを呼ぶ (§6-5・§11-2)。
- P1-5 unmap 失敗: C7 の `gfx2_map_detach`・`gfx2_vaspace_drop` を足し、unmap 失敗の面は backing を返さない (KEEP)。destroy・quarantine の失敗でも AS への参照は 0 (§7-3・§11-2)。
- P1-6 .text の条件: module の表は `GfxBackend.v2_mod` の module の .text で見る。組込みの表は abi と hook の非 NULL だけを見る (§11-7)。GF の v2 表は geforce_module.c が kernel 側へ写す (§11-7)。
- P2-1 試験と I: §13 の全試験に I の列。I1 は新帯の往復と AS の回収の合格が必須 (§12・§13)。
- P2-2 weak の検査: A0 が I ごとの対象関数の一覧 `tools/checks/gfx2_wiring.txt` を作り、`gfx2_no_weak.sh` が最終 ELF で型 T と a0_weak の外の番地を見る (§11)。
- P3 (指摘外の整合): GART の不足の戻り値を NOSPC に統一 (§8・§14、`OS32_ERR_NOMEM` は os32_kapi_shared.h に無い)。
- D1〜D9 現物の食い違い (GFX_ACCEL_HAL の着地): §1 の表の末尾。
- #3 確定 (ユーザー決定「AとC」): §9-1 に A を書き、C は別票 CONSOLE_DUAL へ。§14 から未決を外し将来直す所を記録。

## 1. 実物の確認

| 物 | file:line | 中身 / v2 への意味 |
|---|---|---|
| GfxBackend | include/gfx_hal.h:67-144 | 色は u8、`fill_rect`・`blit` は表示面の中だけ。表示の口 (118-125) は `u8 head` だけで `set_mode(u32 id)` に head が無い |
| H1 の INDEX8 の口 | gfx_hal.h:53-65 (`GfxDirectOps`)・129-143 | §3-7 で INDEX8 専用。v2 は意味を変えない |
| KAPI の H1 | gfx/gfx_hw_submit.c:70・95 (実装済み、D1)、gfx_hal.h:211-212、kapi.json `"version": 83` | |
| 走査面 | gfx_hal.h:35-37 `GfxScanout` | format は GFX_FMT_* |
| 形式の番号 | os32_kapi_shared.h:319-320 (PLANAR4=0・PACKED8=1)、include/gfx_format.h:11-12 (内部 RGB565=2・XRGB8888=3) | v2 はこれを延ばす |
| 画面情報 | os32_kapi_shared.h:326-335 `GFX_ScreenInfo` (`reserved[5]`) | 能力は別の構造体 |
| GeForce 層 3 | hwdev/gf/accel.h:13-16 (`gf3_fmt`)・85-89 (`gf3_surface`、予約の id・世代)・127-140 (`gf3_surface_check` は `gf_vram_match` で生きた予約を要する)・162-191 (全部同期、staging の再利用ごとに待つ) | |
| GF の ROP | hwdev/gf/accel_rop.c:51 (alu < 16 の GX だけ)・61-63 (32bpp は COPY 以外を拒否)・78 (パターンは全 1 に固定) | ROP3 全種と任意パターンは無い |
| GF の fence | command.h:101・211-212、command.c:98-99 (再初期化で seq=0・epoch+1)、fence.c:49 (古い epoch は EFAULT) | |
| GF の予約 | hwdev/gf/vram_view.h:99-108 (`gf_vram_reserve`・`release`、id は再利用しない) | |
| GF の color key | accel_objects.c:48・112 (NULL に結ぶ) | |
| GF の RAM の DMA | command.h:64・66・138、geforce_dma.c | 1 頁の staging だけ |
| GeForce 層 4 | hwdev/gf/display/api.h:19・87-97 | 列挙・表示の元 |
| GF の登録 | backend_geforce.c:259・263-264 (mode の表を書く)・749-767 (表。module の組みでは `geforce_backend_ops`:749、組込みでは `gfx_backend_geforce`:751) | |
| Trident | trident.h:351-352・581、write_gap.c:4-21、regs.c:117-138、present.c:26-56、trident_module.c:198-199、backend_trident.c:96-98・144 | 同期層へ移す元 |
| mm | mm/dma_pool.h (64KB 固定)、pgalloc.h:227-235・191-197・116、pgalloc_owner.inc:216-231 (`ledger_transfer`)・233- (`ledger_reclaim_owner`)、pgalloc_resource.inc:37-38 (`ledger_device_map_prepare` は DEVICE owner だけ)、paging_map.inc:169-171 (supervisor の恒等 UC の写像) | 写像はユーザー向けでない |
| ユーザーの写像 | mm/paging.h:286-317 (`map_user`・`map_user_range_phys`・`free_user_range` は頁を pgalloc へ返す)、paging.h:206 `lease_mapping`、exec/lease.c:40-60 (ledger_surfaces に縛られる)、memmap.h:465-468 (lease の帯 F0000000〜FE000000・8 本) | |
| 写像の帯 | mm/paging_addrspace.inc:202-215 (`map_user_range_phys` は app_pde の外を拒む)、paging.h:216 (`lease_pt_phys[MEM_LEASE_MAX_PDES]`)、paging_lease.inc:5-11 (root の文脈)・135- (`paging_lease_unmap` は帯の中なら ledger に縛られない)、paging_addrspace.inc:115-120 (破棄で lease の PT を返す)、exec/lease.c:69 (確保の上限は `MEM_LEASE_END`) | 新しい帯は lease の PT を共用する |
| AS の後片付け | exec/exec_finish.inc:33-34 (`lease_revoke_all`)・36 (poison なら私物を返さない)・55 (`paging_addrspace_destroy`)・62-64 (reclaim・retire)・69 (`kfree(a->as)`) | |
| GF の同期 | hwdev/gf/accel_fill.c:44-46・accel_copy.c:61-62 (中で fence を待つ) | emit だけの口を足す |
| g_backend | include/gfx_hal.h:181 は `GfxBackend *` (H4 が hook を書き換える)、実体は gfx/gfx_core.c:58 で初期値は `DISPLAY_FALLBACK_BACKEND` | `GfxBackendV2` は const、v1 は毎回 `a->v1->` から読む (D2) |
| backend の選択 (B4 の provider) | include/display_provider.h、platform/pc98/gfx_selector.c:53-108 `display_provider_select` (候補の表 :12-18、probe の判定 `gfx_probe_backend` :46-51)・:29-36 `display_provider_ledger` (backend → `LEDGER_SF_*`)、platform/powermac/display_provider.c (未対応の provider)。gfx/gfx_core.c:156-159 の `gfx_select_backend` は provider を呼ぶだけ | v2 は `&gfx_backend_*` を直に比べず、kind は `display_provider_ledger()` から取る。probe が通った物の列挙は provider に足す `display_provider_each` (§11-7) |
| 台帳の種別 | mm/pgalloc.h:297-301 `LEDGER_SF_PC98 1・PEGC 2・CIRRUS 3・GEFORCE 4・TRIDENT 5` | `GFX2_KIND_*` は同じ値 (§11-2) |
| gfx_init の流れ | gfx/gfx_core_init.inc:4 `palette_shadow_reset`・:8 `gfx_select_and_init_backend`・:9-16 早い戻り (init の失敗は :9、packed 系は :10-16)。gfx_core.c:203-238 の選択と init (init の失敗で pc98 へ落ちるのは :227-231)、`gfx_init`:295・`gfx_init_200`:300、`gfx_prepare_backend`:252-291 | 登録の時点は §11-7 |
| gfx_shutdown | gfx/gfx_core.c:356-384 (GF → pc98 の切り替え :375-382、`g_backend = DISPLAY_FALLBACK_BACKEND` の代入 :380、`gfx_reinit_surfaces(1)` :383)、`gfx_screen_owner` :351-354 | §11-7 |
| パレット | gfx/palette.c:34 `palette_shadow_reset` (16 色・4bit の影、既定は :14-31)、gfx/gfx_core_api.inc:50-59 `gfx_set_palette_hal`・:102-130 `gfx_lease_palette` (v1 の set_palette の経路、KAPI の成分は 0〜15、backend が ×17、backend_pegc.c:20-22・backend_geforce.c:717) | v2 は中核の `gfx2_pal[256]` を正にする (§9-2) |
| TLB | arch/x86/arch_cpu.h:46-52 (`invlpg` は使わず CR3 の読み直し)、mm/paging_lease.inc:126 (`paging_current_cr3() == as->pd_phys` なら `paging_load_cr3`) | §7-4 |
| build の一覧 | build/select.mk:243 が `C_KERNEL` を `C_COMMON` (build/sources/common.mk) と `C_ORDER_LEGACY` (build/sources/order.mk) から組む。geforce の module は build/geforce-module.mk | §11-7 |
| module | tools/module_schema.py、build/out/trident/trident_imports.h:19 (`TRIDENT_MODULE_ABI_GEN`)、geforce_module.c:299 | |

食い違い 2 件 (r1 から): (2) SURVEY §3-1 の GF の透過色は実物では未使用。(3) SURVEY §6 #8 に対し GF の中には `pci_dma` と 1 頁の staging がある。(1) H2 以降の未着地は解消 (D1)。

r4 で直した食い違い (現物 4f971604。上の表の該当の行もこの値で読む):
| ID | r3 の前提 | 現物 | 直した所 |
|---|---|---|---|
| D1 | gfx_hw_submit.c:1-14 は NOSYS | gfx/gfx_hw_submit.c は実装済み (111 行、`submit_one`:20・`gfx_hw_submit`:70・`gfx_hw_get`:95)。H1〜H6・G1〜G3 は main に入った | §11 の着地の前提は満たした。C14 の呼ぶ行は `gfx_hw_submit`:70・`gfx_hw_get`:95 の入口 |
| D2 | gfx_hal.h:181 は `const GfxBackend *` | `extern GfxBackend *g_backend` (gfx_hal.h:181、実体 gfx_core.c:58、初期値 `DISPLAY_FALLBACK_BACKEND`)。`gfx_policy_reeval` が `g_backend->fill_rect`・`blit` を書き換える (gfx_policy.c:47-48) | `struct gfx2_adapter.v1` は `GfxBackend *`。v1 の hook は呼ぶたびに `a->v1->` から読み、関数ポインタを写して持たない (§11-2) |
| D3 | `gfx_scanout_gen_bump()` | 無い。走査の変化は `gfx_policy_scan_changed()` (gfx/gfx_policy.c:67-76、宣言 gfx_policy.h:14-16「Module-safe scan notification」。中身は `gfx_scan_changed = 1;` → `gfx_policy_reeval()`) | §9-3 |
| D4 | gfx_screen_owner は gfx_core.c:457 | gfx_core.c:351-354 | §6-5 |
| D5 | accel_copy.c:60-61 | accel_copy.c:61-62 (accel_fill.c:44・46 は同じ) | §1・§11-6 |
| D6 | GfxBackend の末尾に足すだけ | 末尾は `shadow_fill_rect`・`shadow_blit` (gfx_hal.h:142-143)。tools/hwdev_layout.json:661 に `struct GfxBackend`、:1033 に `struct gf3_ctx` の配置 | A0 は 143 の後に足し、hwdev_layout.json の 2 表も直す (§11-6・§11-7) |
| D7 | GF・Trident の表は module の物を指す | module の init が kernel 側の `candidate` を埋め、`geforce_start` (geforce_module.c:193-241) が module の init で kernel 側の `candidate` を埋めて hook ごとに `module_text_contains` を見る (G3 で `direct_ok` の text の検査 :217 と、`direct_ops` の表が module の ro の中にあるかの検査 :221-227・各 hook の検査 :228-238、表示の hook の検査 :240-253 が加わった)。公開 (`gfx_backend_geforce = candidate`) は load の成功の後の :334 で別に行う (既定の表は :144)。trident_module.c:22・31 も同形 | v2 の表も kernel 側へ写し、`v2_mod` を付ける (§11-7) |
| D8 | 「module の .text の中」の検査を組込みにも使える | kernel の .text の境界の記号が無い (build/os32.ld:36-41 は `*(.text)` だけ、`_etext` を使うのは host 試験だけ) | 組込みの表は .text を検査しない (abi と hook の非 NULL だけ)。境界の記号は足さない (§11-7) |
| D9 | v1 の GF の HW の 2D は v2 の番号を出す | v1 の GF の描画は同期 (`gf3_fill` は accel_fill.c:46 で待つ)。v1 の入口から出る GPU の仕事は戻る時に終わっている | `v1_bridge_end` の fence は今は常に 0、C14 は `gfx2_fence_note` を呼ばない (§6-5) |

## 2. 層の図

```
アプリ (素のアプリ / Win32・DirectDraw・D3D9 互換層 [L1] はユーザーランド)
   │  libos32gfx v2: ARGB → 生値、lock の決まり、MANAGED、gfx2_check_op で分岐
═══ KAPI (gfx2_*、末尾追加、v84) ════════════════════════════════════════════
HAL v2 中核 (gfx/v2/): 形式・能力・check_op・面の表と状態・heap・GART・fence・submit・列挙・表示
   │   ├ CPU の 2D (gfx/v2/sw_*.c)        └ v1 の橋 (gfx/v2/v1_bridge.c): v1 の全入口の同期
   ├─ GPU の backend (GfxBackendV2): GeForce・ATI (将来、同じ表)
   └─ CPU 直描きの同期バックエンド層 (gfx/sync/、/etc/gfx/sync.cfg)
          ├ PEGC (SHADOW 固定)   ├ Trident   └ 付録のカード
今の版 (GfxBackend の v1 の口・H1/H2 の INDEX8・gfx_hw_submit) は意味を変えずに並ぶ
```

## 3. 形式と色

### 3-1. 色
```c
typedef struct { u32 v; u32 x; } GFX2_Color;   /* 8B。v = 値、x = 透過色などの余地 */
```
| 使う所 | v | x |
|---|---|---|
| 前景 (`fg`)・線・塗り | 書き先の形式の生値 (INDEX8 は index) | ALPHA の op だけ: 下位 8 bit = 定数 α、他の op は 0 (0 以外は INVAL) |
| 背景 (`bg`、mono・mono パターンの 0) | 同上 | bit0 `GFX2_CX_TRANSPARENT` で透過、他は 0 |
| src の鍵 (`skey`)・dst の鍵 (`dkey`) | 範囲の下端 (src / dst の形式の生値) | 範囲の上端。`x == v` で 1 色。`x != v` は `gfx2_check_op` が範囲を許すときだけ |
- 生値にする理由: カーネルと backend は色を変換しない。ARGB → 生値は libos32gfx の `gfx2_color_pack`。

### 3-2. 形式 (r1 と同じ)
| 番号 | GFX2_FMT_ | bpp | 必須 | 備考 |
|---|---|---|---|---|
| 0 | PLANAR4 | 4 | v1 だけ | v2 の面には使わない |
| 1 | INDEX8 | 8 | ○ | D3DFMT_P8 |
| 2・3 | RGB565・XRGB8888 | 16・32 | ○ | 走査面 (GF・ATI・Trident) |
| 4 | ARGB8888 | 32 | ○ (面) | カーソル・α の元 |
| 5・6 | XRGB1555・ARGB1555 | 16 | 能力 | |
| 7・8 | A8・L8 | 8 | 能力 | |
| 9・10 | YUY2・UYVY | 16 | 能力 | 拡大縮小の元だけ |
| 0x40〜0x7F | 3D の段 | — | 欄だけ | 3D の別設計 |
- 24bpp の packed は持たない。RGB → INDEX8 の変換はカーネルでは NOSUPPORT (libos32gfx が最近色で)。

### 3-3. 参照の式 (sw・host の正解・HW の比べの基準。gfx/v2/format.c と sw_*.c に 1 か所ずつ)
| 物 | 式 (整数) | HW に許す差 |
|---|---|---|
| 拡げ・縮め | 5→8 `x<<3\|x>>2`、6→8 `x<<2\|x>>4`、縮めは上位を採る。α の無い形式から読むと α=255、X の欄は 255 を書く | 0 |
| INDEX8 → RGB | 中核のパレットの写し `gfx2_pal[256]` (§9-2) | 0 |
| YUV → RGB (BT.601、16〜235) | C=Y−16、D=U−128、E=V−128、R=clamp((298C+409E+128)>>8)、G=clamp((298C−100D−208E+128)>>8)、B=clamp((298C+516D+128)>>8)。YUY2 の 2 画素は U・V を共有 | 各成分 ±2 |
| 最近点 | `sx = sr.x + ((2·i+1)·sr.w) / (2·dr.w)` (i は書き先の列、行も同じ) | 0 |
| 双線形 | 中心合わせ `fx = ((2·i+1)·sr.w·65536)/(2·dr.w) − 32768` (負は 0)、`w = (fx>>8)&255`、成分ごとに `(a·(256−w)+b·w+128)>>8` を横 → 縦、端は複製 | 各成分 ±1 |
| α (premultiplied) | `div255(t) = (t + 128 + ((t+128)>>8)) >> 8`。定数 α: `s' = div255(s·ca)` を成分と α に。合成: `d = s' + div255(d·(255−s'_a))` | 各成分 ±1 |
| クリップ | 元の座標は**クリップ前**の dst→src の対応で決め、クリップは書く画素を減らすだけ | 0 |
| 重なり | COPY は重なってよい (方向は中核が選ぶ)。STRETCH・ALPHA・形式の変換で src と dst が同じ面の重なる矩形は INVAL | — |
| 線 | Bresenham、主軸は `\|dx\|>=\|dy\|` で x、誤差の初期値 `2·dmin−dmax`、`e > 0` で副軸へ、**終点は描かない** (GDI の LineTo) | 0 |
- 差を許す操作は caps の `exact` の bit を 0 にする。試験は exact な操作は画素一致、他は画素ごとの最大差で見る (§13)。

## 4. 能力

### 4-1. GFX2_Caps (256B、u32 だけ、`gfx2_caps(adapter, out, size)`、`size` の前方だけ埋める)
| offset | 欄 |
|---|---|
| 0 | `size, adapter, backend_kind, tier3d_now, tier3d_hw, overlay_now, overlay_hw, overlay_fmt` (tier3d_now・overlay_now は今は 0、hw は装置の世代 = GF T3・RV280 T2・RV515 T3・overlay_hw は RV が 1) |
| 32 | `caps2d_hw, caps2d_sw, exact` (op ごとに 1 bit: `GFX2_CAP2D(op) = 1u << op` (§11-1)、op は §5-2 の番号 1〜10、bit0 と 11 以上は 0。hw/sw は「どれかの形式・pool で使える」の要約だけで、形式・pool・flags ごとの可否は `gfx2_check_op`) |
| 44 | `fmt_surface, fmt_render2d, fmt_scanout, fmt_src_stretch` |
| 60 | `max_w, max_h, max_pitch, pitch_align, offset_align, scanout_align, stretch_min_q16, stretch_max_q16, max_cmds` |
| 96 | `vram_bytes, vram_cpu_bytes, gart_bytes, gart_page, fence_hw, async` |
| 120 | `heads, connectors, palette_entries, gamma_bits, cursor_w, cursor_h, cursor_fmt, flip_vblank, vblank_irq, thermal` |
| 160 | `cpu_direct, sync_mode` + `char sync_profile[16]` |
| 184 | 3D の段 (今は全部 0): `vs_version, ps_version, max_tex_w, max_tex_h, max_tex_aspect, max_tex_stages, max_streams, max_prim, max_index, max_rt, tex_caps, raster_caps, zcmp_caps, blend_caps` |
| 240 | `reserved[4]` (0) |

装置ごとの埋め方 (数値は目安。実際の値は backend の `caps` が返し、試験は「返した値で動くこと」を見る。v2 の表の無い backend は C16 の shim の `caps` が C10 の `gfx2_caps_from_v1` で v1 の `GfxMode` から埋める):
| 欄 | GeForce 6200 (G1) | PEGC (P1) | Trident (shim) | PC-98 内蔵 (shim) |
|---|---|---|---|---|
| caps2d_hw | FILL・COPY・STRETCH・MONO・HOSTDATA・READBACK (FILL・COPY の ROP3 とパターンは G7 で INDEX8・565 だけ、32bpp は単色の塗りと SRCCOPY だけ。色鍵の COPY と ALPHA は HW に無い (§11-6 の「色鍵・α」) ので 0、ROP4・LINE は 0) | 0 | 0 (v2 では GE を使わない、§6-4) | 0 |
| caps2d_sw | op 1〜10 の全部 | 同 | 同 | 同 |
| exact | STRETCH・ALPHA 以外の全部 (sw・hw とも) | 同 | 同 | 同 |
| fmt_surface | 1〜4 (5〜10 は G* で確かめた物から足す) | 1〜4 (SYSMEM だけ) | 1〜4 (SYSMEM だけ) | 1〜4 (SYSMEM だけ) |
| fmt_scanout | INDEX8・565・X888 | INDEX8 | INDEX8・565・X888 (backend_trident.c:96-98) | 0 (PLANAR4 は v1 だけ) |
| max_w/max_h・pitch_align | 4096/4096・64 | 640/480・4 | 1024/768・4 | 640/400・4 |
| fence_hw / async | 1 / 1 (gf2_fence) | 0 / 0 | 0 / 0 | 0 / 0 |
| vram_bytes・gart_bytes | 装置の値・`GFX2_GART_KB`×1024 (§8) | 0・0 | 0・0 | 0・0 |
| heads・cursor | 2・64×64 ARGB | 1・0 (SW) | 1・0 | 1・0 |
| tier3d_hw | 3 | 0 | 0 | 0 |
| cpu_direct・sync_mode・sync_profile (backend は埋めない。C10 の `gfx2_caps` が照会のたびに head 0 の `gfx2_sync_get` から埋める: 0 なら 1・`gfxsync_mode`・`gfxsync_profile`、NOTFOUND なら 0・0・"") | 0・0・"" | 1・SHADOW・"pegc" | 1・sync.cfg の mode・"pci:1023:*" の当たった鍵 | 0・0・"" |

### 4-2. 操作ごとの可否
```c
#define GFX2_HOW_NOSUPPORT 0
#define GFX2_HOW_SW        1
#define GFX2_HOW_HW        2
int gfx2_check_op(u32 adapter, const GFX2_Op *op_shape, u32 src_fmt, u32 dst_fmt,
                  u32 src_pool, u32 dst_pool, u32 *how);  /* op・rop・flags と形式と pool で判定 */
```
- 中核の判定の順: (1) 形式の組が規則で禁止 (RGB→INDEX8、PLANAR4、3D の形式) → NOSUPPORT、(2) adapter が LOST・LOST_HELD (§6-3) → backend を呼ばず (5) の SW の判定へ、(3) 書き先が SHADOW の面 (§6-4) → SW、(4) backend の `check` が HW を返す → HW、(5) それ以外は、関わる面が全部 CPU から見える (`kva` が非 NULL、または GART の面で呼んだ AS に写像がある、§8 の「CPU から」) なら SW、見えない面があれば NOSUPPORT。submit は同じ判定で命令ごとに振り分ける (判定と実行が食い違わない)。判定の本体は C8 の内部の `int gfx2_check_exec(struct gfx2_adapter *a, const struct gfx2_exec *ex, u32 *how)` (gfx2_int.h、C8 が A0 の追補として足す) で、C9 の走査 1 が命令ごとに実の面で呼び `ex->how` に入れる。公開の `gfx2_check_op` は形から仮の `gfx2_exec` (面の表に載せない stack の `gfx2_surf` 2 つに fmt・pool・w・h を入れ、`ex->pat` は flags の PAT があれば kind 1、PAT と PAT_COLOR の両方があれば kind 2 の仮のパターン、無ければ NULL) を作って同じ関数を呼ぶ。仮の面は別名でない (3) は通らない) ので、(5) の「CPU から見える」は pool で読む: SYSMEM・GART は見える、VRAM は `a->caps.vram_cpu_bytes != 0` の時だけ見える。sid・data・pitch・矩形の位置は見ない (大きさ dw・dh・sw・sh は見る)。

## 5. 2D の口

### 5-1. GFX2_Op (80B、offset は A0 で固定)
```c
typedef struct {
    u16 op, rop;            /*  0: rop は ROP3 (下位 8 bit) か ROP4 (前景 8 + 背景 8) */
    u32 flags;              /*  4: SRCKEY・DSTKEY・CLIP・FILTER・ALPHA_PIXEL・PAT */
    u32 dst, src;           /*  8: 面の sid (src=0 = 無し) */
    i16 dx, dy; u16 dw, dh; /* 16 */
    i16 sx, sy; u16 sw, sh; /* 24 */
    GFX2_Color fg, bg;      /* 32 */
    GFX2_Color skey, dkey;  /* 48 */
    u32 data, pitch;        /* 64: CPU 側の元/先 (ユーザーの VA) */
    i16 cx, cy; u16 cw, ch; /* 72: クリップ (flags の CLIP) */
} GFX2_Op;                  /* 80 */
```

### 5-2. op ごとの欄 (使わない欄は 0、0 でなければ INVAL)
| op | 番号 | 使う欄 | 禁止の組み合わせ |
|---|---|---|---|
| FILL | 1 | dst・d*・fg・rop (ROP3、P は fg か束のパターン)・flags の PAT | src・鍵 |
| COPY | 2 | dst・src・d*・sx/sy (sw・sh は dw・dh と同じ)・rop・skey/dkey・fg (PAT の無い時の P の単色) | ROP が SRCCOPY 以外と鍵の同時 |
| STRETCH | 3 | COPY + sw・sh・flags の FILTER (形式の変換もここ) | rop は SRCCOPY だけ、重なり |
| MONO | 4 | dst・d*・fg・bg・data・pitch (1bpp、MSB が左) | src |
| HOSTDATA | 5 | dst・d*・data・pitch (dst の形式の画素)・skey (CPU の元の鍵) | src |
| READBACK | 6 | src・s*・data・pitch | dst |
| ROP4 | 7 | COPY + rop (16 bit) + data・pitch (1bpp の mask、1=前景の ROP) + 束のパターン | 鍵・STRETCH |
| ALPHA | 8 | COPY + sw・sh + fg.x (定数 α) + flags の ALPHA_PIXEL | ROP・鍵 |
| LINE | 9 | dst・(dx,dy)→(sx,sy) の端点・fg・rop | src、dw/dh/sw/sh |
| SET_PATTERN | 10 | data・fg・bg・flags の PAT_COLOR。mono: 8B (1 行 1B、MSB が左、1=fg・0=bg、bg.x の透過可)。色: 64 個の u32 (行の順) の生値で、使う命令の書き先の形式の下位の bit を採る (INDEX8 は 8・565 は 16・8888 は 32) | dst・src・矩形。束の中の以後の命令だけに効き、束をまたがない |
- src と dst は同じ adapter の面 (違えば INVAL)。
- パターンの原点は書き先の面の (0,0) (画素 (x,y) は パターンの `(x & 7, y & 7)`)。flags の PAT を持つ命令がその束で SET_PATTERN より前にあれば走査 1 で INVAL。
- パターンの状態は §11-3 の `struct gfx2_pattern` 1 つ (submit.c の写しの中)。check・sw・backend の emit には `struct gfx2_exec.pat` で同じ物を渡す。

### 5-3. 束の口と寿命
```c
int gfx2_submit(u32 adapter, const GFX2_Op *ops, u32 n, u32 gen, u32 *done, u32 *fence);
/* gfx/v2/submit.c の順。k_ops は submit.c の static (GFX2_MAX_CMDS × 80B = 5,120B)。
 * シングルタスク・協調切替なので入れ子は無い (旗は持たない) */
if (user && (!writable(done, 4) || !writable(fence, 4))) return OS32_ERR_INVAL;   /* 何もしない */
a = gfx2_adapter_get(adapter); if (!a) return OS32_ERR_INVAL;
if (!(a->flags & GFX2_AF_ACTIVE)) return OS32_ERR_NOSYS;   /* §9-1: ACTIVE でない adapter には描かせない */
if (gen != a->gen) return OS32_ERR_STALE;
if (n > GFX2_MAX_CMDS || (user && !readable(ops, n * sizeof(GFX2_Op)))) return OS32_ERR_INVAL;
copy_from(k_ops, ops, n * sizeof(GFX2_Op));                 /* 以後 ops は読まない */
u32 k_done = 0, k_fence = a->fence.issued; int rc = 0;     /* n == 0 は 0・done=0・fence=発行済みの最新 */
static struct gfx2_exec ex[GFX2_MAX_CMDS];
for (i = 0; i < n; i++) { rc = check_one(a, &k_ops[i], &ex[i]); if (rc) goto out; }  /* 走査 1: 何も実行しない */
u32 k_hw = 0;
for (i = 0; i < n; i++) { rc = run_one(a, &ex[i]); if (rc) break; k_done = i + 1; k_hw += ex[i].how == GFX2_HOW_HW; }  /* 走査 2 */
if (k_hw) { a->be->kick(a->ctx); }                          /* a->be は常に非 NULL (§11-7、v1 だけの backend は shim) */
k_fence = a->fence.issued;
out:
*done = k_done; *fence = k_fence;      /* 最後に書く。READBACK の先と重なっていてもこの値が残る */
return rc;   /* 走査 2 の rc は装置の失敗だけ (§6-3)。k_ops[k_done] は途中まで行ったかもしれず、再送しない */
```
- 検査 (走査 1): 面の sid の世代・持ち主 (呼んだ者の ledger owner、§7-1)・状態 (§7-1)、欄の表 (§5-2)、矩形とクリップのあふれ、data の範囲 (`(h−1)·pitch + 行のバイト` を u32 のあふれ込みで)、`gfx2_check_op` が NOSUPPORT でないこと (NOSUPPORT の命令があれば束は `OS32_ERR_NOSYS` で、何も実行しない。INVAL は欄・範囲の誤りだけに使う)。
- data の中身 (MONO・HOSTDATA・mask・パターン) は走査 2 で命令の順に読む。同じ束の前の READBACK がそこへ書いていれば、書いた後の値を読む (順に行う意味どおり)。範囲は走査 1 で確かめ済み。
- **CPU の入出力は戻る前に終わる**: MONO・HOSTDATA・SET_PATTERN・ROP4 の mask は走査 2 でカーネルが読み切る (GF は push buffer への inline か staging に写し、staging の再利用ごとの待ちは層 3 の今の契約のまま)。READBACK は完了を待ってからユーザーの先に写して戻る。したがってアプリは戻った後に data を自由に使ってよい。
- 非同期で残るのは FILL・COPY・STRETCH・ALPHA・LINE (面と面) だけ。`*fence` を待てば全部終わっている。

## 6. fence と CPU のアクセス

### 6-1. 番号
- adapter ごとの u32 の通し番号。**0 は「待つ物が無い」**で発行しない (折り返しで 0 を飛ばす)。`done(f) = f == 0 || (i32)(completed − f) >= 0`。
- 面は `last_write`・`last_read` を持つ。submit は書き先に `last_write = f`、元に `last_read = f`。
- **書く前の待ちは max を取らず、`wait(last_write)` → `wait(last_read)` の順**に待つ (どちらも終わっていれば何もしない)。
- **番号を取る順** (P1-1): HW の命令ごとに `f = gfx2_fence_reserve(a)` (番号を決めるだけ、`issued` は進めない、印も出さない) → `be->emit(ctx, ex, f)` (描画だけ。f は backend の記録用の値で、印は出さない) → `gfx2_fence_commit(a, f, dst, src)` (描画の**後**に `be->fence_emit` で印を出し、`issued = f`、`last_write`/`last_read` を記録)。印は必ずその命令の描画より後ろに積まれる。コードは §11-2。
- 完了を見た時 (cpu_begin・submit・fence_query) に `done(f)` の欄を 0 に戻す。発行の番号が 2^30 の倍数を越えるたびに全部の面を掃除し、残る値と completed の距離を < 2^31 に保つ (1,024 面を回すだけ)。

### 6-2. CPU が触る前
- 1 か所 `gfx2_cpu_begin(surf, access)`: 読む → `wait(last_write)`。書く → `wait(last_write)`・`wait(last_read)`。`gfx2_cpu_end(surf, access)` は書いた時だけ、面の種類で 1 つを行う (C3。backend の hook は使わない):

| 面 | cpu_end の書きの後 |
|---|---|
| 同期層の DIRECT の別名 (`gfx2_sync_get` が DIRECT) | `gfxsync_flush(ctx)` を 1 回。rc は返さない (cpu_end は void、書きは済んでいる) |
| 同期層の SHADOW の別名 | `gfx2_dirty_add` (転送は C13 の present) |
| それ以外の VRAM の面 (GF の VRAM の面と VRAM の別名) | `arch_cpu_serialize()` (§10 の arch の口) で WC の書きを吐き出す。nouveau は CPU の書きを GPU に渡す前に `mb()` を置く (drm/nouveau/nouveau_dma.h:82-85 の `WRITE_PUT`)、その `mb()` に当たる。失敗は無い |
| SYSMEM・GART | 何もしない (WB、bus master は snoop。nouveau_bo.c:693 の `nouveau_bo_sync_for_device` も x86 の cached の頁では何もしない) |
- 通る入口: `gfx2_lock`/`unlock`、sw の 2D、present、v1 の橋 (§6-5)。

### 6-3. 失敗の完了と装置の喪失
- v2 の番号は GF の `gf2_fence.seq` と独立に中核が振る。backend は「v2 の番号 → 自分の印 (GF は `{epoch, seq}`)」を `fence_emit` の時 (= 描画の emit の後、§6-1) に輪 (256 件、G4 の `struct gf_v2_ring`) に記録し、`completed` と `failed_upto` を返す。
- 輪が満杯で次を発行する時: (1) `kick`、(2) 最古の項目の v2 の番号を `fence_wait` で待つ、(3) 終われば最古を外して記録。**未完了の項目は上書きしない**。(2) が TIMEOUT・EFAULT なら発行せず装置の失敗 (下の 2 行) にし、submit は走査 2 の rc としてそれを返す。
```c
/* drivers/video/geforce/gf_v2_fence.c (G4) */
int gf_v2_fence_emit(struct gf_v2 *g, u32 v2, u32 dl)
{
    struct gf_v2_ring *r = &g->ring;
    if (r->count == GF_V2_RING) {
        int rc = gf2_kick(g->ch);
        if (!rc) rc = gf2_fence_wait(g->ch, r->e[r->tail].f, dl);
        if (rc) return gf_v2_fail(g, rc);          /* failed_upto = 発行済みの最新、LOST へ */
        r->tail = (r->tail + 1) % GF_V2_RING; r->count--;
    }
    struct gf2_fence f; int rc = gf2_fence_emit(g->ch, &f, dl);
    if (rc) return gf_v2_fail(g, rc);
    r->e[r->head] = (struct gf_v2_ring_e){v2, f}; r->head = (r->head + 1) % GF_V2_RING; r->count++;
    return 0;
}
```
- GF の epoch が変わった (再初期化) ら、backend はそれまでに発行した v2 の番号を全部 `failed_upto` にする。中核は `failed_upto` 以下を待つ面のうち未完了だった書きの先を `LOST` にし、adapter の `gen` を上げる。`gen` が違う submit は STALE、LOST の面は `free` と `desc` だけ通り、lock は `OS32_ERR_STALE`。
- 待ちの上限 (`GF3_TIMEOUT_US` 等) を越えたら `OS32_ERR_TIMEOUT`、adapter を `LOST` にして同じ処理。上限は物理の制約ではない (切れたらユーザーへ報告)。
- 停止の確認に失敗した (`gf3_fini` が失敗し資源を保持する) 場合は adapter を `LOST_HELD` にする。この間は VRAM・GART の頁を一切返さない (§8 の回収も止める)。回復が成功して停止が確認できた時に初めて解放を進める。
- **LOST の後は自動の回復をしない** (ユーザー判断 2026-11-10)。v1: `gfx2_adapter_lost` (C3) は最後に `gfx_policy_reeval()` を呼ぶ。GF の `geforce_direct_ok` (backend_geforce.c:121-124) は G1 が足す 1 行で `gf_v2` が LOST の間 0 を返し、`gfx_direct_wanted` (gfx/gfx_policy.c:17-23) が偽になって v1 の hook は SHADOW へ移る (gfx_policy.c:47-52)。v2: LOST の面 (失敗した書きの先) は free と desc だけ、それ以外の面は `gfx2_check_op` の (2) で SW に回り、`kva` のある面 (と持ち主の AS に写像のある GART の面) はそのまま描ける。HW の口 (`be->emit`・`kick`・`fence_*`) は呼ばない。復帰は次の mode_set (`gfx2_mode_set` §9-3、または gfx_init のやり直しで C1 の `gfx2_adapter_register` が呼ばれた時 §11-7) が装置の初期化に成功した時に C3 の `gfx2_adapter_recovered` を呼ぶか、再起動。GF の `be->stop` と再開の中身 (channel が止まらなければ層 4 の失敗の道へ渡し、その後は再起動でだけ戻る) は末尾の「確定の後の追記 (2026-11-11、I3 の G2・D1、設計者決定)」の G11。

### 6-4. 正本と SHADOW
| 面 | 正本 | HW の操作 | CPU の書き |
|---|---|---|---|
| v2 の VRAM・GART の面 | 自分 (影は持たない) | 可 | lock の間に直接 |
| 同期層の SHADOW の表示面 (PEGC・Trident の SHADOW) | RAM の影 (sw の `view.base` = RAM の正本、`view.sync = NULL`。VRAM への転送は C13 の present だけ) | **しない** (`check_op` が SW を返す。Trident の GE も使わない) | 影に書き dirty に足す。present が `gfxsync_write_rows` で写す |
| 同期層の DIRECT の表示面 | VRAM | 可 (GE の後は `tri_sync` が fence の代わり) | lock は RAM の写し (staging) を渡す: lock で `gfxsync_read_rows` (DISCARD なら 0 で埋める)、unlock で面の全体を `gfxsync_write_rows`。生の VRAM の VA は渡さない。staging の頁と写像は §7-5 |
- 同期の対象は「同期の対象の別名面」(C14 が `gfx2_surf_alias` で作る表示面) だけ。通常の面 (SYSMEM・VRAM・GART の create の面) は ctx を拾わない (`gfx2_sync_get` を呼ばず 1 行目の扱い)。別名面のどの行を使うかは `gfx2_sync_get(a, head, &ctx, &mode)` (C1、§11-2) の 1 口だけで決める: `NOTFOUND` = 同期層の面でない (1 行目)、`mode == GFXSYNC_SHADOW` = 2 行目、`GFXSYNC_DIRECT` = 3 行目。C7 (lock の staging)・C8 (SW へ回す)・C9 (`gfx2_view.sync`)・C13 (present) はこの値だけを見る。面と head の対応は「別名の面の `role` の head」(C14 が作成の時に `gfx2_surf_alias` の `head` に入れる)。
| v1 の GeForce の DISPLAY (H2 の DIRECT) | VRAM | v1 の INDEX8 の口だけ | v1 の規則 (H2) |

### 6-5. v1 との境目 (gfx/v2/v1_bridge.c)
- 起動時に v1 の DISPLAY と CLIENT を v2 の表に**別名の面** (`pool=VRAM か SYSMEM`、`usage=SCANOUT|V1`、`owner=GFX2_OWNER_SCREEN`、free は INVAL) として載せる。gfx2_lock は同期層の DIRECT の別名 (§6-4 の 3 行目、staging を渡す §7-5) だけを画面の持ち主に許し、ほかの別名は INVAL (VRAM・影をアプリへ直に見せる道は v1 の DISPLAY の lease だけ)。
- 呼んだ者の識別は 1 口 (R7): C1 の `gfx2_caller_get(struct gfx2_caller *c)` が `caller_access_get_user` (include/redir_access.h:11-16) の 1 回の値から `{app_id, as, owner}` を作り、公開の入口はこれだけを使う。面の持ち主の照合は `owner` (ledger の owner = `as->owner`)、画面の持ち主の照合は `app_id == gfx_screen_owner()` (gfx_core.c:351-354、`appslot_gfx_owner` の app_id) で、2 つを混ぜない。カーネルの中の呼び手 (C14・C15) は `gfx2_caller` を作らず `struct gfx2_surf *` を直に持つ。
- 使える者: `gfx2_surf_get` は `owner == GFX2_OWNER_SCREEN` の面を、呼んだ者の `app_id` が `gfx_screen_owner()` の時だけ返す (持ち主の照合の唯一の例外)。sid の取得は `int gfx2_screen_surface(u32 adapter, u32 head, u32 role, u32 *sid)` (role = DISPLAY・CLIENT、持ち主でなければ BUSY)。
- 作り直し: v1 の mode の変更・`ledger_surface_regen`・H4 の方式の切り替えの後に gfx_core が `v1_bridge_rebind()` を呼ぶ (C14 の担当、呼ぶ行は gfx_core.c の 3 か所も C14 が足す)。C14 は v1 の今の値から `struct gfx2_alias_desc` を作って C4 の `gfx2_surf_alias_update` を呼ぶだけで、sid・gen・形状・kva を自分では書かない (P1-4)。開いている lock は C7 の `gfx2_lock_revoke` で先に閉じる (C14 は staging・写像に触らない)。
```c
/* gfx/v2/v1_bridge.c (C14)。起動時は gfx2_surf_alias で作り、以後は update だけ */
void v1_bridge_rebind(void)
{
    for (role = DISPLAY; role <= CLIENT; role++) {
        struct gfx2_alias_desc d; struct gfx2_surf *s = alias[role];
        int has;
        if (!s) continue;                              /* 起動時の gfx2_surf_alias が失敗した役 */
        has = !v1_alias_desc(role, &d);                /* 偽 = v1 に面が無い (テキストの画面など) */
        rc = gfx2_surf_quiesce(s, GFX2_REBIND_DL);     /* 0 / TIMEOUT / STALE (失敗の完了 = もう動いている物は無い) */
        if (rc == OS32_ERR_TIMEOUT) continue;          /* 別名は旧のまま (lock も閉じない) */
        (void)gfx2_lock_revoke(s);                     /* C7: DIRECT の別名の lock を書き戻さずに閉じる。0 / IO、
                                                          どちらでも戻った時は map_as・staging が無く state は IDLE か LOST */
        (void)gfx2_surf_alias_update(s, has ? &d : NULL);  /* 0 (NULL: 旧 sid は STALE、形を持たない w=h=0)。
                                                          d.adapter は今の ACTIVE の adapter (R5: 付け替えで別名も移る) */
    }
}
```
- rebind の時の lock: rebind は v1 の mode の変更・`ledger_surface_regen`・H4 の切り替えの**後**に走るので、staging の中身は前の形と前の方式の物で、今の面へは書き戻さない (DirectDraw の mode の変更で面が失われ中身が残らないのと同じ)。lock を持っていたアプリの後の `gfx2_unlock(旧 sid)` は gen+1 で STALE、写像を外した VA に触れば fault。v1 の DISPLAY の lease は v2 の面の状態ではなく `alias_update` は見ない (v1 の規則のまま)。
- `gfx2_lock_revoke` (C7 の追補、gfx/v2/lock.c。staging の後始末は lock.c の static `staging_unmap` (unlock と同じ道) を使うので lock.c に置く):
```c
int gfx2_lock_revoke(struct gfx2_surf *s)   /* C14 の rebind だけが呼ぶ。カーネルの呼び手なので gfx2_caller を使わない */
{
    int rc = 0;
    if (!(s->flags & GFX2_SF_ALIAS)) return OS32_ERR_INVAL;
    if (s->staging || s->map_as) rc = staging_unmap(s);   /* unmap の IO は §7-5 の unlock の IO と同じ: map_detach、頁は staging_owner に残す */
    if (s->state == GFX2_ST_LOCKED_RO || s->state == GFX2_ST_LOCKED_RW)
        (void)gfx2_surf_state_set(s, GFX2_ST_IDLE);       /* LOST はそのまま (alias_update が IDLE にする) */
    return rc;                                            /* 0 / IO。write_rows と cpu_end はしない (VRAM に書いていない) */
}
```
- 文脈: `paging_lease_unmap` は CR3 が kernel か `map_as` の時だけ通る (mm/paging_lease.inc:5-11)。別名を lock できるのは画面の持ち主だけ (§6-5) なので、持ち主の KAPI の中の mode の変更と kernel の文脈 (gfx_init) では外せる。ほかの AS の文脈で走った時の IO は §7-5 の unlock の IO と同じ扱い (PTE と頁はその AS の破棄で返る)。
- v1 の入口 (present_rect (gfx/gfx_vram.c:55-63・:284-298)・fill_rect・blit (gfx/gfx_core_api.inc:66-78)・fill_rop・put・mono・get・gfx_hw_submit:70・gfx_hw_get:95・osri_show・set_mode・page_flip・DISPLAY の lease の開始と終わり (exec/lease.c の `surface_lease`:245-296・`surface_lease_bundle`:302-353)) は全部 `v1_bridge_begin(alias, access)`/`end` を通す: CPU も v1 の HW も触る前に §6-2 の待ち (v2 の束が別名に書いた分を待つ)。v1 の GF の HW は同期なので (D9) `v1_bridge_end` の fence は 0 で、別名の面に番号を記録しない。v1 が非同期の HW を持つ時に初めて C3 に口を足す (A0 の版上げ)。
- lock か v1 の lease (全画面アプリの DISPLAY) が開いている面への投入は `OS32_ERR_BUSY` (読むだけの投入は READONLY の lock の間なら通す)。lease の終わり・unlock で `cpu_end` の吐き出し。

## 7. 面

### 7-1. 状態と許可
```c
typedef struct { u32 sid, gen; u16 w, h; u8 fmt, pool, usage, tiling; u32 pitch, bytes, gpu_off, state; } GFX2_SurfaceDesc; /* 32B */
int gfx2_surface_create(u32 adapter, u16 w, u16 h, u8 fmt, u8 pool, u8 usage, u32 *sid);
int gfx2_surface_free(u32 sid);   int gfx2_surface_desc(u32 sid, GFX2_SurfaceDesc *out);
int gfx2_lock(u32 sid, u32 flags, void **va, u32 *pitch);   int gfx2_unlock(u32 sid);
```
| 状態 | lock | unlock | submit (書く / 読む) | free | 次 |
|---|---|---|---|---|---|
| IDLE | 可 → LOCKED_RO/RW | INVAL | 可 / 可 | 可 → (scan_ref か未完了 fence があれば CLOSING、無ければ空) | |
| LOCKED_RO | BUSY | 可 → IDLE | BUSY / 可 | BUSY | |
| LOCKED_RW | BUSY | 可 → IDLE (dirty・吐き出し) | BUSY / BUSY | BUSY | |
| CLOSING | STALE | INVAL | STALE | INVAL | 条件が揃うと空 (§7-3) |
| LOST | STALE | 可 (LOCKED のまま失われた時) | STALE | 可 | |
- sid: 下位 10 bit = 表の位置 (1,024 面)、上位 22 bit = 世代 (解放で +1)。呼んだ者の `gfx2_caller.owner` (§6-5 の 1 口) と面の持ち主が違えば INVAL。
- lock の flags: READONLY・DISCARD・DONOTWAIT。**DISCARD でも待ちは同じ** (`wait(last_write)`・`wait(last_read)`)。DISCARD の効果は「中身を読まない」だけ: staging の面は lock の時の読みを省く。DONOTWAIT で未完了なら `OS32_ERR_BUSY`。

### 7-2. 確保と GF の予約
- pool: VRAM・GART・SYSMEM (MANAGED は libos32gfx)。usage: RENDER2D・SCANOUT・CURSOR・STAGING・TEXTURE (欄だけ)。tiling は backend が選び、CPU から線形に見える物だけ。
- VRAM の heap (gfx/v2/vram_heap.c) が置き場を決め、backend の `alloc` が装置の予約を作る。GF は `gf_vram_reserve(ch, surf, off, bytes, node)` を呼び、返った予約の id・世代を面の私物に持つ (`gf3_surface_check` がそのまま通る)。解放は GPU の使用の終わりと写像の解除の後に `gf_vram_release`。
- GART の面を GF で描くため `gf3_surface` の末尾に `u32 dma` (VRAM_DMA か GART_DMA の handle) を足し、`gf3_surface_set`・`gf3_surface_check` を領域ごとに分ける (段 G8)。GART の面の検査は予約ではなく GART の表 (§8) の範囲で行う。
- SYSMEM の面の backing (R2、§8 と同じ形。段 C18 `gfx/v2/sysmem.c`)。PEGC・Trident・PC-98 の adapter は SYSMEM だけを持つ:

| 項目 | 決め |
|---|---|
| 取り方 | 作成の時に `pgalloc_alloc_n_owner(owner, n, MEM_PHYS_EXEC_FLOOR / PAGE_SIZE, pgalloc_arena_end(), 0, &pfn)` で**物理で連続**の n 頁 (mm/pgalloc.h:228、範囲は drivers/video/trident/resources.c:171 と同じ形)。持ち主は面の持ち主 (呼んだ者の ledger owner)。dma_pool は使わない。GPU は触らないので pin しない |
| 上限 | 自前の上限は持たない。pgalloc の空き (アプリの私物と同じ) で、取れなければ `OS32_ERR_NOSPC`。1 面の大きさは caps の `max_w`・`max_h`・`max_pitch` まで |
| kernel の写像 | `kva = P2V(pfn << 12)` (連続なので sw の `view.base` にそのまま使える)。cache は WB |
| ユーザーの写像 | lock の時に C7 の `gfx2_map` が連続の 1 区間で張る (§7-4、PTE は `pa \| PAGE_RW \| PTE_USER`、WB) |
| 解放 (destroy) | 写像を外す → `pgalloc_free_n_owner(owner, pfn, n)`。待つ fence は無い (SW だけ) |
| 隔離 (§7-3 の (5)) | KEEP (写像を外せなかった) の時だけ来る。頁は動かさず `as->owner` のまま (poison された AS が保持し retire しない)。`gfx2_quarantine_drain` は KEEP を飛ばすので返さない |
| AS の回収との順 | `gfx2_owner_release` が reclaim より前に返すので、`ledger_reclaim_owner` と二重に返さない (§7-3) |
```c
/* gfx/v2/sysmem.c (C18)。C4 の gfx2_surf_new・gfx2_surf_destroy だけが呼ぶ */
int gfx2_sysmem_alloc(struct gfx2_surf *s, u32 owner)
{
    u32 n = (s->bytes + PAGE_SIZE - 1) / PAGE_SIZE, pfn;
    if (!pgalloc_alloc_n_owner(owner, (int)n, MEM_PHYS_EXEC_FLOOR / PAGE_SIZE, pgalloc_arena_end(), 0, &pfn))
        return OS32_ERR_NOSPC;
    s->sys_pfn = pfn; s->sys_n = n; s->kva = (u8 *)P2V(pfn << 12);
    kmemset(s->kva, 0, n * PAGE_SIZE);
    return 0;
}
int gfx2_sysmem_free(struct gfx2_surf *s)      /* 前提: 写像は外れている (map_as == NULL)、KEEP でない */
{
    if (!pgalloc_free_n_owner(s->owner, s->sys_pfn, (int)s->sys_n)) return OS32_ERR_IO;   /* 他の owner の頁: 返さない */
    s->sys_pfn = s->sys_n = 0; s->kva = 0;
    return 0;
}
```
- GART の面の CPU から (R3): GART の頁は連続でないので `kva` は NULL。作成の時に C4 が C7 の `gfx2_map` で持ち主の AS の gfx2 の帯へ連続の VA で張り (失敗なら面を返して NOSPC)、sw の `view.base = map_va`・`pitch` で見る。sw がこの base を使えるのは CR3 が持ち主の AS の時 (= 持ち主の KAPI の中の submit・lock) だけで、C8 の `gfx2_check_exec` は `gfx2_cpu_visible(s)` (C7: `kva` か `map_as == 今の AS`) が偽なら §4-2 の (5) で NOSUPPORT。present・v1 の橋・`gfx2_owner_release` は GART の面を sw で触らない。

### 7-3. 走査面の解放
- 面は `scan_ref` を持つ: head の今の走査面で +1、保留中の flip の先で +1。flip の反映 (vblank の後に backend が報告) か head の停止で −1。
- `free` は `scan_ref > 0` か fence が未完了なら CLOSING にして戻る。vblank の処理と `fence_query`・submit の入口で CLOSING を見回り、両方 0 になったら C4 の `gfx2_surf_destroy` で実際に返す。
- 返す順はこの 1 つだけ (§8・§11-2 も同じ)。TTM の `ttm_bo_release` と同じ順: fence の完了 (ttm_bo.c:265 の待ち・:277-311 の遅れた破棄) → CPU の写像の解除 (:272-273、ユーザーの写像は参照を持つので解放の前に消えている ttm_bo_vm.c:350-367) → GPU の写像の解除 (:185-186 の `delete_mem_notify` → nouveau_bo.c:1093 の `nouveau_vma_unmap`) → GART の unbind と頁の解放 (:188 → :1487-1494 → nouveau_bo.c:1442-1455 の unbind → `ttm_pool_free`)。

| 順 | 何を | 誰が |
|---|---|---|
| 0 | fence の完了 (公開 free は CLOSING の見回り、C15 は `gfx2_surf_quiesce`) | 呼び手 |
| 1 | `gfx2_unmap` (IO なら KEEP を立てて BUSY で戻る。以後は何もしない) | C4 → C7 |
| 2 | `be->release` (GF は VRAM の予約の解放、GART の面は何もしない) | C4 → backend |
| 3 | VRAM: `gfx2_heap_free` / GART: `gfx2_gart_free` (中で `gart_unbind` → unpin → 頁の解放、§8) / SYSMEM: `gfx2_sysmem_free` | C4 → C5・C6・C18 |
| 4 | 表を空き (gen+1) | C4 |
- 2 の失敗 (0 以外) と 3 の失敗 (`gfx2_gart_free` の IO、§8) は 1 の IO と同じ扱い: KEEP を立てて BUSY、backing は返さない。KEEP の面は (5) の隔離へ行き再起動まで返さない (下の (1))。同じ失敗の道を 1 つにする。
- AS の終了 (`gfx2_owner_release`、呼ぶ所は exec/exec_finish.inc:33-34 の `lease_revoke_all` の直後、36 行の poison の判定より前。X1 の担当):
```c
/* gfx/v2/owner_release.c (C15)。0 = この AS から完全に切り離した、
 * OS32_ERR_IO = CPU の写像を外せなかった (呼び手は as を poison する) */
int gfx2_owner_release(struct addrspace *as)
{
    int poison = 0;
    for each surf s with s->owner == as->owner || s->map_as == as:   /* 後者は as が lock した DIRECT の別名 (§7-5) */
        if (s->map_as == as) {
            if (gfx2_unmap(s)) { poison = 1; s->flags |= GFX2_SF_KEEP; }  /* (1) 外せない: PTE が残る。backing は返さない */
            gfx2_map_detach(s);                              /* 必ず: map_as = NULL・map_va = map_npages = 0 (PTE には触らない) */
        }
        gfx2_staging_drop(s);                                /* (2) C7: staging を返す。KEEP なら頁は staging_owner (= as->owner) の
                                                                まま欄だけ 0 (poison の AS が保持)。無ければ何もしない */
        if (s->flags & GFX2_SF_ALIAS) s->flags &= ~GFX2_SF_KEEP;   /* 別名は destroy・drain の対象でない。KEEP は staging のためだけ */
        if (s->state == LOCKED_RO || s->state == LOCKED_RW)
            (void)gfx2_surf_state_set(s, GFX2_ST_IDLE);      /*     lock を外す。表の遷移なので失敗しない */
    for each head h scanning a surf of as->owner:
        (void)gfx2_head_restore_v1(a, h);                    /* (3) 失敗 (IO) でも scan_ref は C13 が外す。外せない head は scan_ref が残り (5) へ */
    for each surf s of as->owner:
        if (!(s->flags & GFX2_SF_KEEP) && !s->scan_ref &&
            gfx2_surf_quiesce(s, GFX2_RELEASE_DL) == 0 &&
            gfx2_surf_destroy(s) == 0)
            continue;                                        /* (4) 待てて返せた物 */
        if (gfx2_surf_quarantine(s)) poison = 1;             /* (5) 残り全部 (destroy が BUSY の物も)。戻り値に関わらず
                                                                s->owner = GFX2_QUAR_OWNER・state = QUAR は済んでいる */
    gfx2_vaspace_drop(as);                                   /* (6) C7: as の VA の bitmap の枠を空ける (as_gen を 0) */
    return poison ? OS32_ERR_IO : 0;
}
/* 戻った時の保証 (rc に関わらず): どの s も s->map_as != as、s->owner != as->owner、gfx2_vaspace に as の枠が無い。
 * gfx2 の表に struct addrspace * は 1 つも残らない → exec_finish.inc:69 の kfree(a->as) の後も安全 */
```
- `gfx2_surf_quarantine` (C15) の契約と順:
```c
int gfx2_surf_quarantine(struct gfx2_surf *s)   /* 前提: s->map_as == NULL (違えば map_detach してから続ける) */
{
    int rc = 0;
    if (s->map_as) gfx2_map_detach(s);
    if (s->pool == GFX2_POOL_GART)
        rc = gfx2_gart_quarantine(s);   /* C6: 連続の区間ごとに ledger_transfer_pinned(.., s->owner, QUAR)。
                                           途中で 0 なら IO を返し、移せた頁数を s->gart_moved に。残りは as->owner の
                                           まま pin が付いている → X1 の poison で retire されず頁は保持 */
    s->owner = GFX2_QUAR_OWNER;  (void)gfx2_surf_state_set(s, GFX2_ST_QUAR);
    return rc;                          /* 0 / OS32_ERR_IO */
}
```
- 隔離の面の backing: `GFX2_SF_KEEP` (写像を外せなかった) の面は再起動まで返さない (`gfx2_quarantine_drain` は飛ばし、数を `gfx2_leak_pages` の診断に)。KEEP でない隔離の面は下の drain で返す。GART は `gart_moved` の頁だけ unpin → free (残りは poison された as->owner が持つ)。
- 戻った後、exec は今どおり 36 行以降 (私物の解放 → `paging_addrspace_destroy` → reclaim → retire → `kfree(a->as)`) を進める。隔離した頁はもう AS の owner の物ではないので reclaim に混ざらない。`OS32_ERR_IO` の時だけ X1 が `paging_addrspace_poison(a->as)` を呼び (exec_finish.inc:43 と同じ口) 今の poison の道 (頁を保持・retire しない、`kfree` はする) に入る。gfx2 は as を指さないので `kfree` の後も安全。
- 隔離の面は adapter の回復 (停止が確かめられた時、C3 の `gfx2_adapter_recovered`) と CLOSING の見回りで `gfx2_quarantine_drain` が返す。

### 7-4. ユーザーへの写像
- 帯: lease の帯の上の方を使う。`MEM_GFX2_MAP_BASE 0xF8000000`・`MEM_GFX2_MAP_END 0xFE000000` (96MB) は A0 が memmap.h に足し (§11-4)、lease の確保の上限 `MEM_LEASE_ALLOC_END = MEM_GFX2_MAP_BASE` の定義と exec/lease.c:69 の上限を `MEM_LEASE_ALLOC_END` にする書き換えは M3。`paging_lease_unmap` の帯の検査は `MEM_LEASE_END` のままで新しい帯を含む。
- 監査との共存 (R1、M3 と C17): `lease_check` (exec/lease.c:167-231) は lease の PT の全部の PTE を lease の表と照らし (:212-229)、合わない PTE を `LEASE_INVAL` と数える。`lease_audit_all` (:363-、呼ぶのは :380、入口は kernel/kaudit.c:22 の `kselftest_run_audit`) がこれを全 AS に回すので、gfx2 の写像をそのまま置くと監査が失敗する。lease の照合は lease の帯 (`MEM_LEASE_BASE`〜`MEM_LEASE_ALLOC_END`) だけにし、gfx2 の帯は gfx2 が照らす:
```c
/* exec/lease.c (M3)。PDE の照合 (:191-200) は今どおり 56 本全部 (gfx2 の帯の PT も as->owner の物で同じ形) */
#define MEM_LEASE_ALLOC_PDES ((MEM_LEASE_ALLOC_END - MEM_LEASE_BASE) >> MEM_PDE_SHIFT)   /* memmap.h (M3)、= 32 */
/* :176-177 の lease の範囲の検査 */
    l->base < MEM_LEASE_BASE || l->base >= MEM_LEASE_ALLOC_END ||
    l->npages > (MEM_LEASE_ALLOC_END - l->base) / PAGE_SIZE) return LEASE_INVAL;
/* :212 の PTE の走査 */
for (u32 di = 0; di < MEM_LEASE_ALLOC_PDES; di++) { ... 今の照合 ... }
/* lease_audit_all の :380 の次の行 */
bad += lease_check(a->as) != 0;
bad += gfx2_map_audit(a->as) != 0;              /* C17。A0 の weak は 0 を返す (C17 の前に写像は無い) */

/* gfx/v2/map_audit.c (C17)。lease_audit_all と同じ文脈 (CR3 = kernel、irq_save の中) */
int gfx2_map_audit(const struct addrspace *as)
{
    u32 want = 0, have = 0, pa, attr;
    for (u32 i = 0; i < GFX2_MAX_SURFACES; i++) {
        const struct gfx2_surf *s = &gfx2_surfs[i];
        if (s->map_as != as) continue;
        if (s->map_va < MEM_GFX2_MAP_BASE || s->map_npages > (MEM_GFX2_MAP_END - s->map_va) / PAGE_SIZE) return -1;
        for (u32 k = 0; k < s->map_npages; k++) {
            if (gfx2_surf_page(s, k, &pa, &attr)) return -1;          /* C7: k 頁目の物理番地と cache の属性 */
            if ((lease_pte_get(as, s->map_va + k * PAGE_SIZE) & ~(PTE_ACCESSED | PTE_DIRTY)) !=
                (pa | PAGE_RW | PTE_USER | attr)) return -1;
        }
        want += s->map_npages;
    }
    for (u32 di = MEM_LEASE_ALLOC_PDES; di < MEM_LEASE_MAX_PDES; di++)  /* 帯の present の PTE は写像の分だけ */
        if (as->lease_pt_phys[di]) have += pte_present_count(P2V(as->lease_pt_phys[di]));
    return have == want ? 0 : -1;
}
```
- 監査の時間: 面 1,024 個の走査と帯の PT 24 本 × 1,024 PTE の数え上げで、lease の照合の PTE × 8 本の形より軽い。`lease_pte_get`・`pte_present_count` は C17 の static (P2V で PT を読むだけ)。
- PT: 新しい帯の PDE は今の `as->lease_pt_phys[k]` (k = (va − MEM_LEASE_BASE) >> 22) をそのまま使う。無ければ `paging_gfx2_map` が `pgalloc_alloc_phys(as->owner, 1)` で取り、0 で埋め、`pd[LEASE_PDE + k] = phys | PAGE_RW | PTE_USER` にする (lease_context の検査と同じ形)。AS の破棄 (paging_addrspace.inc:115-120) が今どおり返す。
- 張る: `paging_gfx2_map(as, va, pa, npages, pte_flags)` (M2) を連続の区間ごとに。文脈は `paging_lease_root_context(as)` (CR3 が kernel かその AS、IRQ・例外の外) を要する。KAPI の中 (CR3 = その AS) から呼べる。PTE は `pa | PAGE_RW | PTE_USER | cache`、VRAM は exec/lease.c:26 `alias_cache` と同じ属性、GART は WB。区間の途中で失敗したら gfx2 が張った区間を `paging_lease_unmap` で外して、その区間の rc (INVAL・NOSPC) を返す (全部か無し)。TLB は `invlpg` を使わない (i386 互換、arch/x86/arch_cpu.h:46-52)。lease と同じく、張り終えた後に CR3 がその AS なら `paging_load_cr3(as->pd_phys)` で読み込み直す (mm/paging_lease.inc:126 の型)。kernel の CR3 の時は何もしない (その AS の TLB は CR3 の切り替えで消える)。
- `paging_gfx2_map` の契約 (M2)。手本は `paging_lease_map` (mm/paging_lease.inc:59-133) と同じで、違いは ledger の面の照合と lease の表への記録が無いことだけ:

| 項目 | 決め |
|---|---|
| 文脈 | `lease_context(as)` (paging_lease.inc:13-27) が偽なら −1 |
| va・npages | va は `MEM_GFX2_MAP_BASE` 以上 `MEM_GFX2_MAP_END` 未満で頁境界、`npages >= 1`、`npages <= (MEM_GFX2_MAP_END − va) / PAGE_SIZE`。外れは −1 |
| pa | 頁境界。`npages > (0xFFFFFFFF − pa) / PAGE_SIZE + 1` (u32 のあふれ込み) は −1。VRAM の BAR は物理の上の方にあるので `PHYSMEM_MAX_PFN` では縛らない |
| pte_flags | 受け付けるのは `PTE_PRESENT・PTE_USER・PTE_RW・PTE_PCD・PTE_PWT` だけで、`PTE_PRESENT | PTE_USER` は必須 (paging_lease.inc:83-84 と同じ)。ほかの bit があれば −1 |
| 既存の PTE | 範囲の中に present の PTE が 1 つでもあれば −1 (:96 と同じ。上書きしない) |
| PT | 無い PDE の PT は先に全部取って 0 で埋め (:97-100)、取れなければ取った分を返して −2。検査が全部通ってから irq_save の中で PDE と PTE を書く (:104-116) |
| 失敗の後 | −1・−2 とも PDE・PTE は 1 つも書いていない、取った PT は返した (:129-131)。C7 は −1 を INVAL、−2 を NOSPC にする |
| TLB | CR3 がその AS なら `paging_load_cr3` (:126) |
- 外す: 今の `paging_lease_unmap(as, va, npages)` (帯の中なら ledger に縛られず、頁を返さない)。free・AS の終了・LOST で外す。VA は gfx/v2/lock.c の AS ごとの bitmap (§11-2 `struct gfx2_vaspace`) が配る。
- 写像は lock をまたいで残すが、触ってよいのは lock の間だけ (シングルタスクなので PTE の付け外しで縛らない)。libos32gfx の wrapper は必ず lock を通す。DIRECT の別名の staging だけは lock の間だけ張る (§7-5)。

### 7-5. DIRECT の別名の staging (C7)
アプリに渡す staging は頁単位で取り、kmalloc の領域は渡さない。手本は Linux の fbdev の影 (drivers/gpu/drm/drm_fbdev_ttm.c:200 の `vzalloc` で頁単位に取り、drivers/video/fbdev/core/fb_defio.c:125-135・222-244 で 1 頁ずつユーザーへ渡す)。

| 項目 | 決め |
|---|---|
| 対象 | 同期層の DIRECT の別名の lock だけ (§6-4・§6-5)。通常の面とほかの別名は staging を持たない |
| 取り方 | lock の時に `pgalloc_alloc_n_owner(caller.owner, n, MEM_PHYS_EXEC_FLOOR / PAGE_SIZE, pgalloc_arena_end(), 0, &pfn)`、`n = (pitch·h + PAGE_SIZE − 1) / PAGE_SIZE` の物理で連続の頁 (§7-2 の SYSMEM と同じ形)。頁を他の物と共有しない。`staging = P2V(pfn << 12)`・`staging_pitch = s->pitch`・`staging_pfn = pfn`・`staging_n = n`・`staging_owner = caller.owner` |
| 中身 | DISCARD なら 0 で埋める (前の持ち主の中身を見せない)。ほかは `gfxsync_read_rows` で面の全体を読む |
| 写像先 | 呼んだ AS の gfx2 の帯 (§7-4)。`gfx2_map` は `gfx2_surf_page` から頁を引く。`gfx2_surf_page` は staging がある間は `staging_pfn + k` (WB) を返す (C17 の監査も同じ値で照らす) |
| 寿命 | lock から unlock まで。別名は画面の持ち主が替わるので、AS をまたいで写像を持ち越さない |
| unlock の順 | `gfxsync_write_rows` (面の全体) → `gfx2_cpu_end` (§6-2 の `gfxsync_flush`) → `gfx2_unmap` → `gfx2_staging_drop` → IDLE |
| 失敗 | lock: 頁が取れない → NOSPC。`gfx2_map` の失敗 → 頁を返してその rc。`gfxsync_read_rows` の失敗 → 写像と頁を戻して IO。どれも状態は IDLE のまま。unlock: `write_rows` が失敗しても順を最後まで進めて IO (状態は IDLE)。`gfx2_unmap` の IO → `gfx2_map_detach` し、staging の欄を 0 にして頁は `staging_owner` のまま残し IO (PTE はその AS に残るが、AS の破棄 exec_finish.inc:55 が PT を外した後の reclaim :62-64 で返る) |
| `gfx2_staging_drop` | `pgalloc_free_n_owner(staging_owner, staging_pfn, staging_n)` で返して欄を 0。KEEP の面は返さず欄だけ 0。呼ぶのは C7 (unlock) と C15 (§7-3 の (2)) |
| AS の終了 | §7-3 の (1)・(2)。別名は `s->map_as == as` で拾う |

### 7-6. VRAM の heap (C5)
手本は nouveau の `nvkm_mm` (drm/nouveau/nvkm/core/mm.c) の head 側。`blk[0..n)` は offset の順に [base, base+size) を隙間なく覆う区間の並び (使用中と空き)。

| 項目 | 決め |
|---|---|
| init | `size == 0` は空の heap (`n = 0`、alloc は全部 NOSPC。shim の `heap_range` が 0 を返す時)。`base + size` が u32 であふれる → INVAL。ほかは空きの 1 区間 |
| alloc の選び方 | first-fit: offset の小さい空きの区間から順に `s = (off + align − 1) & ~(align − 1)` を取り、`s + bytes <= off + len` なら採る (mm.c:121-139)。`s` や `s + bytes` の計算があふれた区間は飛ばす |
| 分割 | 頭の端数 (`s − off`) と尻の残りをそれぞれ空きの区間にする (mm.c:142-146 の `region_head`)。足りる空きが無ければ NOSPC |
| align | 1 以上の 2 の冪。それ以外 (0 を含む) は INVAL。整列するのは返す offset (VRAM の中の位置) だけで、bytes は丸めない (丸めは C4 が pitch で済ませる) |
| bytes | 0 は INVAL |
| 区間の上限 | 分割で `n` が `GFX2_HEAP_BLOCKS` を越えるなら NOSPC で何も変えない (先に要る数を数える。mm.c:143-148 の -ENOMEM と同じ扱いで、途中の分割も残さない) |
| free | `off` が使用中の区間の先頭と一致しなければ INVAL で何も変えない (二重の free も INVAL)。空きにして前後の空きと結合する (mm.c:56-69) |
| 戻り値 | 0/NOSPC/INVAL。失敗では `heap` を変えない |

## 8. GPU から見える RAM

| 項目 | 決め |
|---|---|
| 取り方 | 必要な時に `pgalloc_alloc_n_owner` で 4KB 頁ごと (連続でなくてよい)。持ち主は面の AS の owner。dma_pool は使わない。`n = (bytes + PAGE_SIZE − 1) / PAGE_SIZE`、`n == 0` は INVAL |
| C6 の作成の順と失敗 (`gfx2_gart_alloc`) | (1) pfn の表 (gart.c の static、`GFX2_GART_KB / 4` 本、上限の 8MB で 2,048 本) に連続の n 本を first-fit で取る。無ければ NOSPC → (2) n 頁を 1 頁ずつ取る。途中で取れなければ取った頁を返して NOSPC → (3) `pgalloc_pin(pfn, 1, owner)` を各頁。失敗 (起きないはず) は pin を外し頁を返して IO → (4) `be->gart_bind`。失敗は (3)〜(1) を逆に戻してその rc (NOSPC・IO) を返す。どの失敗でも面は作られず、表・頁・pin は元のまま |
| 上限 (PM 決定 #1) | **遅延確保の上限 8MB** (gart.c の定数 `GFX2_GART_KB 8192`。system.cfg の設定は持たない)。予約はしない。越えれば `OS32_ERR_NOSPC` (`OS32_ERR_NOMEM` は無い) |
| pin | pgalloc に頁ごとの pin の数 (`u8 pin_map[PHYSMEM_MAX_PFN]`、pgalloc_owner.inc、M1) を足し、`pgalloc_free_n_owner`・`ledger_transfer`・`ledger_reclaim_owner` の**全部**で pin の頁があれば全件不変で拒否し `ledger_bad_free` に数える (reclaim は pin の頁を飛ばして返さず、戻り値 0)。pin の頁を動かせるのは `ledger_transfer_pinned` (隔離へ移すため) だけ |
| 物理の表 | 中核が `u32 pfn[]` を持ち、backend の `gart_bind(pfn, n, *gpu_off)`・`gart_unbind(gpu_off, n)`。`pfn` の n 本は `gart_unbind` までの間、場所も中身も変えない (C6 の static の表。backend は借りて持ち、装置の再初期化の時に頁表を書き直す、§11-6 の「GART (G5)」)。`gpu_off` は GART の DMA の物の中のバイトの offset (頁の境界)。`gart_bind` は全部か無し: 途中で失敗したら backend が自分の書いた頁表の項目を消してから失敗を返す (中核は巻き戻さない。nouveau の `nvkm_vmm_ptes_get` が失敗の時に取った分を自分で外すのと同じ、nvkm/subdev/mmu/vmm.c:750-755。`nouveau_sgdma_bind` も自分で `nouveau_mem_fini` して返す、nouveau_sgdma.c:45-50)。GART の空きが足りなければ NOSPC。`gart_unbind` は頁表の項目を消すだけで失敗しない (nouveau の `nv04_vmm_pgt_unmap` は 0 を書くだけで戻り値が無い、nvkm/subdev/mmu/vmmnv04.c:59-64)。0 以外を返すのは bind していない範囲 (呼び手の誤り) だけ。GF6200 (Ra266 の 10de:0221 = NV44A) は nouveau の `nv4a_chipset` が `.mmu = nv04_mmu_new` (nvkm/engine/device/base.c:677-688) で、GART は nv04 の「頁表付きの PCI の DMA の物」(nvkm/subdev/mmu/vmmnv04.c、MIT)。nv44 の専用 GART (mmu/nv44.c・vmmnv44.c の 0x1008xx の設定と flush) は NV44A では使わない。細目は §11-6 の「GART (G5)」。ATI は PCI GART (`r100_pci_gart_*`、MIT) |
| cache | WB (bus master は snoop)、DMA_PHYS_LIMIT は `dma_range_ok` |
| CPU から (R3) | `kva` は NULL。作成の時に持ち主の AS の gfx2 の帯へ連続の VA で張り (C7 の `gfx2_map`)、sw は `view.base = map_va` で見る。使えるのは CR3 が持ち主の AS の時だけ (`gfx2_cpu_visible`、§7-2)。張れなければ作成は NOSPC |
| 解放 | 順は §7-3 の表 (fence の完了 → CPU の写像の解除 → `be->release` → `gfx2_gart_free`)。`gfx2_gart_free` の中は `gart_unbind` → `pgalloc_unpin` → `pgalloc_free_n_owner` → pfn の表の n 本を空き。LOST_HELD の間は何もせず BUSY (§6-3)。`gart_unbind` が 0 以外なら unpin も解放もせず IO を返し、C4 は KEEP を立てる (§7-3、頁は pin のまま隔離へ行き再起動まで返さない)。C4 と C6 の口は `gfx2_gart_alloc`・`gfx2_gart_free` の 2 つだけで、unbind と unpin を分けた口は持たない |
| 回収の失敗 | GPU の待ちが終わらない GART の頁は §7-3 の (5) で隔離の owner (`GFX2_QUAR_OWNER`、C1 が最初の `gfx2_adapter_register` で `ledger_owner_new(LEDGER_KIND_MODULE, 0, "gfx2-quar", &gfx2_quar_owner)` で取る。DEVICE は RAM を持てない (mm/pgalloc.h:101、`owner_ram` が拒む pgalloc_owner.inc:14) ので MODULE。MODULE の owner は頁がある間 retire されない (pgalloc_owner.inc:104-105)) へ移し、AS の回収と retire は今どおり進む。隔離の頁は adapter の停止が確かめられた時に `gfx2_quarantine_drain` が unpin → `pgalloc_free_n_owner(GFX2_QUAR_OWNER, pfn, n)` で返す。LOST_HELD の間は返さない |
| 隔離の owner が取れない時 (C1) | `ledger_owner_new` が 0 (ledger の未初期化か owner の枠の不足) でも登録は続け、`gfx2_adapter_register` は 0・ACTIVE は成り立つ (表示は止めない)。`gfx2_quar_owner` は 0 のまま kprintf を 1 回。0 の間は次の `gfx2_adapter_register` (gfx_init のたび) でもう一度取る。0 の間の隔離は `gfx2_gart_quarantine` が移さずに IO (`gart_moved = 0`) を返し、頁は pin のまま `as->owner` に残る → X1 の poison で保持 (§7-3 の (5) の既存の道)。面の `owner = GFX2_QUAR_OWNER` (= 0) は ledger の owner に無い番号 (pgalloc.h の「0 は空き」) なので `s->owner != as->owner` の保証は崩れない |
| GF の staging | `pci_dma` と 1 頁の staging は GF の中に残す |

## 9. 列挙と表示

### 9-1. 列挙 (構造体の大きさと offset は A0 で固定)
```c
int gfx2_adapter_count(void);
int gfx2_adapter_info(u32 a, GFX2_AdapterInfo *o);          /* 128B */
int gfx2_head_info(u32 a, u32 head, GFX2_HeadInfo *o);       /* 64B */
int gfx2_connector_info(u32 a, u32 c, GFX2_ConnectorInfo *o);/* 64B */
int gfx2_connector_edid(u32 a, u32 c, u32 block, u8 out[128]);
int gfx2_mode_count(u32 a, u32 head, u32 fmt);
int gfx2_mode_get(u32 a, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *o);  /* 40B */
```
| 構造体 | 欄 (順に) |
|---|---|
| AdapterInfo | `u16 vendor, device; u32 subsys; u8 revision, bus, dev, fn; char name[64], driver[32]; u32 driver_ver, vram_bytes, gart_bytes; u8 heads, connectors, backend_kind, flags; u32 reserved` (flags: ACTIVE・ONBOARD) |
| HeadInfo | `u32 head, attached, connector_mask; i16 x, y; u16 w, h; u32 rotation (0), mode_id, scanout_sid, fmt_scanout; u16 max_w, max_h; u32 reserved[6]` (= 64B) |
| ConnectorInfo | `u32 id, type, possible_heads, clone_mask, status, edid_blocks; char monitor_name[16]; u16 mm_w, mm_h; u32 preferred_mode_id, reserved[4]` (type: VGA・DVI_I・DVI_D・TV_COMPOSITE・TV_SVIDEO・PC98_ANALOG) |
| ModeInfo | `u32 id; u16 w, h; u32 fmt, refresh_mhz, pixclk_khz; u16 htotal, vtotal; u32 hsync_khz10, flags; u8 source, scaling; u16 pad; u32 reserved` |
- 列挙は probe が通った全部の adapter。机 (絵) を出すのは 1 adapter (flags の ACTIVE)、その adapter の 2 heads は使える。ACTIVE でない adapter の head 系の口 (head_info 以外の mode_set・flip・vblank_wait・palette・gamma・cursor_*・dpms・present・screen_surface) は `OS32_ERR_NOSYS`、`gfx2_surface_create` も NOSYS (描かせない)。口は adapter・head の番号を持つまま (同時出力へ広げても ABI は変えない)。ユーザー決定 #3「AとC」(DECISIONS.md 末尾) で確定。
- C (ACTIVE でない側の TVRAM にコンソールを出し続ける) は HAL v2 の中で設計しない。別の票 **CONSOLE_DUAL** (仮名、「GUI の間も ACTIVE でない側の TVRAM にコンソールを出し続ける」) で設計する (確定設計 [CONSOLE_DUAL_DESIGN](CONSOLE_DUAL_DESIGN.md))。HAL v2 の段 (C1 など) は C の仕事を持たない。ただし CONSOLE_DUAL の暫定の口 `gf_outputs` は HAL v2 の列挙に差し替える (CONSOLE_DUAL_DESIGN.md:67・167、段 K1 §12)。PC-98 の adapter は `kind = GFX2_KIND_PC98`・`flags = 0` で登録するだけ (A の列挙)。
- CONSOLE_DUAL が受け持つ経路 (第 4 回のレビューの指摘): gfx/gfx_core_init.inc:9-16 の GF/Trident の早い戻り / kernel/console/console.c:53-56・195-204 の GUI 中の TVRAM の出力の停止 / console.c:434-443 の復帰の時の消去 / init/kernel.c:473 などの切り替えの時の消去 (`_gfx_common_init` の TVRAM の消去 `display_text_hide()` (gfx_core.c:178、実体は platform/pc98/gfx_init.c:10) を含む)。

### 9-2. 表示の口 (公開) と v1 への変換 (完全な署名は §11-1)
| gfx2_* | GfxBackendV2 | v2 の表の無い backend (Trident・PC-98) で C16 の shim が呼ぶ v1 の hook |
|---|---|---|
| `gfx2_mode_set(a, head, conn_mask, mode_id, sid)` | `mode_set` | `set_mode(id)` (head=0 だけ、sid は v1 の DISPLAY の別名だけ) |
| `gfx2_flip(a, head, sid, flags, deadline)` | `flip` | `page_flip(head, GfxScanout)` (sid の予約から GfxScanout を作る) |
| `gfx2_vblank_wait(a, head, after, deadline, *seen)` | `vblank_wait` | `vsync` |
| `gfx2_palette(a, first, count, const u32 *xrgb)` | `palette` | `set_palette` (u32 → 3B に詰める) |
| `gfx2_gamma(a, head, const u16 rgb[3][256])` | `gamma` | `gamma` |
| `gfx2_cursor_image(a, head, sid)`・`cursor_move`・`cursor_show` | `cursor_*` | `cursor_*` (sid の ARGB を GfxCursorImage に) |
| `gfx2_dpms(a, head, state)` | `dpms` | `dpms` |
| `gfx2_present(a, head, rect)` | — (中核) | SHADOW の表示面の dirty を `gfxsync_write_rows` で |
| `gfx2_fence_wait(a, fence, deadline_ms)`・`gfx2_fence_query(a, *completed, *failed_upto)` | `fence_*` | 常に完了 |
- `gfx2_cursor_image` の面の寿命 (R16): 呼んだ時に C13 が `gfx2_cpu_begin(s, R)` で sid の画素を読み、backend の cursor の記憶へ写し切って (GF は今の backend_geforce.c:336-347 と同じく写す) `gfx2_cpu_end` で戻る。面の参照 (scan_ref 等) は持たないので、呼んだ後に free してよい。cursor を消すのは `cursor_show(.., 0)`。
- パレットの写し (C2・C9 の元): 中核は `u32 gfx2_pal[256]` (XRGB8888、実体は C13) を INDEX8 の色の正に持つ。palette.c の 16 色・4bit の影は元にしない。書く時は 3 つ: (1) `gfx2_palette` が `be->palette` の 0 の後に [first, first+count) を写す (C13)。(2) v1 の set_palette の経路 `gfx_set_palette_hal` (gfx/gfx_core_api.inc:50-59) と `gfx_lease_palette` (:102-130、成功の時だけ) が backend の set_palette を呼んだ後に、0〜15 の成分を backend と同じ ×17 で 8bit にして `gfx2_pal_set_v1` で写す (呼ぶ行は C14)。(3) gfx_init のたびに `palette_shadow_reset` (gfx/gfx_core_init.inc:4) の直後に `gfx2_pal_reset16` で 0〜15 番を既定の 16 色 (palette.c:14-31 を ×17) へ戻す。v1 と見え方をそろえるためで、16〜255 番は保つ (呼ぶ行は C1)。読む所は INDEX8 → RGB の変換 (C2 の `gfx2_to_argb` の `pal256`) と `gfx2_view.pal` (C9) だけ。
- 表示を変える口 (mode_set・flip・palette・gamma・cursor・dpms) は画面の持ち主 (`gfx_screen_owner` の持ち主、GUI なら gshell、全画面アプリならそのアプリ) だけ。他は `OS32_ERR_BUSY`。

### 9-3. mode_set の境目 (gfx/v2/modeset.c)
```c
if (!screen_owner(caller)) return OS32_ERR_BUSY;
if (head >= a->heads || !conn_mask || (conn_mask & ~a->conn_possible[head])) return OS32_ERR_INVAL;
if (popcount(conn_mask) > 1 && !clone_ok(a, head, conn_mask)) return OS32_ERR_NOSYS;
m = mode_find(a, head, mode_id); s = surf_get(sid);            /* 無ければ INVAL / STALE */
if (s->adapter != a || !(s->usage & SCANOUT) || s->fmt != m->fmt || s->w < m->w || s->h < m->h ||
    s->gpu_off % a->caps.scanout_align || s->pitch % a->caps.pitch_align) return OS32_ERR_INVAL;
rc = gfx2_surf_quiesce(s, GFX2_MODESET_DL);                     /* 0 / OS32_ERR_TIMEOUT / OS32_ERR_STALE */
if (rc) return rc;                                              /* 表示・scan_ref・世代は無変更 */
if (a->state == GFX2_AD_LOST_HELD && a->be->stop(a->ctx))       /* 停止が確かめられない間は mode も変えない */
    return OS32_ERR_STALE;
rc = a->be->mode_set(a->ctx, head, conn_mask, m, s);            /* 失敗時は backend が退避した状態へ戻す */
if (rc) return rc;                                              /* 世代も scan_ref も変えない */
if (a->state != GFX2_AD_OK) gfx2_adapter_recovered(a);          /* §6-3: LOST からの復帰はここ (と C1 の再登録) だけ */
gfx2_scan_ref_swap(a, head, s);  gfx_policy_scan_changed();   /* H4 の唯一の遷移の口 (gfx_policy.h:14-16、D3)。module の側の G* から呼ぶ所も同じ口 */
if (fullscreen_app(caller)) a->restore_on_leave = 1;            /* 全画面を抜ける時に gshell の mode と走査面へ戻す */
return 0;
```

## 10. CPU 直描きの同期バックエンド層

置き場: `/etc/gfx/sync.cfg`。定数 `SYS_GFXSYNC_CFG "/etc/gfx/sync.cfg"` は A0 が include/config.h の `SYS_SYSTEM_CFG` (:41) の次の行に足す ([C4])。区画は機器の鍵、値は整数と列挙だけ:
```
[pegc]
mode=SHADOW        ; ユーザー決定: PEGC はバックバッファを維持 (DIRECT にしない)
unit=4  method=PLAIN  flush=NONE  read_ns=0  read=SHADOW
[pci:1023:*]       ; Trident (Ra266 内蔵)
mode=SHADOW  unit=4  method=GAP  gap_ns=80  flush=NONE  read_ns=1500  read=SHADOW
```
| 項目 | 値 | 意味 |
|---|---|---|
| mode | SHADOW・DIRECT | 正本が RAM の影か VRAM か |
| unit | 1・2・4 | 1 回の書きの幅 (バイト) |
| method | PLAIN・GAP・SYNC_READ・WAIT_PORT | 書きの後の待ち。今の `TRI_PRESENT_*` (present.c:27・38-52) と 1 対 1 (BYTE は unit=1 + PLAIN) |
| gap_ns | 0〜100000 | GAP の間隔。`cpu_loops_per_tick()` (kernel/time/cpu_calibrate.c:115) で回数に直す (write_gap.c の算数をそのまま移す) |
| flush | NONE・READBACK・SERIALIZE | WC の吐き出し方 (UC の読み、または直列化の命令) |
| read_ns・read | 整数・SHADOW/DIRECT | 読み出しの遅さと読む元。ROP・α・dst の鍵のように dst を読む sw の操作は read=SHADOW なら RAM から |
- I/O 番地は設定に書かない。SYNC_READ・WAIT_PORT の実体は backend が `GfxSyncTarget` の hook で渡す (Trident の CR+6 の読み present.c:46-49、PC-98 の 5Fh present.c:50-52)。
- 鍵の照合 (Y1): `pci:VVVV:DDDD` → `pci:VVVV:*` → 機種名 (`pegc`) の順。どれも無ければ組み込みの既定 (SHADOW・unit=1・PLAIN・flush=NONE)。

PM 決定 #2: `TRI_WRITE_GAP` はここへ移す (既定 80ns)。旧値の引き継ぎと「sync.cfg が system.cfg より優先」は配備の手順で保証し、カーネルは system.cfg の旧鍵を読まない。
```c
typedef struct GfxSyncTarget {           /* include/gfx_sync.h */
    volatile u8 *va; u32 bytes, pitch; u8 fmt, head;
    int (*sync_read)(void *);            /* method=SYNC_READ */
    int (*wait_port)(void *);            /* method=WAIT_PORT */
    int (*flush)(void *);                /* flush=READBACK (UC の読みの先は backend が知る) */
    int (*read32)(void *, u32 off, u32 *v); /* read=DIRECT */
    void *opaque;
} GfxSyncTarget;
typedef struct GfxSyncCtx GfxSyncCtx;    /* 中身は gfx/sync/ の中だけ。head・面ごとに 1 つ */
int gfxsync_bind(const char *key, const GfxSyncTarget *t, GfxSyncCtx **out); /* 0 / OS32_ERR_FULL (ctx 4 本) / INVAL */
int gfxsync_write_rows(GfxSyncCtx *, u32 x, u32 y, u32 w, u32 h, const u8 *src, u32 pitch);
int gfxsync_read_rows(GfxSyncCtx *, u32 x, u32 y, u32 w, u32 h, u8 *dst, u32 pitch);
int gfxsync_flush(GfxSyncCtx *);
void gfxsync_unbind(GfxSyncCtx *);
#define GFXSYNC_SHADOW 1
#define GFXSYNC_DIRECT 2
int gfxsync_mode(const GfxSyncCtx *);    /* Y4。bind の後の実効の mode (既定へ落ちた後の値)。NULL は 0 */
```
- ctx の受け渡し (P1-2): backend (P1・T1) は head ごとに `gfxsync_bind` → `gfx2_sync_publish(kind, head, ctx)`。v1 の mode の変更などで bind し直す時は `gfx2_sync_publish(kind, head, NULL)` → `gfxsync_unbind(old)` → `gfxsync_bind` → `gfx2_sync_publish(kind, head, new)` の順で、bind し直すのは backend (P1・T1) の担当。中核は ctx を写して持たず、使うたびに `gfx2_sync_get` で引く。bind し直しは v1 の mode の変更の中なので C14 の `v1_bridge_rebind` より前に終わる (gfx_core の呼ぶ順、C14 が足す行は backend の set_mode の戻りの後)。
| 組み合わせ | bind の扱い |
|---|---|
| method=SYNC_READ で `sync_read` が NULL、WAIT_PORT で `wait_port` が NULL、flush=READBACK で `flush` が NULL | その区画を捨て組み込みの既定 (SHADOW・unit=1・PLAIN・flush=NONE) + kprintf |
| mode=DIRECT かつ read=SHADOW (影が無い) | 同上 |
| read=DIRECT で `read32` が NULL | read=SHADOW に (mode=SHADOW の時)、DIRECT なら既定へ |
| flush=SERIALIZE | Y3 の sync_write.c が arch の口 `arch_cpu_serialize()` を呼ぶ、hook 不要。`#ifdef` で分けない。口は arch/x86/arch_cpu.h (`cpuid`) と arch/ppc/arch_cpu.h (`sync`) に置く (A0)。HAL の CPU の段の判定 (MMX Pentium 以上、ユーザー決定 2026-11-08) が使う `cpuid` も同じ所の口 `arch_cpu_has_mmx()` にし、arch/ppc は「MMX 相当なし」(0) を返す |
- 端数: 行の先頭と末尾の unit に満たないバイト・未整列の部分は 1 バイトずつ (今の present.c:57 と同じ)、method の待ちは unit の書きのあとごと。境界の検査 (present.c:18-25 の相当) は bind の `bytes`・`pitch` で中で行う。
- Trident の移し方 (T1、Y2・Y3 が受け皿): (1) drivers/video/trident/write_gap.c (21 行) の算数を gfx/sync/sync_gap.c の `gfxsync_gap_loops(lpt, ns)` へ (Y2)、(2) regs.c:117-138 の `write_gap32` と present.c:26-56 の 3 方式 (GAP・SYNC・WAIT) を gfx/sync/sync_write.c へ (Y3)、(3) present.c は境界の検査 (:18-25) と `gfxsync_write_rows` の呼び出しだけに、(4) trident_module.c:198-199 の system.cfg の `TRI_WRITE_GAP` を読む 2 行を削除し sync.cfg の `gap_ns` に (既定 `TRI_WRITE_GAP_DEFAULT_NS` 80、trident.h:581 と同じ値)。host 試験 tools/tests/trident_module_host.c:268・328 の 65・7・38 の場合は sync の host 試験 (Y1・Y2) へ移す。write_gap.c を消すので、旧パスの .o・.d には空の規則を 1 本置く (96c5835e の ime_render_tvram.c と同じ型、次の大きな整理で消してよい)。

## 11. 今の版との関係と A0 (宣言だけを先に置く段)

- 着地の前提: GFX_ACCEL_HAL の H1〜H6 と G1〜G3 が main に入っていること。v1 の INDEX8 の口・`GFX_ScreenInfo`・`GFX_CAP_*` は変えない。
- 着地の前提は満たした (D1、4f971604)。
- **A0 は宣言と仮の実体だけを置く段** (1 人、ほかの全段より先)。中身: §11-1〜11-7 の全部のヘッダ、kapi.json の末尾の gfx2_* (83→84、全部 NOSYS)、内部の全関数の **weak の仮の実体** (gfx/v2/a0_weak.c・gfx/sync/a0_weak.c・drivers/video/geforce/gf_v2_weak.c、全部 `return OS32_ERR_NOSYS;` か 0)、大きさと offset の検査、module_schema・hwdev_layout・build の登録、include/config.h の `SYS_GFXSYNC_CFG` (§10)。A0 の後は各段が単独で `make kernel` を通せる。**A0 以外の段はヘッダを編集しない** (足りなければ止めて PM へ)。
- 仮の実体の掃除 (P2-2): weak の仮の実体は本来の関数名 (`gfx2_surf_new` 等) で置くので、記号名では見分けない。A0 が `tools/checks/gfx2_wiring.txt` (行 = `I1 kernel gfx2_surf_new` の形、I ごとの対象関数を §11-2〜11-7 の表から全部) と `tools/checks/gfx2_no_weak.sh <I>` を作る。検査は最終の build/out/kernel.elf (build/config.mk:190 の `-Map=$(BUILD_OUT)/kernel.map` と同じ組み) と geforce module の ELF に対し、対象の各記号が (a) `nm` の型が `T`/`t` (`W`・`V`・`U` は不合格)、(b) 番地が a0_weak.o の .text の区間 (build/out/kernel.map の a0_weak.o の行から) の外、の両方。各 I の合格は「割り当てた §13 の試験の合格の記録 + `gfx2_no_weak.sh <I>` の 0」の両方 (§12)。

### 11-1. 公開 (sdk/include/os32/os32_gfx2.h、C89、A0)
```c
#define GFX2_MAX_CMDS 64
#define GFX2_OP_FILL 1  #define GFX2_OP_COPY 2  #define GFX2_OP_STRETCH 3  #define GFX2_OP_MONO 4
#define GFX2_OP_HOSTDATA 5  #define GFX2_OP_READBACK 6  #define GFX2_OP_ROP4 7  #define GFX2_OP_ALPHA 8
#define GFX2_OP_LINE 9  #define GFX2_OP_SET_PATTERN 10  #define GFX2_OP_MAX 10
#define GFX2_HOW_NOSUPPORT 0  #define GFX2_HOW_SW 1  #define GFX2_HOW_HW 2
#define GFX2_CAP2D(op) (1UL << (op))   /* caps2d_hw・caps2d_sw・exact の bit (§4-1) */
#define GFX2_AF_ACTIVE 1  #define GFX2_AF_ONBOARD 2   /* AdapterInfo.flags */
#define GFX2_F_SRCKEY 0x01  #define GFX2_F_DSTKEY 0x02  #define GFX2_F_CLIP 0x04
#define GFX2_F_FILTER 0x08  #define GFX2_F_ALPHA_PIXEL 0x10  #define GFX2_F_PAT 0x20  #define GFX2_F_PAT_COLOR 0x40
#define GFX2_CX_TRANSPARENT 1
#define GFX2_POOL_VRAM 1  #define GFX2_POOL_GART 2  #define GFX2_POOL_SYSMEM 3
#define GFX2_USE_RENDER2D 0x01  #define GFX2_USE_SCANOUT 0x02  #define GFX2_USE_CURSOR 0x04
#define GFX2_USE_STAGING 0x08   #define GFX2_USE_TEXTURE 0x10  #define GFX2_USE_V1 0x80
#define GFX2_TILE_LINEAR 0  #define GFX2_TILE_TILED 1
#define GFX2_ST_FREE 0 #define GFX2_ST_IDLE 1 #define GFX2_ST_LOCKED_RO 2 #define GFX2_ST_LOCKED_RW 3
#define GFX2_ST_CLOSING 4 #define GFX2_ST_LOST 5 #define GFX2_ST_QUAR 6
#define GFX2_LOCK_READONLY 1  #define GFX2_LOCK_DISCARD 2  #define GFX2_LOCK_DONOTWAIT 4
#define GFX2_FLIP_VBLANK 1    #define GFX2_ROLE_DISPLAY 1  #define GFX2_ROLE_CLIENT 2
#define GFX2_DPMS_ON 0 #define GFX2_DPMS_STANDBY 1 #define GFX2_DPMS_SUSPEND 2 #define GFX2_DPMS_OFF 3
/* エラーは今の OS32_ERR_*: INVAL・NOSYS・STALE・BUSY・FULL・TIMEOUT・IO・NOSPC(VRAM/GART の不足) */
int gfx2_adapter_count(void);
int gfx2_adapter_info(u32 a, GFX2_AdapterInfo *o);
int gfx2_adapter_gen(u32 a, u32 *gen);                 /* submit に渡す世代。喪失・再初期化で +1 */
int gfx2_caps(u32 a, GFX2_Caps *o, u32 size);
int gfx2_check_op(u32 a, const GFX2_Op *shape, u32 sfmt, u32 dfmt, u32 spool, u32 dpool, u32 *how);
int gfx2_surface_create(u32 a, u16 w, u16 h, u8 fmt, u8 pool, u8 usage, u32 *sid);
int gfx2_surface_free(u32 sid);
int gfx2_surface_desc(u32 sid, GFX2_SurfaceDesc *o);
int gfx2_screen_surface(u32 a, u32 head, u32 role, u32 *sid);
int gfx2_lock(u32 sid, u32 flags, void **va, u32 *pitch);
int gfx2_unlock(u32 sid);
int gfx2_submit(u32 a, const GFX2_Op *ops, u32 n, u32 gen, u32 *done, u32 *fence);
int gfx2_fence_wait(u32 a, u32 fence, u32 deadline_ms);
int gfx2_fence_query(u32 a, u32 *completed, u32 *failed_upto);
int gfx2_head_info(u32 a, u32 head, GFX2_HeadInfo *o);
int gfx2_connector_info(u32 a, u32 c, GFX2_ConnectorInfo *o);
int gfx2_connector_edid(u32 a, u32 c, u32 block, u8 *out128);
int gfx2_mode_count(u32 a, u32 head, u32 fmt);
int gfx2_mode_get(u32 a, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *o);
int gfx2_mode_set(u32 a, u32 head, u32 conn_mask, u32 mode_id, u32 sid);
int gfx2_flip(u32 a, u32 head, u32 sid, u32 flags, u32 deadline_ms);
int gfx2_vblank_wait(u32 a, u32 head, u32 after, u32 deadline_ms, u32 *seen);
int gfx2_palette(u32 a, u32 first, u32 count, const u32 *xrgb);
int gfx2_gamma(u32 a, u32 head, const u16 *rgb768);     /* [3][256] を平たく */
int gfx2_cursor_image(u32 a, u32 head, u32 sid);
int gfx2_cursor_move(u32 a, u32 head, i32 x, i32 y);
int gfx2_cursor_show(u32 a, u32 head, u32 show);
int gfx2_dpms(u32 a, u32 head, u32 state);
int gfx2_present(u32 a, u32 head, i16 x, i16 y, u16 w, u16 h);
typedef char gfx2_chk_op[sizeof(GFX2_Op) == 80 ? 1 : -1];   /* 以下同形: Color 8・Caps 256・SurfaceDesc 32・
   AdapterInfo 128・HeadInfo 64・ConnectorInfo 64・ModeInfo 40。offset は各欄の表 (§4-1・§5-1・§9-1) を
   tools/tests/gfx2_abi_host.c が offsetof で全部比べる */
```

### 11-2. 中核の共有の型 (gfx/v2/gfx2_int.h、A0) と欄の所有
```c
struct gfx2_rect { i32 x, y, w, h; };
struct gfx2_head { u32 scan_sid, pend_sid, mode_id, conn_mask; };
struct gfx2_heap { u32 base, size, n; struct { u32 off, bytes, used; } blk[GFX2_HEAP_BLOCKS /* 2048 */]; };
struct gfx2_fence_st { u32 issued, reserved, completed, failed_upto, last_sweep; };  /* reserved: 予約済みで commit 前 (0 = 無し) */
#define GFX2_AD_OK 0  #define GFX2_AD_LOST 1  #define GFX2_AD_LOST_HELD 2
struct gfx2_caller { int app_id; struct addrspace *as; u32 owner; };   /* C1 の gfx2_caller_get (§6-5) */
struct gfx2_adapter {
    u32 index, gen, state;               /* C1 (登録)・C3 (喪失で gen+1・state = GFX2_AD_*)・C1 (ACTIVE を外す時に gen+1) */
    u32 flags;                           /* C1 だけ (GFX2_AF_ACTIVE・ONBOARD。ACTIVE は全部の adapter で 1 つ) */
    u32 vram_phys;                       /* C1 (be->heap_range の phys。VRAM の面の PTE の元) */
    GfxBackend *v1;                      /* C1。hook は呼ぶたびに a->v1-> から読む (H4 が書き換える、D2) */
    const struct GfxBackendV2 *be; void *ctx;   /* C1 (be は kernel の中の表、常に非 NULL: v2 の表・C16 の shim・C16 の lost の表のどれか、§11-7) */
    u32 kind;                            /* C1。GFX2_KIND_PC98 1・PEGC 2・CIRRUS 3・GEFORCE 4・TRIDENT 5 (LEDGER_SF_* と同じ値、
                                            display_provider_ledger(v1) の戻り値をそのまま入れる) */
    GFX2_Caps caps;                      /* C10 */
    struct gfx2_fence_st fence;          /* C3 だけが書く (reserved は reserve、issued は commit) */
    struct gfx2_heap heap;               /* C5 だけが書く */
    u32 heads; struct gfx2_head head[2]; /* C12・C13 だけが書く */
    u32 restore_on_leave;                /* C12 */
};   /* 同期層の ctx は持たない。gfx2_sync_get で毎回引く (P1-2) */
struct gfx2_alias_desc {                 /* C14 が v1 の今の値から作り、C4 に渡す */
    u32 adapter;                         /* ACTIVE の adapter (gfx2_active() の index) */
    u16 w, h; u8 fmt, pool, head, role; u32 pitch, bytes, gpu_off;
    u8 *kva;                             /* 借りる。C4 は解放しない */
};
struct gfx2_surf {
    u32 sid, gen, adapter, owner, state; /* C4 だけが書く (state は C7 の lock/unlock も gfx2_surf_state_set 経由で) */
    u16 w, h; u8 fmt, pool, usage, tiling; u32 pitch, bytes, gpu_off;  /* C4 (作成の時だけ) */
    u32 last_write, last_read;           /* C3 だけ (gfx2_fence_note) */
    u16 scan_ref, flags;                 /* C4 (scan_ref_get/put)。flags: GFX2_SF_ALIAS 1 (C4)・GFX2_SF_KEEP 2 (C4・C15) */
    u8 head, role;                       /* C4 (別名だけ。同期層の ctx を引く head) */
    struct gfx2_rect dirty;              /* C4 (gfx2_dirty_add)、C13 の present が読んで 0 に */
    struct addrspace *map_as; u32 map_va, map_npages; /* C7 だけ */
    u8 *staging; u32 staging_pitch;      /* C7 (DIRECT の別名の lock の写し、§7-5) */
    u32 staging_pfn, staging_n, staging_owner; /* C7 (staging の連続の頁と持ち主、§7-5) */
    u8 *kva;                             /* C4 (作成の時に be->kva)・C18 (SYSMEM の P2V)。GART の面は NULL (§7-2) */
    u32 heap_off;                        /* C5 (gfx2_heap_alloc の結果、C4 が格納) */
    u32 gart_first, gart_n, gart_moved; /* C6 だけ (gart_moved は隔離へ移せた頁数) */
    u32 sys_pfn, sys_n;                  /* C18 だけ (SYSMEM の連続の頁、§7-2) */
    u32 priv[4];                         /* backend の私物 (GF: 予約の id・世代・node の番号、G9) */
};
struct gfx2_vaspace { u32 as_gen; u32 bits[(MEM_GFX2_MAP_END - MEM_GFX2_MAP_BASE) / 4096 / 32]; }; /* C7、4 本。帯の定数は include/memmap.h (A0、§11-4) */
extern struct gfx2_adapter gfx2_adapters[GFX2_MAX_ADAPTERS /* 4 */];  /* 実体は C1 */
extern u32 gfx2_pal[256];                                             /* 実体は C13 (§9-2 のパレットの写し) */
/* GFX2_KIND_* は mm/pgalloc.h:297-301 の LEDGER_SF_* と同じ値。A0 が gfx2_int.h に置き、
   _Static_assert で 5 つとも等しいことを確かめる */
extern struct gfx2_surf gfx2_surfs[GFX2_MAX_SURFACES /* 1024 */];     /* 実体は C4 */
```
| 関数 (全部 gfx2_int.h に宣言) | 戻り値 | 実装の段 | 呼ぶ段 |
|---|---|---|---|
| `struct gfx2_adapter *gfx2_adapter_get(u32 a)` | NULL = 無い | C1 | 全部の公開の入口 |
| `int gfx2_adapter_register(GfxBackend *v1, u32 kind, u32 flags)` | 0/FULL/INVAL (abi 不一致・hook が NULL・module の表の hook が module の .text の外・生きた adapter の表の差し替え)。冪等 (同じ v1 は同じ adapter)。隔離の owner が取れなくても 0 (§8 の「隔離の owner が取れない時」) | C1 (§11-7 のコード) | gfx_core (C1 が呼ぶ行を足す、§11-7) |
| `void gfx2_adapter_activate(struct gfx2_adapter *)`・`struct gfx2_adapter *gfx2_active(void)` | — / ACTIVE の adapter (起動の後は常に非 NULL) | C1 (§11-7 のコード) | C1 (register)・C14 |
| `void gfx2_adapter_unregister(GfxBackend *v1)` | — (adapter を LOST・held=0 に、`be = &gfx2_lost_be`・`ctx = a`) | C1 | geforce_module.c・trident_module.c の停止の道 (G1・T1 が 1 行) |
| `int gfx2_caller_get(struct gfx2_caller *)` | 0/INVAL (USER の呼び手でない) | C1 | 全部の公開の入口 (§6-5) |
| `extern const GfxBackendV2 gfx2_v1_shim, gfx2_lost_be` | — | C16 (§11-7 のコード) | C1 |
| `int gfx2_caps_from_v1(struct gfx2_adapter *, GFX2_Caps *)` | 0 | C10 (§4-1 の表の shim の列) | C16 (shim の caps) |
| `void gfx2_caps_refresh(struct gfx2_adapter *a)` | — (失敗の無い計算だけ。`a->be->caps(a->ctx, &tmp)` が 0 なら §4-1 の caps を `a->caps` に入れ直す。0 以外なら `a->caps` を変えない) | C10 (`adapter.caps` を書くのはここだけ) | C1 (`gfx2_adapter_register` の表の付け替え、§11-7) |
| `int gfx2_v1_enum_head(const GfxBackend *v1, u32 head, GFX2_HeadInfo *o)`・`int gfx2_v1_enum_conn(const GfxBackend *v1, u32 conn, GFX2_ConnectorInfo *o)`・`int gfx2_v1_enum_mode(const GfxBackend *v1, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *o)` | `be->enum_*` と同じ OS32_ERR_* (v1 の hook から作る列挙) | C11 | C16 (`shim_enum_*` がそのまま呼ぶ) |
| `int gfx2_sync_publish(u32 kind, u32 head, GfxSyncCtx *ctx)` | 0/INVAL (kind・head の範囲外) | C1 (static `GfxSyncCtx *sync_tab[GFX2_KIND_MAX + 1][2]` を書くのはここだけ) | P1 (直接)・T1 (module の import)。bind の後・unbind の**前** (NULL を publish してから unbind) |
| `int gfx2_sync_get(const struct gfx2_adapter *, u32 head, GfxSyncCtx **ctx, u32 *mode)` | 0 (mode = `gfxsync_mode(ctx)`) / OS32_ERR_NOTFOUND (同期層でない、*ctx = NULL) | C1 | C7・C8・C9・C13 (§6-4 の判定の唯一の口) |
| `struct gfx2_surf *gfx2_surf_get(u32 sid, u32 caller_owner, int *rc)` | NULL + rc (INVAL/STALE) | C4 | C7・C9・C12・C13・C14 |
| `int gfx2_surf_new(struct gfx2_adapter *, u16 w, u16 h, u8 fmt, u8 pool, u8 usage, u32 owner, struct gfx2_surf **out)` | 0/INVAL/NOSPC | C4 (heap は C5、GART は C6、予約は `be->alloc`) | 公開 create だけ |
| `int gfx2_surf_alias(struct gfx2_adapter *, const struct gfx2_alias_desc *, struct gfx2_surf **out)` | 0/FULL/INVAL | C4: 表の空きに `owner = GFX2_OWNER_SCREEN`・`usage = SCANOUT\|V1`・`flags = ALIAS`、heap・GART・`be->alloc` は呼ばない | C14 (起動時) |
| `int gfx2_surf_alias_update(struct gfx2_surf *, const struct gfx2_alias_desc * /* NULL = 形無し */)` | 0/BUSY (state が IDLE・LOST でない・`last_write`/`last_read` が未完了 (失敗の完了は完了に数える))/INVAL (ALIAS でない) | C4: 失敗時は何も変えない。成功時は gen+1 (旧 sid は STALE) → adapter・形状・pitch・gpu_off・kva を写し、`last_*` を 0、LOST は IDLE に (借りる backing が替わるため)。backing は解放も確保もしない | C14 (`v1_bridge_rebind`) |
| `int gfx2_surf_destroy(struct gfx2_surf *)` | 0/BUSY/INVAL (ALIAS) | C4。順と失敗は §7-3 の表だけ: `gfx2_unmap` → `be->release` → C5 (VRAM)/C6 (GART、`gfx2_gart_free`)/C18 (SYSMEM) → 表を空き (gen+1)。どこかの失敗は KEEP を立てて BUSY、backing は返さない | 公開 free・C15・見回り |
| `int gfx2_surf_state_set(struct gfx2_surf *, u32 st)` | 0/INVAL (§7-1 の表に無い遷移) | C4 | C3・C7・C15 |
| `void gfx2_scan_ref_get/put(struct gfx2_surf *)`・`void gfx2_scan_ref_swap(struct gfx2_adapter *, u32 head, struct gfx2_surf *)` | — | C4 | C12・C13 |
| `void gfx2_dirty_add(struct gfx2_surf *, const struct gfx2_rect *)` | — | C4 | C3 (cpu_end)・C9 |
| `void gfx2_closing_sweep(struct gfx2_adapter *)` | — | C4 | C3・C9・C13 (vblank) |
| `u32 gfx2_fence_reserve(struct gfx2_adapter *)` | 次の番号 (0 を飛ばす)、`fence.reserved` に置くだけ。commit 前にもう一度呼べば同じ値。0 = adapter が LOST | C3 | C9 |
| `int gfx2_fence_commit(struct gfx2_adapter *, u32 f, struct gfx2_surf *dst, struct gfx2_surf *src)` | 0 / 装置の失敗の rc | C3 (下のコード) | C9 (`be->emit` が 0 を返した後だけ) |
| `void gfx2_fence_commit_fail(struct gfx2_adapter *, u32 f, struct gfx2_surf *dst, struct gfx2_surf *src, int rc)` | — (下のコード) | C3 | C9 (`be->emit` が装置の失敗を返した時)・C3 (commit の中) |
| `void gfx2_fence_abandon(struct gfx2_adapter *, u32 f)` | — (`reserved = 0`、番号は次の reserve で再使用) | C3 | C9 (`be->emit` が検査の失敗 (INVAL・NOSYS) を返した時) |
| `int gfx2_fence_done(struct gfx2_adapter *, u32 f)` | 1/0 | C3 | C4・C9 |
| `int gfx2_fence_wait_k(struct gfx2_adapter *, u32 f, u32 dl_us)` | 0/TIMEOUT/STALE (失敗の完了) | C3 | C7・C12・C15 |
| (`gfx2_fence_note` は C3 の static。commit の中だけで呼ぶ、D9 で C14 は呼ばない) | | | |
| `int gfx2_cpu_begin(struct gfx2_surf *, u32 access, u32 dl_us)`・`void gfx2_cpu_end(struct gfx2_surf *, u32 access, const struct gfx2_rect *)` | 0/TIMEOUT/STALE (失敗では何も変えない)。cpu_end の吐き出しは §6-2 の表 | C3 | C7・C9 (sw)・C13・C14 |
| `int gfx2_surf_quiesce(struct gfx2_surf *, u32 dl_us)` | 0/TIMEOUT/STALE、何も変えない | C3 | C12・C15 |
| `void gfx2_adapter_lost(struct gfx2_adapter *, int held)`・`void gfx2_adapter_recovered(struct gfx2_adapter *)` | — | C3 | C9・G4 経由の rc |
| `int gfx2_heap_init(struct gfx2_heap *, u32 base, u32 size)`・`int gfx2_heap_alloc(struct gfx2_heap *, u32 bytes, u32 align, u32 *off)`・`int gfx2_heap_free(struct gfx2_heap *, u32 off)` | 0/NOSPC/INVAL。契約は §7-6 | C5 | C1 (init)・C4 |
| `int gfx2_gart_alloc(struct gfx2_adapter *, struct gfx2_surf *, u32 owner)`・`int gfx2_gart_free(struct gfx2_adapter *, struct gfx2_surf *)`・`int gfx2_gart_pfns(const struct gfx2_surf *, const u32 **pfn, u32 *n)`・`int gfx2_gart_quarantine(struct gfx2_surf *)` | alloc: 0/INVAL/NOSPC/IO、free: 0/BUSY (LOST_HELD)/IO (unbind の失敗)、pfns: 0/INVAL (GART の面でない)、quarantine: 0/IO。順と失敗の後の状態は §8 | C6 (pin は M1、`be->gart_bind`) | C4・C7・C15 |
| `int gfx2_map(struct gfx2_surf *, struct addrspace *, u32 *va)`・`int gfx2_unmap(struct gfx2_surf *)` | map: 0/INVAL (`paging_gfx2_map` の −1)/NOSPC (VA の bitmap か vaspace の 4 枠の不足、`paging_gfx2_map` の −2)、失敗では PTE・VA の bit・map_* を変えない。unmap: 0/IO、IO は map_* を変えない | C7 (`paging_gfx2_map`・`paging_lease_unmap`) | C7 (lock)・C4・C15 |
| `int gfx2_surf_page(const struct gfx2_surf *, u32 k, u32 *pa, u32 *attr)`・`int gfx2_cpu_visible(const struct gfx2_surf *)` | 0/INVAL (k 頁目の物理番地と PTE の cache の属性: VRAM は `vram_phys + gpu_off` から UC/WC、GART は pfn の表、SYSMEM は `sys_pfn` から WB、staging がある間は `staging_pfn` から WB (§7-5)) / 1/0 (`kva` か `map_as` が今の AS) | C7 | C7 (map)・C9・C17 |
| `int gfx2_map_audit(const struct addrspace *)` | 0/-1 (§7-4 のコード) | C17 | exec/lease.c の `lease_audit_all` (M3 が 1 行) |
| `int gfx2_sysmem_alloc(struct gfx2_surf *, u32 owner)`・`int gfx2_sysmem_free(struct gfx2_surf *)` | 0/NOSPC/IO (§7-2 のコード) | C18 | C4 |
| `void gfx2_head_detach(struct gfx2_adapter *, u32 head)` | — (`scan_sid`・`pend_sid` の scan_ref を外す。装置には触らない) | C13 | C1 (ACTIVE を外す時)・gfx_core の `gfx_shutdown` (C1 が 1 行) |
| `void gfx2_map_detach(struct gfx2_surf *)` | — (map_as = NULL・map_va = map_npages = 0。PTE と VA の bit には触らない) | C7 | C15 |
| `void gfx2_staging_drop(struct gfx2_surf *)`・`void gfx2_vaspace_drop(struct addrspace *)` | — (staging の頁を `staging_owner` に返す、KEEP なら欄だけ 0 (§7-5) / as の枠の as_gen を 0、bit を全部 0) | C7 | C7 (unlock)・C15 |
| `int gfx2_lock_revoke(struct gfx2_surf *)` | 0/IO/INVAL (ALIAS でない)。戻った時は map_as・staging が無く、LOCKED は IDLE (§6-5 のコード) | C7 (C14 の段が lock.c に足す) | C14 (`v1_bridge_rebind`) |
| `int gfx2_head_restore_v1(struct gfx2_adapter *, u32 head)` | 0/IO | C13 | C15・C12 (全画面を抜ける) |
| `void gfx2_pal_set(u32 first, u32 count, const u32 *xrgb)`・`void gfx2_pal_set_v1(int first, int count, const u8 *rgb)`・`void gfx2_pal_reset16(void)` | — (§9-2 のパレットの写し。v1 は 0〜15 の成分を ×17) | C13 | C13 (`gfx2_palette`)・gfx_core_api.inc の `gfx_set_palette_hal`・`gfx_lease_palette` (C14 が呼ぶ行)・gfx_core_init.inc:4 の後 (C1 が呼ぶ行) |
| `int gfx2_surf_quarantine(struct gfx2_surf *)`・`void gfx2_quarantine_drain(struct gfx2_adapter *)` | 0/IO (§7-3 のコード。IO でも owner・state は隔離に済む) | C15 | C15・C3 (回復)。drain は KEEP の面を飛ばす |
| `int v1_bridge_begin(u32 role, u32 access)`・`void v1_bridge_end(u32 role, u32 access, u32 fence)`・`void v1_bridge_rebind(void)` | 0/BUSY/TIMEOUT | C14 | gfx_core・gfx_hw_submit・surface_query (C14 が呼ぶ行を足す) |

fence の 3 段 (P1-1。C3 が実装、C9 の `run_one` の HW の道がこの順でだけ呼ぶ):
```c
/* gfx/v2/submit.c (C9) の run_one の HW の道 */
u32 f = gfx2_fence_reserve(a);                  /* 番号だけ。0 = adapter が LOST */
if (!f) return OS32_ERR_STALE;
rc = a->be->emit(a->ctx, ex, f);                /* 描画の命令だけ積む。印 (fence) は積まない */
if (rc == OS32_ERR_INVAL || rc == OS32_ERR_NOSYS) {   /* 検査の食い違い: 何も積んでいない決まり */
    gfx2_fence_abandon(a, f); return rc;
}
if (rc) { gfx2_fence_commit_fail(a, f, ex->dst, ex->src, rc); return rc; }  /* 途中まで積んだかもしれない */
return gfx2_fence_commit(a, f, ex->dst, ex->src);     /* 0 の時だけ k_done に数える */

/* gfx/v2/fence.c (C3) */
int gfx2_fence_commit(struct gfx2_adapter *a, u32 f, struct gfx2_surf *dst, struct gfx2_surf *src)
{
    int rc = a->be->fence_emit(a->ctx, f, GFX2_EMIT_DL);   /* 描画の後ろに印。GF は §6-3 の輪に記録 */
    gfx2_fence_note(dst, src, f);                          /* 成否に関わらず: 書き先は f を待つ / LOST の対象になる */
    a->fence.reserved = 0;
    if (rc) { gfx2_fence_commit_fail(a, f, NULL, NULL, rc); return rc; }
    a->fence.issued = f;
    return 0;
}
void gfx2_fence_commit_fail(struct gfx2_adapter *a, u32 f, struct gfx2_surf *dst, struct gfx2_surf *src, int rc)
{
    if (dst || src) gfx2_fence_note(dst, src, f);
    a->fence.reserved = 0;
    a->fence.failed_upto = f;           /* 予約の番号まで失敗の完了 → note 済みの書き先は LOST */
    gfx2_adapter_lost(a, 0);            /* gen+1・LOST の掃き出し・be->stop (stop の失敗で中が LOST_HELD に) */
}
```
- 束の終わり: C9 は HW で行った命令が 1 つ以上なら `be->kick`、`*fence = a->fence.issued`。sw の命令は `gfx2_cpu_begin` が前の HW の命令の commit 済みの番号を待つので、混在の束 (hw・sw・hw) で CPU が描画の前に触ることは無い。

### 11-3. 形式と sw (gfx/v2/gfx2_sw.h、A0)
```c
struct gfx2_fmt_info { u8 bpp, cpp, has_alpha, indexed; u8 rs, rb, gs, gb, bs, bb, as, ab; }; /* 位置・幅 */
int  gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *o);      /* C2。0/INVAL */
u32  gfx2_px_read(const u8 *p, u32 fmt);  void gfx2_px_write(u8 *p, u32 fmt, u32 v);   /* C2 */
u32  gfx2_to_argb(u32 fmt, u32 v, const u32 *pal256);       /* C2。§3-3 の拡げ */
int  gfx2_from_argb(u32 fmt, u32 argb, u32 *v);            /* C2。INDEX8 は NOSYS */
u32  gfx2_yuv_argb(u8 y, u8 u, u8 v);  u32 gfx2_div255(u32 t);                       /* C2 */
struct gfx2_pattern { u32 kind; /* 0=無し 1=mono 2=色 */ u8 mono[8]; u32 color[64]; GFX2_Color fg, bg; };
struct gfx2_view {                    /* sw が見る面。C9 が作る */
    u8 *base; u32 pitch; u16 w, h; u8 fmt;
    const u32 *pal;                   /* INDEX8 → ARGB。C9 が gfx2_pal (§9-2) を指す */
    GfxSyncCtx *sync;                 /* 非 NULL: 行の書き・読みは gfxsync_*、base は使わない */
};
struct gfx2_exec {                    /* 走査 1 が作り、走査 2・check・emit が同じ物を見る */
    const GFX2_Op *op; struct gfx2_surf *dst, *src; struct gfx2_view dv, sv;
    struct gfx2_rect clip;            /* クリップ後の書き先 (面と CLIP の交わり) */
    const struct gfx2_pattern *pat;   /* submit.c の束の状態 */
    const u8 *cpu; u32 cpu_pitch;     /* MONO・HOSTDATA・mask の元 (ユーザーの VA、走査 1 で範囲を確かめ済み) */
    u32 how;                          /* GFX2_HOW_* */
};
typedef int (*gfx2_sw_fn)(const struct gfx2_exec *, struct gfx2_rect *touched);  /* 0/INVAL。touched は書いた外接 */
int gfx2_sw_fill(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S1 */
int gfx2_sw_copy(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S2 */
int gfx2_sw_rop(const struct gfx2_exec *, struct gfx2_rect *touched);       /* S3 */
int gfx2_sw_rop4(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S4 */
int gfx2_sw_mono(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S5 */
int gfx2_sw_hostdata(const struct gfx2_exec *, struct gfx2_rect *touched);  /* S6 */
int gfx2_sw_readback(const struct gfx2_exec *, u8 *kbuf);                   /* S7 (表に載せず C9 が直に呼ぶ) */
int gfx2_sw_stretch(const struct gfx2_exec *, struct gfx2_rect *touched);   /* S8 */
int gfx2_sw_ckey(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S9 */
int gfx2_sw_alpha(const struct gfx2_exec *, struct gfx2_rect *touched);     /* S10 */
int gfx2_sw_line(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S11 */
extern const gfx2_sw_fn gfx2_sw_table[GFX2_OP_MAX + 1];   /* 実体は gfx/v2/sw_table.c (I1 が置いた。C9 は読むだけ)。
                                                           表に無い振り分け (sw_rop・sw_ckey・sw_readback) は C9 の run_one (確定の後の追記 2026-11-11 I2) */
```
- 呼び方: C9 の `run_one` は how == SW なら `gfx2_cpu_begin(dst, W)`(と src の R) → `gfx2_sw_table[op](ex, &t)` → `gfx2_cpu_end(dst, W, &t)` (SHADOW の面は dirty、VRAM は吐き出し)。HW なら §11-2 の reserve → emit → commit。`ex->dv.sync`・`sv.sync` は C9 が埋める: 別名面 (`flags & GFX2_SF_ALIAS`) で `gfx2_sync_get(a, s->head, &ctx, &mode)` が 0 かつ `mode == GFXSYNC_DIRECT` の時だけ ctx、ほかは NULL。SHADOW の別名面は `view.base` = RAM の正本・`view.sync = NULL` (転送は C13 の present)。通常の面は常に NULL。
- C2 の format.c はカーネルと libos32gfx (U1) の両方で同じファイルを組む (libc を使わない)。

### 11-4. mm と exec (A0 が宣言、実装は M1〜M3・X1)
```c
/* mm/pgalloc.h (M1)。全部 irq_save の中、全件か無し */
int pgalloc_pin(u32 pfn, u32 n, u32 owner);     /* 1 = 成功。owner の頁でない・pin が 255 を越えるなら 0 */
int pgalloc_unpin(u32 pfn, u32 n, u32 owner);   /* 1/0 (pin が 0 の頁を含むなら 0) */
int pgalloc_pinned(u32 pfn);                    /* pin の数 */
int ledger_transfer_pinned(u32 pfn, int n, u32 from, u32 to); /* 1/0。pin は保つ。pin の頁を動かす唯一の口 */
/* mm/paging.h (M2) */
int paging_gfx2_map(struct addrspace *as, u32 va, u32 pa, u32 npages, u32 pte_flags);
    /* 0/-1。va は MEM_GFX2_MAP の帯の中で頁境界、PT が無ければ as->owner で取る。-1 の時は PTE を
       1 つも書いていない (取った PT は空のまま残り、paging_lease_unmap か AS の破棄で返る) */
/* include/memmap.h (A0)。MEM_LEASE_MAX の後に。値を書くのはここだけ ([C4])、gfx2_int.h の gfx2_vaspace が使う */
#define MEM_GFX2_MAP_BASE 0xF8000000UL   /* lease の帯の中、PDE 境界 */
#define MEM_GFX2_MAP_END  0xFE000000UL   /* = MEM_LEASE_END の値。96MB */
/* include/memmap.h (M3): MEM_LEASE_ALLOC_END MEM_GFX2_MAP_BASE (と §7-4 の MEM_LEASE_ALLOC_PDES)。
   exec/lease.c:69 を MEM_LEASE_ALLOC_END にするのも M3 */
/* gfx/v2/gfx2_exec.h (C15 が実装、X1 が exec_finish.inc:34 の後に呼ぶ) */
int gfx2_owner_release(struct addrspace *as);   /* 0 = 切り離し済み、OS32_ERR_IO = poison せよ */
```

### 11-5. 同期層の内部 (gfx/sync/sync_int.h、A0。include/gfx_sync.h も A0 だけが書く)
```c
struct gfxsync_cfg { u8 mode, unit, method, flush, read; u32 gap_ns, read_ns; };
struct GfxSyncCtx { GfxSyncTarget t; struct gfxsync_cfg cfg; u32 gap_loops; u8 used; };
int gfxsync_cfg_lookup(const char *key, struct gfxsync_cfg *out); /* Y1。0 = 区画を見つけた、NOTFOUND = 既定を入れた */
int gfxsync_cfg_check(const struct gfxsync_cfg *, const GfxSyncTarget *); /* Y4。0 / INVAL (§10 の表) */
u32 gfxsync_gap_loops(u32 loops_per_tick, u32 ns);                         /* Y2。write_gap.c の算数 */
int gfxsync_row_write(GfxSyncCtx *, u32 off, const u8 *src, u32 bytes);    /* Y3 */
int gfxsync_row_read(GfxSyncCtx *, u32 off, u8 *dst, u32 bytes);           /* Y3 */
extern GfxSyncCtx gfxsync_ctx_pool[4];                                     /* 実体は Y4 (sync_bind.c) */
```
- 公開の `gfxsync_bind`・`write_rows`・`read_rows`・`flush`・`unbind` の実体は Y4 (bind・unbind) と Y3 (rows・flush)。

### 11-6. GeForce (drivers/video/geforce/gf_v2.h と hwdev/gf/accel.h の追記、A0)
```c
#define GF_V2_RING 256
struct gf_v2_ring_e { u32 v2; struct gf2_fence f; };
struct gf_v2_ring { u32 head, tail, count; struct gf_v2_ring_e e[GF_V2_RING]; };
#define GF_V2_GART_PAGES 32768u                  /* nv04 の頁表の PTE の数 (vmmnv04.c:75 の bits 15)、128MB */
#define GF_V2_GART_PT_BYTES (8u + 4u * GF_V2_GART_PAGES) /* 見出し 8 + PTE (vmm.c:1158)、0x20008 */
#define GF_V2_GART_BINDS GFX2_MAX_SURFACES      /* bind の記録の枠 (GART の面 1 つに 1 件) */
struct gf_v2_gart_bind { u32 first, n; const u32 *pfn; }; /* first は頁の番号。pfn は C6 の表を unbind まで借りる */
struct gf_v2_gart {
    struct gf2_object obj;                       /* GART の ctxdma = 頁表の物 (obj.inst が頁表)。動かさない */
    u32 epoch;                                   /* obj を作った F2 の epoch。0 = 無い */
    u32 count;                                   /* bind[] の件数、first の昇順 */
    struct gf_v2_gart_bind bind[GF_V2_GART_BINDS];
};
struct gf_v2_head { u32 scan_sid, pend_sid, retired_sid; };   /* G2 だけが書く */
struct gf_v2_home { u32 valid, conn, mode_id; struct gf3_surface front; u8 pal[768]; }; /* G2 だけが書く。v2 が head 0 を取る前の v1 の走査 */
struct gf_v2 {                                   /* 実体は G1 (gf_v2_ops.c の gf_v2_inst、下の extern) */
    struct gf3_ctx *g3; struct gf2_chan *ch;     /* G11 (gf_v2_start が層 4 の ctx から結ぶ) */
    struct gf_v2_ring ring;                      /* G4 だけが書く */
    u32 completed, failed_upto;                  /* G4 */
    struct gf_vram_reservation_node node[GFX2_MAX_SURFACES]; /* G9 (surf の priv[2] が番号) */
    struct gf_v2_gart gart;                      /* G5 だけが書く */
    u32 epoch, lost;                             /* G11 だけが書く (結んだ channel の epoch、lost = 1 の間 v2 の HW を使わない) */
    struct gf_v2_head head[2];                   /* G2 */
    struct gf_v2_home home;                      /* G2 */
    u32 pal_set; u8 pal[768];                    /* G2 (gf_v2_palette の写し、RGB 各 8 bit) */
};
extern struct gf_v2 gf_v2_inst;                  /* G1。backend_geforce.c の direct_ok と G2 の gf_v2_owns が読む */
struct gf4_test_ops; struct gf4_ctx; struct gf4_route;
int gf_v2_start(struct gf_v2 *, u32 dl);         /* G11。0 / GF2・GF3 の rc (呼び手が gf_v2_rc で写す) */
int gf_v2_stop(void *ctx);                       /* G11。GfxBackendV2.stop。0 / OS32_ERR_IO (中核が LOST_HELD に) */
const struct gf4_test_ops *gf_v2_disp(void);     /* G11。層 4 の窓口、閉じていれば NULL */
int gf_v2_deadline(struct gf2_chan *, u32 us, u32 *dl);  /* G11。0 / GF2_EIO・時計の rc */
int gf_v2_rc(int gf_rc);                         /* G11。GF2・GF3・GF4 の rc → OS32_ERR_* */
void gf_v2_fence_reset(struct gf_v2 *);          /* G4 の追補 (G11 の段が gf_v2_fence.c に足す) */
u32 gf_v2_route_index(const struct gf4_ctx *, const struct gf4_route *);  /* G10。無ければ nroutes */
int gf_v2_owns(const struct gf3_surface *);      /* G2。G9 の node の予約なら 1 */
int gf_v2_fence_emit(struct gf_v2 *, u32 v2, u32 dl);            /* G4、§6-3 のコード */
int gf_v2_fence_state(struct gf_v2 *, u32 *completed, u32 *failed_upto); /* G4 */
int gf_v2_fence_wait(struct gf_v2 *, u32 v2, u32 dl);            /* G4 */
int gf_v2_alloc(struct gf_v2 *, struct gfx2_surf *);             /* G9: gf_vram_reserve、priv[0..2] */
int gf_v2_release(struct gf_v2 *, struct gfx2_surf *);           /* G9: gf_vram_release */
int gf_v2_gart_bind(struct gf_v2 *, const u32 *pfn, u32 n, u32 *gpu_off);  /* G5 */
int gf_v2_gart_unbind(struct gf_v2 *, u32 gpu_off, u32 n);       /* G5 */
int gf_v2_gart_range(void *opaque, u32 handle, u32 off, u32 bytes);  /* G5。opaque は struct gf_v2 *。
                                                    handle が GART_DMA でなければ EINVAL、[off, off+bytes) が bind 済みの
                                                    頁に収まれば 0、外は ERANGE (u32 のあふれ込み)。G8 が callback 経由で呼ぶ */
int gf_v2_gart_start(struct gf_v2 *, u32 dl);    /* G5。v2 で層 3 が ready になった直後に G11 の gf_v2_start が呼ぶ */
void gf_v2_gart_reset(struct gf_v2 *);           /* G5。unregister の道で F2 が OFF になった後に G1 が呼ぶ */
/* G10 (gf_v2_enum.c)・G2 (gf_v2_display.c)。GfxBackendV2 の同名の hook の型そのもので、ctx は struct gf_v2 *。
   G2・G10・G11 の決めは末尾の「確定の後の追記 (2026-11-11、I3 の G2・D1、設計者決定)」 */
int gf_v2_enum_head(void *, u32 head, GFX2_HeadInfo *);          /* G10 */
int gf_v2_enum_conn(void *, u32 conn, GFX2_ConnectorInfo *);     /* G10 */
int gf_v2_enum_mode(void *, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *);  /* G10 */
int gf_v2_edid(void *, u32 conn, u32 block, u8 *out128);         /* G10 */
int gf_v2_mode_set(void *, u32 head, u32 conn_mask, const GFX2_ModeInfo *, const struct gfx2_surf *);
int gf_v2_flip(void *, u32 head, const struct gfx2_surf *, u32 flags, u32 dl_us);
int gf_v2_flip_done(void *, u32 head, u32 *retired_sid);
int gf_v2_vblank_wait(void *, u32 head, u32 after, u32 dl_us, u32 *seen);
int gf_v2_palette(void *, u32 first, u32 count, const u32 *xrgb);
int gf_v2_gamma(void *, u32 head, const u16 *rgb768);
int gf_v2_cursor_image(void *, u32 head, const struct gfx2_surf *);
int gf_v2_cursor_move(void *, u32 head, i32 x, i32 y);
int gf_v2_cursor_show(void *, u32 head, u32 show);
int gf_v2_dpms(void *, u32 head, u32 state);
/* hwdev/gf/accel.h への追記 (A0)。今の同期の gf3_fill・gf3_copy・gf3_scale は意味を変えない */
int gf3_fill_emit(struct gf3_ctx *, const struct gf3_surface *, struct gf3_rect, u32 color, u8 alu, u32 mask, u32 dl);
int gf3_copy_emit(struct gf3_ctx *, const struct gf3_surface *, const struct gf3_surface *, struct gf3_rect, u32 dx, u32 dy, u8 alu, u32 mask, u32 dl);
int gf3_scale_emit(struct gf3_ctx *, const struct gf3_surface *, const struct gf3_surface *, struct gf3_rect, struct gf3_rect, struct gf3_rect, bool, u32 dl);
    /* G4: accel_fill.c:44-46・accel_copy.c:61-62・accel_scale.c の fence の前までを *_emit に切り出し、
       今の gf3_fill 等は「*_emit → gf2_fence_emit → gf2_fence_wait」に書き換える (動きは同じ) */
/* struct gf3_ctx (accel.h:94-105) の末尾 (scratch の後) に A0 が足す。hwdev_layout.json:1033 も A0 (P1-3) */
typedef int (*gf3_range_fn)(void *opaque, u32 handle, u32 off, u32 bytes);
/*  struct gf3_ctx { ...; gf3_range_fn range_q; void *range_opaque; };   0 初期化 = GART の面を全部拒む */
void gf3_range_query_set(struct gf3_ctx *, gf3_range_fn, void *opaque);  /* G8 が実装 (accel_surface.c)。G11 の gf_v2_start が
                                                    gf3_range_query_set(g->g3, gf_v2_gart_range, g) → gf_v2_gart_start の順に呼ぶ */
#define GF3_HANDLE_GART_DMA (GF2_HANDLE_LAYER3_FIRST + GF3_OBJECT_COUNT + 1) /* accel.h の GF3_HANDLE_NOTIFY の次。G5 の頁表の物 */
/* G8 の gf3_surface_check: s->dma == GF3_HANDLE_GART_DMA なら
 *   c->range_q ? c->range_q(c->range_opaque, s->dma, s->off, s->bytes) : -EINVAL
 * を予約の照合 (gf_vram_match) の代わりに使う。VRAM_DMA (dma == 0) は今のまま */
struct gf3_pattern { u32 kind; u32 mono[2]; u32 color[64]; u32 fg, bg; };
    /* kind は gfx2_pattern.kind と同じ値: 0 = 無し (NULL と同じ、既定の全 1)・1 = GF3_PAT_MONO・2 = GF3_PAT_COLOR。
       mono[i] = 行 4i〜4i+3 の 4 バイト (行 4i が bits 0-7)、各バイトは MSB が左の画素 (CGA6)。fg = 1 の bit・bg = 0 の bit の色。
       color[k] = 画素 (k & 7, k >> 3) の書き先の形式の生値。fg・bg・color は書き先の形式の生値 (下位 bpp bit) */
#define GF3_PAT_MONO 1
#define GF3_PAT_COLOR 2
#define GF3_ALU_KEEP 0xffu   /* *_emit の alu: gf3_rop_set を呼ばず、channel に設定済みの operation・ROP・パターンで描く */
int gf3_rop3_set(struct gf3_ctx *, u8 subc, u8 rop3, const struct gf3_pattern *, enum gf3_fmt, u32 dl); /* G7 */
/* struct gf3_surface の末尾に u32 dma (0 = VRAM_DMA で今と同じ、GF3_HANDLE_GART_DMA) を A0 が足す。意味の実装は G8 */
```
- channel の状態の戻し (R8): パターンの種類 (MONO・COLOR) と中身は channel の物の状態で、v1 の `gf3_rop_set` (hwdev/gf/accel_rop.c:43-91) は mono の色と bit (0x310 の 4 語) だけを送り、種類 (0x30c) は送らない。G1 の `emit` は `gf3_rop3_set` で設定した命令の描画を積んだ後、同じ emit の中で既定 (`gf3_rop3_set(g3, subc, 0xcc, NULL, fmt, dl)` = MONO・全 1。nouveau も planemask のパターンの後に `NV04EXASetPattern(pNv, ~0, ~0, ~0, ~0)` で全 1 に戻す、nv04_exa.c:68-69) に戻してから返す。命令の間の channel は常に既定で、v1 の入口と v2 の束が交互でも互いの状態を見ない。戻しの積みに失敗したら emit は装置の失敗を返す (§11-2 の commit_fail → LOST)。色鍵と α は GF の HW で行わない (下の「色鍵・α」) ので戻す状態は無い。

- **ROP3・パターン (G7・G4 の追補・G1)**。手本は xf86-video-nouveau の nv04_exa.c (以下 X = `~/os32-tmp/refs/geforce6200/xf86-video-nouveau/src/`)。nouveau は状態の設定 (`NV04EXASetROP`・`NV04EXASetPattern`、X/nv04_exa.c:31-85、Prepare の中) と描画 (`NV04EXASolid` :141-160・`NV04EXACopy`) を分け、描画は設定済みの状態で描く。OS32 も同じに分ける: G7 の `gf3_rop3_set` が設定、G4 の `*_emit` が描画。`*_emit` の `alu` が `GF3_ALU_KEEP` の時は `gf3_rop_set` を呼ばず (accel_fill.c:30・accel_copy.c:54 の行を飛ばす、`mask` は見ない)、ほかは今のまま (v1 の GX の道)。

| 順 | method (subc) | 値 | nouveau・資料 |
|---|---|---|---|
| 0 | — | 書き先の形式の cpp が 4 なら何も積まずに `GF3_EUNSUPPORTED` (32bpp の ROP は nouveau も断る) | X/nv04_exa.c:52-56、今の accel_rop.c:61-63 |
| 1 | 0x0000 (MISC) | `GF3_HANDLE_PATTERN` | X/nv04_exa.c:36-37 |
| 2 | 0x0300 COLOR_FORMAT (MISC) | `gf3_format_get(fmt).pat` (INDEX8 は 3 = A8R8G8B8、565 は 1 = A16R5G6B5) | X/hwdefs/nv01_2d.xml.h:118-121、今の accel_rop.c:69-70 |
| 3 | 0x030c PATTERN_SELECT (MISC) | NULL・kind 0・kind 1 は 1 (MONO)、kind 2 は 2 (COLOR) | X/hwdefs/nv01_2d.xml.h:132-134、X/nv_accel_common.c:330-335 (初期化は MONO) |
| 4a | 0x0310 から 4 語 (MISC): MONOCHROME_COLOR(0)・(1)・MONOCHROME_PATTERN(0)・(1) | NULL・kind 0: `~0, ~0, ~0, ~0`。kind 1: `bg | hi, fg | hi, mono[0], mono[1]` (hi = cpp 1 は 0xffffff00、cpp 2 は 0xffff0000。nouveau の planemask と同じ上位の 1) | X/nv04_exa.c:38-42・52-53・69、envytools docs/hw/graph/2d/pattern.rst:270-300 (0 の bit = COLOR(0)、i=0 が bits 0-31) |
| 4b | kind 2: INDEX8 は 0x0400 から 16 語 (1 語に 4 画素、画素 4i が bits 0-7)、565 は 0x0500 から 32 語 (1 語に 2 画素、画素 2i が bits 0-15) | `color[]` の下位 8 / 16 bit を詰める | X/hwdefs/nv01_2d.xml.h:144-168、pattern.rst:303-334。nouveau の EXA は色パターンを使わない (実機で確かめる、§13-3) |
| 5 | 0x0000 (MISC) | `GF3_HANDLE_ROP` | X/nv04_exa.c:60-61・70-71 |
| 6 | 0x0300 ROP (MISC) | `rop3` (bit の添字は `(P<<2)|(S<<1)|D`、S3 と同じ) | X/nv04_exa.c:62-63・72-73 (`NVROP[alu].copy` は ROP3 の値、X/nv_rop.h:303-318) |
| 7 | 0x02fc OPERATION (`subc` = GDI か BLIT) | 1 (ROP_AND) | X/nv04_exa.c:77-78 |
- mono の bit の順: 層 3 の初期化はパターンの MONOCHROME_FORMAT を CGA6 (= 1、envytools rnndb/graph/nv1_ctxobj.xml:37-39。hwdev/gf/accel_objects.c:97) にしている。CGA6 は各バイトの bit を反転して内部の LE に入れる (pattern.rst:288-299)。内部の bit `(py&7)<<3 | (px&7)` (pattern.rst:167-177) なので、バイト r = 行 r・MSB = 左。G1 は `gfx2_pattern.mono[8]` (1 行 1B、MSB が左) を `mono[i] = m[4i] | m[4i+1] << 8 | m[4i+2] << 16 | m[4i+3] << 24` で詰めるだけ。パターンの座標は書き先の面の絶対座標 (pattern.rst:140-143) で §5-2 の原点と同じ。
- 途中の emit の失敗: `gf2_emit` の失敗はその rc を返し `c->last_rc` に残す (今の accel_rop.c と同じ)。G1 は装置の失敗として扱う。引数の誤り (`c`・`c->ch` が NULL、kind が 0〜2 の外、形式の取得の失敗) は何も積まずに `GF3_EINVAL`/`GF3_EFORMAT`。
- G1 の emit (FILL・COPY):
```c
/* drivers/video/geforce/gf_v2_ops.c (G1)。subc は FILL = GF3_SUBC_GDI、COPY = GF3_SUBC_BLIT */
if (!(op->flags & GFX2_F_PAT) && rop3 == (op->op == GFX2_OP_FILL ? 0xf0 : 0xcc))
    return map_rc(op->op == GFX2_OP_FILL                   /* 単色の塗りと SRCCOPY は今の道 (GXcopy、状態を変えない) */
        ? gf3_fill_emit(g3, &dst, r, op->fg.v, 3 /* GXcopy */, ~0u, dl)
        : gf3_copy_emit(g3, &src, &dst, sr, dx, dy, 3, ~0u, dl));
pat = op->flags & GFX2_F_PAT ? pack(ex->pat) : solid(op->fg.v);  /* PAT の無い P は fg の単色: kind 1・mono 全 0・bg = fg = fg.v */
rc = gf3_rop3_set(g3, subc, rop3, &pat, fmt, dl);
if (rc) return map_rc(rc);                                 /* EUNSUPPORTED は check が先に断るので来ない */
rc = op->op == GFX2_OP_FILL ? gf3_fill_emit(g3, &dst, r, op->fg.v, GF3_ALU_KEEP, ~0u, dl)   /* GDI の色 = S = fg */
                            : gf3_copy_emit(g3, &src, &dst, sr, dx, dy, GF3_ALU_KEEP, ~0u, dl);
rc2 = gf3_rop3_set(g3, subc, 0xcc, NULL, fmt, dl);         /* R8 の戻し。*_emit の rc に関わらず */
if (rc2) return OS32_ERR_IO;                               /* 装置の失敗 */
return map_rc(rc);   /* *_emit の検査の失敗は描画を積んでいない (状態は戻した) ので INVAL、gf2_emit の失敗は装置の失敗 */
```
- G1 の `check` が HW を返す ROP の範囲: 書き先が INDEX8・565 (32bpp は COPY の SRCCOPY だけ)、PAT の kind 2 は INDEX8・565、bg の `GFX2_CX_TRANSPARENT` のパターンは NOSYS (sw)。

- **色鍵・α (GF は HW で行わない)**: nouveau の NV40 系の GR の class の表 (nvkm/engine/gr/nv44.c:83-101、Ra266 の NV44A) に chroma (0x0017・0x0057) は無い (NV04 の表 nvkm/engine/gr/nv04.c:1370-1371 にはある)。xf86-video-nouveau も IMAGE_BLIT の COLOR_KEY に NULL を結ぶだけ (X/nv_accel_common.c:421-422) で、2D の色鍵の手本が無い。α も nouveau の NV40 は 3D (curie、X/nv40_exa.c の `NV40EXAPrepareComposite`、X/nouveau_exa.c:504-507) で合成し、2D の BLEND (BETA1・BETA4 は SIFM の SRCCOPY に結ぶだけ、X/nv_accel_common.c:472-480) は使わない。§3-3 の premultiplied の式に合う 2D の BLEND の資料も無い (envytools docs/hw/graph/2d/intro.rst の Blending は未記述)。よって GF の `check` は SRCKEY・DSTKEY の COPY と ALPHA を NOSYS にし、sw (S9・S10) で行う。段 G3・G6 は廃止。HW 化は 3D (curie) の設計で行う (§14)。

- **GART (G5)**: NV44A の GART は nouveau の nv04 の経路をそのまま移す。G5 が書くのは instmem (PRAMIN) の頁表と RAMHT だけで、MMIO のレジスタは 1 つも書かない (0x100800・0x100850 は今の `gf2_fb_init` (hwdev/gf/fifo.c:98-104、nvkm/subdev/fb/nv44.c:50-55 と同じ) のまま。NV44A は `.fb = nv44_fb_new`・`.mmu = nv04_mmu_new`、device/base.c:683・688 で、nv44_mmu_init の 0x1008xx は走らない)。

| 項目 | 決め | nouveau (drivers/gpu/drm/nouveau/) |
|---|---|---|
| 方式 | 頁表は instmem の 1 つの物で、先頭 8 バイトが DMA の物の見出し、続けて 4 バイトの PTE が `GF_V2_GART_PAGES` (32,768) 本 = 128MB。GART の ctxdma は頁表の物そのもの (16B の別の DMA の物を作らない。`gf2_dma_new` は使わない) | nvkm/subdev/mmu/nv04.c:29-35 (`.vmm` の global)、nvkm/engine/dma/usernv04.c:51-55・97-101 (TARGET_VM は clone、start 0 は頁表を wrap) |
| 確保・配置・容量 | `gf_inst_alloc(f, GF_V2_GART_PT_BYTES, 0x1000, true, &gart.obj.inst)` (0x20008 バイト、整列 0x1000、0 で埋める)。instmem の予約 `GF_INSTMEM_BYTES` の `GF_RESERVE_BASE_BYTES 0x100000` (頁表 512KB + 物 512KB、hwdev/gf/vram_layout.h:13-17) の中。取れなければ `GF2_ENOSPC` | nvkm/subdev/mmu/vmmnv04.c:73-77 (bits 15・4 バイト・整列 0x1000)、vmm.c:1158-1159 (pd_header 8 + 4×2^15、0 で埋める)、mmu/base.c:207 (INST に置く)、instmem/nv40.c:175 (頁表の 512KB) |
| 見出し | +0 に `0x0002103d` (IN_MEMORY・PCI・RW・頁表あり・線形でない)、+4 に `0x07ffffff` (limit − 1、limit = 1 << 27) | vmmnv04.c:135-139、vmm.c:1206-1207 (size 0 で limit = 1 << bits) |
| PTE | 頁 i は +8+4i に `pfn << 12` と 3 (PRESENT・RW) の OR。外すときは 0 | vmmnv04.c:45-57 (dma)・59-64 (unmap) |
| 書きの吐き出し・flush | 見出しと PTE は `gf_inst_write32` (書いた後に読み返す、hwdev/gf/instmem.c:68-77) で書き、それ以上の待ちと TLB の flush はしない | instmem/nv40.c:68-71 (release は wmb だけ)、vmmnv04.c:92-99 (nv04 の vmm に `.flush` が無い)、vmm.c:127-135 (flush は hook がある時だけ) |
| DMA の物の登録 | `gf2_ramht_add(ch, GF3_HANDLE_GART_DMA, GF2_RAMHT_ENGINE_DMA, gart.obj.inst.off, &gart.obj.cookie)`、`gart.obj.handle = GF3_HANDLE_GART_DMA`・`class_id = GF2_DMA_IN_MEMORY`。登録は drain と FIFO の pause の間だけ (hwdev/gf/command.h:180-183) | nouveau_chan.c:420-432 (AGP でない時の GART の ctxdma は TARGET_VM・start 0・limit = vmm の limit − 1) |
| 番地の割り当て | `gart.bind[]` (first の昇順) の隙間のうち、n 頁が入る最も小さい隙間 (同じ大きさなら番地の小さい方) の先頭。`*gpu_off = first << 12` | vmm.c:1773-1786 (大きさ順の空きの木で最小の空き)、nouveau_ttm.c:126-154 (GART の資源を取る時に `nvif_vmm_get`)、nouveau_sgdma.c:30-55 (bind で写像) |
| 停止の時の解放 | G5 は自分で頁表の物を消さない。F2 の停止 (`gf2_stop` → `command_stop` の `release_instances`、hwdev/gf/command.c:44-59・61-72) が drain・FIFO の停止・engine の停止の後に RAMHT の全項目と可変の inst を全部消し、頁表もそこで消える。nouveau では GART の ctxdma は channel と一緒に消え、頁表は装置の破棄まで残る。OS32 は instmem を F1 の停止で手放すので、頁表を channel より長く持たない | nouveau_chan.c:109 (chan->gart の dtor)、mmu/base.c:378-383・402 (頁表は oneinit で 1 度作り dtor で破棄) |

- 状態: `gart.epoch` は頁表の物を作った F2 の epoch (0 = 無い)。「生きている」は `gart.epoch != 0 && gart.epoch == g->ch->epoch && g->ch->state == GF2_RUN`。`gart.bind[]` (`count` 件) は bind ごとの `{first, n, pfn}` で、epoch と無関係に unbind か reset まで残る。
- `gf_v2_gart_start(g, dl)`: 生きていれば 0。`g->ch->state != GF2_RUN` は `GF3_ESTATE`。順は gf3_init と同じ形 (hwdev/gf/accel_objects.c:200-245): `gart.obj = {0}` (前の epoch の物は F2 の停止が消している) → drain (`gf2_fence_emit` → `gf2_fence_wait`) → `gf2_fifo_pause` → 確保 → 見出し → 記録のある頁の PTE を全部書き直す → RAMHT に登録 → resume (`ch->last_rc = 0; gf2_fifo_resume(ch);` の後に `last_rc` と RUN を見る、accel_objects.c:157-163) → `gart.epoch = ch->epoch`。確保から登録までの失敗は、取った inst を `gf_inst_free` して `gart.obj = {0}` に戻し、resume してから rc を返す (resume の失敗はその rc)。drain・pause の失敗は rc をそのまま返す (門は閉じたまま、後始末は F2 の停止)。書き直しの理由: F2 の recover (epoch が変わる) で頁表は消えるが、LOST にならなかった GART の面は回復の後も HW で描く (§6-3)。nouveau の頁表は channel を作り直しても残り写像が続くので、借りた pfn から書き直して同じ状態にする。
- 呼ぶ所: G11 の `gf_v2_start` だけ (`gf3_range_query_set` の直後)。`gf_v2_start` は v1 の起動の成功の後 (G1 が backend_geforce.c の `geforce_init` から) と、LOST の間の mode_set の最初 (G2) に呼ばれる。層 3 を作り直すのは層 3・4 の持ち主 (`gf3_service`・`gf4_service_*`) だけで、v2 は作り直された層 3 に結び直すだけ。0 以外は `gf_v2_start` の失敗 (`lost` を 1 のまま、G1 は v2 の HW を使わない、G2 は mode_set の失敗)。
- `gf_v2_gart_bind(g, pfn, n, *gpu_off)`: `pfn == NULL`・`n == 0` は `OS32_ERR_INVAL`。生きていなければ `OS32_ERR_IO`。`n > GF_V2_GART_PAGES`・入る隙間が無い・`count == GF_V2_GART_BINDS` は `OS32_ERR_NOSPC`。PTE を n 本書き、途中の `gf_inst_write32` の失敗は書いた分に 0 を書いて (その書きの rc は見ない) `OS32_ERR_IO`。成功で記録を昇順の位置に挿し `*gpu_off` を書いて 0。FIFO は止めない (nouveau も写像の書きで channel を止めない)。
- `gf_v2_gart_unbind(g, gpu_off, n)`: `gpu_off` が頁の境界で `first = gpu_off >> 12` と `n` が記録の 1 件と一致しなければ `OS32_ERR_INVAL` で何も変えない。一致すれば、生きている時だけ n 本の PTE に 0 を書き (書きの rc は見ない。nouveau の unmap も戻り値が無い)、記録を外して 0。生きていない時は書かずに外して 0 (頁表は F2 の停止で消えている)。中核が呼ぶのは fence の完了の後 (§7-3 の順)。
- `gf_v2_gart_range(opaque, handle, off, bytes)`: `handle != GF3_HANDLE_GART_DMA` は `GF3_EINVAL`。生きていないか、`off >= first << 12 && bytes <= ((first + n) << 12) − off` を満たす記録が無ければ `GF3_ERANGE`、あれば 0 (面は 1 回の bind なので、1 つの記録の中に収まること)。
- `gf_v2_gart_reset(g)`: `gart` を全部 0 にする (物・epoch・記録)。G1 が unregister の道で、F2 の停止が `GF2_OFF` を確かめた後にだけ呼ぶ。unregister の後の中核は `gfx2_lost_be` の `gart_unbind` (0) を呼び G5 に来ないので、ここで捨てないと、C6 が再利用した pfn の枠を次の start が張ってしまう。OFF にならなかった時 (ESTOP・LOST_HELD) は呼ばない (頁は中核が返さない、§6-3)。
- 分担: G5 = `struct gf_v2` の `gart` の全部と上の 5 つの関数。G1 = `gf_v2` の実体、表の `.gart_bind`・`.gart_unbind` (ctx を `struct gf_v2 *` にして G5 を呼ぶだけ)、unregister の `gf_v2_gart_reset`、GART の面の `gf3_surface` を `dma = GF3_HANDLE_GART_DMA`・`off = gpu_off` で作ること。G11 = `gf_v2_start` の中の `gf3_range_query_set` → `gf_v2_gart_start`。G8 = `gf3_surface_check` の GART の分岐で `range_q` を呼ぶこと (上のまま) と、`gf3_surface_set` で `dma` が 0 でない面の DMA の handle に `dma` を結ぶこと (§7-2)。C6 = pfn の表を unbind まで変えない (今の gart.c のまま)。

### 11-7. 表の組み立て・登録・module・build
```c
typedef struct GfxBackendV2 {   /* include/gfx_hal2.h (A0)。戻り値は OS32_ERR_*、第 1 引数は ctx。全部の hook は必須
                                   (使わない物は NOSYS を返す関数を置く。NULL は登録で拒む) */
    u32 abi; void *ctx;
    int (*caps)(void *, GFX2_Caps *);                                   /* 副作用なし */
    int (*check)(void *, const struct gfx2_exec *);                     /* 0 = HW でできる、NOSYS = できない */
    int (*heap_range)(void *, u32 *base, u32 *size, u32 *phys);         /* heap に渡す VRAM の範囲と VRAM の offset 0 の物理番地 (C17・C7 の PTE の元) */
    int (*alloc)(void *, struct gfx2_surf *);                           /* heap の置き場に装置の予約、priv を埋める */
    int (*release)(void *, struct gfx2_surf *);                         /* GPU の使用の終わりと写像の解除の後だけ */
    int (*kva)(void *, const struct gfx2_surf *, u8 **va);              /* CPU から触る kernel の VA。NOSYS → sw は NOSUPPORT */
    int (*gart_bind)(void *, const u32 *pfn, u32 n, u32 *gpu_off);
    int (*gart_unbind)(void *, u32 gpu_off, u32 n);
    int (*emit)(void *, const struct gfx2_exec *, u32 fence);           /* 検査済みの 1 命令の描画だけ積む。fence は記録用で印は積まない
                                                                           (印は後の fence_emit)。INVAL・NOSYS は何も積まなかった時だけ返す */
    int (*readback)(void *, const struct gfx2_exec *, u8 *kbuf);        /* 完了まで待って kbuf に */
    int (*kick)(void *);
    int (*fence_emit)(void *, u32 v2, u32 dl_us);
    int (*fence_state)(void *, u32 *completed, u32 *failed_upto);
    int (*fence_wait)(void *, u32 v2, u32 dl_us);
    int (*enum_head)(void *, u32 head, GFX2_HeadInfo *);
    int (*enum_conn)(void *, u32 conn, GFX2_ConnectorInfo *);
    int (*enum_mode)(void *, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *);
    int (*edid)(void *, u32 conn, u32 block, u8 *out128);
    int (*mode_set)(void *, u32 head, u32 conn_mask, const GFX2_ModeInfo *, const struct gfx2_surf *);
    int (*flip)(void *, u32 head, const struct gfx2_surf *, u32 flags, u32 dl_us);
    int (*flip_done)(void *, u32 head, u32 *retired_sid);               /* vblank の処理 (C13) が呼ぶ */
    int (*vblank_wait)(void *, u32 head, u32 after, u32 dl_us, u32 *seen);
    int (*palette)(void *, u32 first, u32 count, const u32 *xrgb);
    int (*gamma)(void *, u32 head, const u16 *rgb768);
    int (*cursor_image)(void *, u32 head, const struct gfx2_surf *);
    int (*cursor_move)(void *, u32 head, i32 x, i32 y);
    int (*cursor_show)(void *, u32 head, u32 show);
    int (*dpms)(void *, u32 head, u32 state);
    int (*stop)(void *);                                                /* 喪失の時。失敗 = LOST_HELD */
} GfxBackendV2;
#define GFX_HAL2_ABI 1
```
| 何 | 担当の段 | 名前 |
|---|---|---|
| GfxBackend の末尾 (gfx_hal.h:143 の後) に `const struct GfxBackendV2 *v2; const struct module_record *v2_mod;` と hwdev_layout.json:661 | A0 | — |
| GF の 1 表 (G1〜G9 の関数を並べる) と backend_geforce.c:749-767 の `.v2 = &gf_backend_v2` | G1 | `const GfxBackendV2 gf_backend_v2` (gf_v2_ops.c、module の中) |
| GF の写し (D7): geforce_module.c の `geforce_start` の :253 (表示の hook の検査) の後・公開 :334 の前に、`candidate.v2` の表が module の ro の中にあること (`direct_ops` と同じ :221-227 の型) と各 hook の `module_text_contains` を確かめてから `if (candidate.v2) { gf_v2_kcopy = *candidate.v2; candidate.v2 = &gf_v2_kcopy; } candidate.v2_mod = r;` (module が v2_mod に何を書いても r で上書き)。停止の道に `gfx2_adapter_unregister(&gfx_backend_geforce)` | G1 (geforce_module.c の 2 か所) | `static GfxBackendV2 gf_v2_kcopy` (kernel) |
| PEGC の表 (caps・enum・heap_range=0・kva) と backend_pegc.c の `.v2` | P1 | `pegc_backend_v2` (pegc_v2.c) |
| Trident: 表は作らない (`v2 = NULL`、中核が v1 から写す)。同期の bind は T1 | T1 | — |
| 登録: gfx_core の backend の選択の後 (下のコードの 2 か所) に `gfx2_adapter_register(g_backend, display_provider_ledger(g_backend), GFX2_AF_ACTIVE)`。最初の登録の時に 1 回だけ、`display_provider_each` で probe が通った他の backend と PC-98 の内蔵を `flags=0` で登録する。kind は常に `display_provider_ledger()` から取り、gfx/v2 は `&gfx_backend_*` を直に比べない (0 = 知らない backend は登録しない) | C1 (gfx_core.c・gfx_core_init.inc の呼ぶ行も C1) | — |
| 列挙の口: include/display_provider.h に `void display_provider_each(void (*fn)(GfxBackend *be, void *arg), void *arg)` (候補の表 gfx_selector.c:12-18 の順に、未リンクの weak を飛ばし、`gfx_probe_backend` (:46-51) と同じ判定で probe が通った物ごとに fn を呼ぶ。ハードウェアの設定はしない。自動の選択で飛ばす明示の指定だけの backend (Trident・GeForce、候補の表 :13-14) は列挙で probe せず、その時に選ばれている backend のときだけ渡す。`cirrus_probed` の記録は選択の probe だけが立てる)。実体は platform/pc98/gfx_selector.c、platform/powermac/display_provider.c は空 (fn を呼ばない) | A0 (宣言と powermac の空の実装)・C1 (gfx_selector.c の実装) | — |
| module: trident の import に `gfxsync_bind`・`gfxsync_write_rows`・`gfxsync_read_rows`・`gfxsync_flush`・`gfxsync_unbind`・`gfx2_sync_publish` (module_schema.py と生成物 build/out/trident/trident_imports.h の両方)。geforce は import 無し・export は今の backend の表 (中の `v2`)。両方の ABI の世代を上げる | A0 (module_schema.py と生成物) | `TRIDENT_MODULE_ABI_GEN`・`GEFORCE_MODULE_ABI_GEN` |
| build: gfx/v2/*.c・gfx/sync/*.c を build/sources/order.mk の `C_ORDER_LEGACY` と build/sources/common.mk の `C_COMMON` の両方に (`C_KERNEL` は build/select.mk:243 がこの 2 つから組む)、tools/tests/fixtures/build_profile_x86.json に同じ一覧、新しい根を足すなら tools/source_roots.py。gf_v2_*.c は build/geforce-module.mk の `GEFORCE_OBJS` (:8) に。a0_weak.c は両方に。手本は B4 の 989ba493 (order.mk・build_profile_x86.json・source_roots.py・tools/ppc/selected_success.json を同じ commit で直した形) | A0 (各段は自分のファイルが一覧にあることだけ前提) | — |
| 登録の検査 (P1-6): `abi == GFX_HAL2_ABI`、全 hook が非 NULL、`v2_mod` が非 NULL なら全 hook が `module_text_contains(v2_mod, p)` (module_runtime.h:56) の中。`v2_mod == NULL` (組込みの PEGC) は abi と非 NULL だけ。`v2 == NULL` (Trident・PC-98) は C16 の shim を据え、検査しない | C1 | — |

shim と登録 (R4・R5)。`a->be` はどの時点でも非 NULL で、中核は `a->be->` を NULL の検査なしに呼ぶ:
```c
/* gfx/v2/v1_shim.c (C16)。v2 の表の無い backend (Trident・PC-98、v2 == NULL) の代わり。ctx は struct gfx2_adapter * */
static int shim_nosys_x(void *c, const struct gfx2_exec *e) { (void)c; (void)e; return OS32_ERR_NOSYS; }
static int shim_ok(void *c) { (void)c; return 0; }
static int shim_heap_range(void *c, u32 *b, u32 *n, u32 *p) { (void)c; *b = *n = *p = 0; return 0; }  /* VRAM の面は NOSPC */
static int shim_fence_state(void *c, u32 *done, u32 *fail)
{ const struct gfx2_adapter *a = c; *done = a->fence.issued; *fail = 0; return 0; }     /* 常に完了 (§9-2) */
static int shim_mode_set(void *c, u32 head, u32 cm, const GFX2_ModeInfo *m, const struct gfx2_surf *s)
{ struct gfx2_adapter *a = c; (void)cm; (void)s;
  return head || !a->v1->set_mode ? OS32_ERR_NOSYS : a->v1->set_mode(m->id); }       /* hook は呼ぶたびに a->v1-> から (D2) */
/* flip・vblank_wait・palette・gamma・cursor_*・dpms も同じ形で §9-2 の右の列の v1 の hook へ (NULL なら NOSYS)。
 * enum_* は C11 の gfx2_v1_enum_* (gfx2_int.h) を呼ぶだけ、caps は C10 の gfx2_caps_from_v1 */
static int shim_enum_head(void *c, u32 head, GFX2_HeadInfo *o)
{ const struct gfx2_adapter *a = c; return gfx2_v1_enum_head(a->v1, head, o); }   /* conn・mode も同じ形 */
const GfxBackendV2 gfx2_v1_shim = {
    .abi = GFX_HAL2_ABI, .caps = shim_caps, .check = shim_nosys_x, .heap_range = shim_heap_range,
    .alloc = shim_nospc, .release = shim_ok_s, .kva = shim_nosys_kva,
    .gart_bind = shim_nosys_gb, .gart_unbind = shim_ok_gu,
    .emit = shim_nosys_emit, .readback = shim_nosys_x2,        /* check が NOSYS なので呼ばれない */
    .kick = shim_ok, .fence_emit = shim_ok_fe, .fence_state = shim_fence_state, .fence_wait = shim_ok_fw,
    .enum_head = shim_enum_head, .enum_conn = shim_enum_conn, .enum_mode = shim_enum_mode, .edid = shim_edid,
    .mode_set = shim_mode_set, .flip = shim_flip, .flip_done = shim_flip_done, .vblank_wait = shim_vblank_wait,
    .palette = shim_palette, .gamma = shim_gamma, .cursor_image = shim_cursor_image,
    .cursor_move = shim_cursor_move, .cursor_show = shim_cursor_show, .dpms = shim_dpms, .stop = shim_ok,
};
/* gfx2_lost_be: unregister の後の表。全 hook が OS32_ERR_STALE、kick・stop・release・gart_unbind は 0 (装置は止まっているので、
 * 解放の道 §7-3 を STALE で KEEP にしない)、
 * fence_state は completed = failed_upto = a->fence.issued (待つ物を全部「失敗の完了」にする) */

/* gfx/v2/backend_reg.c (C1)。gfx_init のたびに呼ばれてよい (冪等) */
int gfx2_adapter_register(GfxBackend *v1, u32 kind, u32 flags)   /* kind = display_provider_ledger(v1) */
{
    struct gfx2_adapter *a = adapter_by_v1(v1);                 /* 渡された v1 の番地が同じなら同じ adapter (既知の backend の名前とは比べない) */
    const GfxBackendV2 *be = v1->v2 ? v1->v2 : &gfx2_v1_shim;
    u32 base, size;
    if (v1->v2 && be_check(be, v1->v2_mod)) return OS32_ERR_INVAL;   /* abi・非 NULL・module の .text */
    if (!gfx2_quar_owner &&                                     /* §8: 取れなくても登録は続け、次の登録でもう一度 */
        !ledger_owner_new(LEDGER_KIND_MODULE, 0, "gfx2-quar", &gfx2_quar_owner))
        quar_warn_once();
    if (!a) {
        if (!(a = adapter_slot_new())) return OS32_ERR_FULL;
        a->v1 = v1; a->kind = kind;
    } else if (a->be != be && a->be != &gfx2_lost_be) {
        return OS32_ERR_INVAL;                                  /* 生きた adapter の表は替えない (module の入れ替えは unregister の後) */
    }
    if (a->be != be) {                                          /* 新規、または unregister の後の module の再 load */
        a->be = be; a->ctx = v1->v2 ? be->ctx : a;
        a->be->heap_range(a->ctx, &base, &size, &a->vram_phys); gfx2_heap_init(&a->heap, base, size);
        gfx2_caps_refresh(a);                                   /* C10 */
    }
    if (flags & GFX2_AF_ACTIVE) {
        if (a->state != GFX2_AD_OK && (a->state != GFX2_AD_LOST_HELD || !a->be->stop(a->ctx)))
            gfx2_adapter_recovered(a);                          /* v1 の init が通った = 次の mode_set (§6-3) */
        gfx2_adapter_activate(a);
    }
    return 0;
}
void gfx2_adapter_activate(struct gfx2_adapter *a)            /* ACTIVE の付け替え */
{
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++) {
        struct gfx2_adapter *o = &gfx2_adapters[i];
        if (o == a || !(o->flags & GFX2_AF_ACTIVE)) continue;
        o->flags &= ~GFX2_AF_ACTIVE;
        for (u32 h = 0; h < o->heads; h++) gfx2_head_detach(o, h);   /* C13。走査は v1 の切り替えが済ませた */
        if (++o->gen == 0) o->gen = 1;                          /* 古い gen の束は STALE、以後は NOSYS (§5-3) */
    }
    a->flags |= GFX2_AF_ACTIVE;
    v1_bridge_rebind();                                         /* C14: 別名を ACTIVE の adapter へ移す (§6-5) */
}
```
- C1 が gfx_core に足す呼ぶ行: (1) gfx/gfx_core_init.inc:8 の `gfx_select_and_init_backend` の後、:9 の `initialized < 0` の戻りと :10 の間 (`initialized >= 0` の時。:10-16 の packed 系の早い戻りの道と :18 以降の道の両方が通る。init の失敗で pc98 へ落ちた場合 gfx_core.c:227-231 も、ここで `g_backend` = pc98 を ACTIVE にする)。(2) `gfx_shutdown` (gfx_core.c:356-384) の GF → pc98 の切り替え (:375-382) の中、`g_backend = DISPLAY_FALLBACK_BACKEND;` (:380) の後。(3) gfx_core_init.inc:4 の `palette_shadow_reset()` の直後に `gfx2_pal_reset16()` (§9-2)。`gfx_init`・`gfx_init_200` (gfx_core.c:295・300) が何度呼ばれても adapter は増えない。
- `gfx_prepare_backend` (gfx_core.c:252-291) は登録しない。起動時の退避 (選択 → prepare か init → shutdown) の後は adapter を作らず、次の gfx_init の (1) で登録する。
- `gfx_shutdown` の時の面と別名: v2 の面はそのまま残す (free・desc・lock は通る。ACTIVE でなくなった adapter への submit・create・head の口は NOSYS)。GF → pc98 の時は上の `gfx2_adapter_activate` が GF の heads を `gfx2_head_detach` し、別名は `v1_bridge_rebind` で pc98 の v1 の今の値へ移る。backend が替わらない時 (PEGC のままテキストへ戻る等) は C1 が `gfx_shutdown` の `gfx_reinit_surfaces(1)` (:383) の前に ACTIVE の adapter の全 head を `gfx2_head_detach` し、C14 の 3 か所の 1 つ (`gfx_reinit_surfaces` の後) の `v1_bridge_rebind` で別名は形無し (`alias_update(s, NULL)`) になる。

## 12. 段と順 (1 機能 1 ファイル、1 行 = 1 段 = 1 実装者)

順は固定: **A0 → (実装の段、並列) → I1 → I2 → I3**。実装の段は単体の host 試験と `make kernel` が通れば「部品の実装済み」で、**機能の完了とは数えない**。完了は、その部品を含む I の段の合格だけ (weak の仮の実体が残らないことを含む)。

| 段 | ファイル | 呼ぶ (宣言の所) | 呼ばれる | 書く共有の状態 | 行 | I |
|---|---|---|---|---|---|---|
| A0 | §11 の全ヘッダ・kapi.json と生成物・a0_weak.c ×3・module_schema.py・hwdev_layout.json (GfxBackend・gf3_ctx)・build の一覧 (build/sources/order.mk・common.mk・build/geforce-module.mk・tools/tests/fixtures/build_profile_x86.json・必要なら tools/source_roots.py、§11-7)・include/display_provider.h (`display_provider_each` の宣言)・platform/powermac/display_provider.c (空の `display_provider_each`)・arch/x86/arch_cpu.h・arch/ppc/arch_cpu.h (`arch_cpu_serialize`・`arch_cpu_has_mmx`、§10)・include/config.h (`SYS_GFXSYNC_CFG`)・include/memmap.h (`MEM_GFX2_MAP_BASE`・`MEM_GFX2_MAP_END`、§11-4。生成の memmap_layout.h は gen_memmap.py が `MEM_*` を全部写すので手を入れない。帯の表 (`make docs-gen` の MEMMAP) には lease の帯も載せていないので足さない)・tools/check_p2v.py (`USER` の正規表現に `MEM_GFX2_MAP_\w+`、[C5] の user-v2p を gfx2 の帯にも掛ける)・tools/tests/gfx2_abi_host.c・tools/checks/gfx2_wiring.txt・gfx2_no_weak.sh | — | 全部 | — | 700 | — |
| C1 | gfx/v2/backend_reg.c・platform/pc98/gfx_selector.c の `display_provider_each` (+gfx_core_init.inc:8 の後と gfx_core.c の `gfx_shutdown` の 2 か所の登録・head の切り離しの行、gfx_core_init.inc:4 の後の `gfx2_pal_reset16`、§11-7) | `be->heap_range`・`gfx2_heap_init`・C10・`module_text_contains`・`gfxsync_mode`・`caller_access_get_user`・`display_provider_ledger`・`display_provider_each`・C13 (`gfx2_head_detach`・`gfx2_pal_reset16`)・C14 (`v1_bridge_rebind`) | 全部の公開の入口・P1・T1 (publish)・gfx_core | `gfx2_adapters[]` の index・v1・be・ctx・kind・flags・vram_phys、`sync_tab` | 280 | I1 |
| C2 | gfx/v2/format.c | — | C9・S*・U1 | — | 200 | I2 |
| C3 | gfx/v2/fence.c | `be->fence_emit/state/wait`・`gfx2_surf_state_set`・`gfx2_quarantine_drain` | C4・C7・C9・C12〜C15 | `adapter.fence` (reserved・issued)・`gen`・`state`、`surf.last_*` | 220 | I1 |
| C4 | gfx/v2/surface.c | C5・C6・C7 (`gfx2_map`・`gfx2_unmap`・`gfx2_map_detach`)・C18・`be->alloc/release/kva` | 公開・C7・C9・C12〜C15 | `gfx2_surfs[]` (§11-2 の C4 の欄、別名の作成と更新を含む) | 320 | I1 |
| C5 | gfx/v2/vram_heap.c | — | C1・C4 | `adapter.heap` | 150 | I1 |
| C6 | gfx/v2/gart.c | `pgalloc_alloc_n_owner`・`pgalloc_pin/unpin`・`ledger_transfer_pinned`・`be->gart_bind/unbind` | C4・C7・C15 | `surf.gart_*`・pfn の表 (gart.c の static) | 200 | I1 |
| C7 | gfx/v2/lock.c | `paging_gfx2_map`・`paging_lease_unmap`・C1 (`gfx2_sync_get`)・C3・C4・`gfxsync_read/write_rows` | 公開・C4・C15 | `surf.map_*`・`staging`、`gfx2_vaspace[4]` | 270 | I1 |
| C8 | gfx/v2/check_op.c (`gfx2_check_op`・`gfx2_check_exec`。gfx2_int.h の `gfx2_check_exec` の宣言と `tools/checks/gfx2_wiring.txt` の `I2 kernel gfx2_check_exec` を最初の commit で足す、§4-2) | C2・C1 (`gfx2_sync_get`)・C7 (`gfx2_cpu_visible`)・`be->check` | 公開・C9 | — | 150 | I2 |
| C9 | gfx/v2/submit.c (+gfx/v2/sw_table.c の `gfx2_sw_rowbuf` の実体と gfx2_sw.h の宣言・`GFX2_SW_ROW_BYTES`、os32_gfx2.h の `GFX2_ROP_SRCCOPY`・`GFX2_ROP_PATCOPY`、確定の後の追記 2026-11-11 I2)。**C8 の後**に着手 (C8 の枝を merge して `gfx2_check_exec` を呼ぶ) | C3・C4・C8 (`gfx2_check_exec`)・S*・`be->emit/readback/kick` | 公開 | `k_ops`・`ex`・パターン (static)。`gfx2_sw_table` は I1 の sw_table.c のまま読む | 260 | I2 |
| C10 | gfx/v2/caps.c (+include/gfx_sync.h の `gfxsync_profile` の宣言・gfx/sync/a0_weak.c の weak (out を "" に)・`tools/checks/gfx2_wiring.txt` の `I3 kernel gfxsync_profile`) | `be->caps`・C1 (`gfx2_sync_get`)・`gfxsync_profile` (Y4) | 公開・C1 (`gfx2_caps_refresh`)・C16 (`gfx2_caps_from_v1`、§4-1 の shim の列) | `adapter.caps` | 140 | I2 |
| C11 | gfx/v2/enum.c | `be->enum_*`・`edid` (v1 から作る列挙は C11 の `gfx2_v1_enum_*` (§11-2) で、C16 の shim が呼ぶ) | 公開・C12・C16 | — | 200 | I3 |
| C12 | gfx/v2/modeset.c | C3・C4・C11・`be->mode_set`・`gfx_policy_reeval` (H4) | 公開 | `adapter.head[]`・`restore_on_leave` | 160 | I3 |
| C13 | gfx/v2/display.c | `be->flip`・`flip_done`・`vblank_wait`・`palette`・`gamma`・`cursor_*`・`dpms` (v1 だけの backend は C16 の shim の中で v1 の hook へ)・C4・`gfxsync_write_rows` | 公開・C1 (`gfx2_head_detach`・`gfx2_pal_reset16`)・C14 (`gfx2_pal_set_v1`)・C15 | `adapter.head[].pend_sid`、`surf.dirty` を 0 に、`gfx2_pal` (§9-2) | 240 | I3 |
| C14 | gfx/v2/v1_bridge.c (+gfx_core.c 3 か所・gfx_hw_submit.c:70・95・surface_query.c・gfx/gfx_vram.c (present_rect :55-63・:284-298)・gfx/gfx_core_api.inc (fill・blit :66-78、パレットの写しの `gfx2_pal_set_v1` を `gfx_set_palette_hal` :50-59・`gfx_lease_palette` :102-130)・exec/lease.c (DISPLAY の lease、`surface_lease`:245-296・`surface_lease_bundle`:302-353) の呼ぶ行)。exec/lease.c は M3 も触るので **M3 の後**に着手。C7 の追補 `gfx2_lock_revoke` (gfx/v2/lock.c・gfx2_int.h の宣言・`tools/checks/gfx2_wiring.txt` に `I2 kernel gfx2_lock_revoke`、§6-5) を C14 の枝の最初の commit で入れる | C3・C4 (`gfx2_surf_alias`・`alias_update`)・C7 (`gfx2_lock_revoke`) | v1 の入口 | 別名の面 | 220 | I2 |
| C16 | gfx/v2/v1_shim.c (R4、§11-7 のコード。`gfx2_v1_shim`・`gfx2_lost_be`) | C10 (`gfx2_caps_from_v1`)・C11 (`gfx2_v1_enum_*`)・v1 の hook | C1 | — | 180 | I1 |
| C17 | gfx/v2/map_audit.c (R1、§7-4 のコード) | C7 (`gfx2_surf_page`) | M3 (`lease_audit_all`) | — | 70 | I1 |
| C18 | gfx/v2/sysmem.c (R2、§7-2 の表とコード) | `pgalloc_alloc_n_owner`・`pgalloc_free_n_owner` | C4 | `surf.sys_pfn`・`sys_n`・`kva` | 60 | I1 |
| C15 | gfx/v2/owner_release.c | C3・C4・C6・C7 (`map_detach`・`staging_drop`・`vaspace_drop`)・C13 | X1 | `surf.owner`・`flags` の KEEP (隔離) | 200 | I1 |
| S1〜S11 | gfx/v2/sw_fill.c・sw_copy.c・sw_rop.c・sw_rop4.c・sw_mono.c・sw_hostdata.c・sw_readback.c・sw_stretch.c・sw_ckey.c・sw_alpha.c・sw_line.c | C2・`gfxsync_row_*` (view.sync の時) | C9 | — | 1450 | I2 |
| M1 | mm/pgalloc_owner.inc | — | C6 | `pin_map[]` | 90 | I1 |
| M2 | mm/paging_gfx2.inc (paging.c に include) | `pgalloc_alloc_phys` | C7 | `as->lease_pt_phys[]` | 80 | I1 |
| M3 | include/memmap.h (`MEM_LEASE_ALLOC_END`・`MEM_LEASE_ALLOC_PDES`。`MEM_GFX2_MAP_*` は A0)・exec/lease.c (:69 の上限、:176-177 の範囲、:212 の PTE の走査の範囲、:380 の次に `gfx2_map_audit`、§7-4) | C17 | C7・lease | — | 15 | I1 |
| X1 | exec/exec_finish.inc:34 の後 | `gfx2_owner_release`・`paging_addrspace_poison` | — | `as->appmem_poisoned` (poison の口で) | 15 | I1 |
| Y1〜Y4 | gfx/sync/sync_cfg.c・sync_gap.c・sync_write.c・sync_bind.c | `sysconfig` の読み・`cpu_loops_per_tick`・`arch_cpu_serialize` (Y3) | C7・C13・S*・T1・P1 | `gfxsync_ctx_pool` (Y4) | 500 | I3 |
| T1 | drivers/video/trident/present.c・regs.c・trident_module.c (write_gap.c 削除と旧パスの空の規則 (§10)、停止の道に `gfx2_adapter_unregister`) | `gfxsync_*`・`gfx2_sync_publish` (A0 が import の一覧と生成物に足した物) | v1 present | — | −50 | I3 |
| P1 | drivers/video/pc98/pegc_v2.c (+backend_pegc.c の `.v2`) | `gfxsync_bind`・`gfx2_sync_publish` | C1 | — | 150 | I3 |
| G1 | drivers/video/geforce/gf_v2_ops.c (+backend_geforce.c の `.v2`・`geforce_direct_ok` の LOST の 1 行 (§6-3、`&& !gf_v2_inst.lost`)・`geforce_init` の 2 か所の `gf_v2_start` (:608 の早い戻りの前と :629-631 の後、末尾の追記の G11)、geforce_module.c の写しと unregister の 2 か所)。`gf_v2_inst` は static にせず gf_v2.h の extern (末尾の追記)。表の `.stop = gf_v2_stop` (G11)、GF2/GF3/GF4 の rc の写しは G11 の `gf_v2_rc`。emit は §11-6 の「ROP3・パターン」の G1 の emit の順 (単色の塗りと SRCCOPY は GX の道、ほかは `gf3_rop3_set` → `*_emit(GF3_ALU_KEEP)` → 既定へ戻す)、check は SRCKEY・DSTKEY・ALPHA を NOSYS (§11-6 の「色鍵・α」)、`gf_v2_inst.lost` か `g3 == NULL` の間は全部 NOSYS。unregister で F2 が OFF の後に `gf_v2_gart_reset` (§11-6 の「GART (G5)」) | gf3_*・G4〜G9・G11 | 中核 (表) | `gf_v2_inst` の実体、`gf_v2_kcopy` | 340 | I2 |
| G2 | drivers/video/geforce/gf_v2_display.c (mode_set・flip・flip_done・vblank_wait・palette・gamma・cursor_*・dpms・`gf_v2_owns`。LOST の間の mode_set は最初に G11 の `gf_v2_start`。+backend_geforce.c の `gf_hook_scan_changed` の先頭の 1 行 `if (gf_v2_owns(front)) return;`。末尾の追記の G2) | gf4_*・G11 | G1 の表・backend_geforce.c | `gf_v2_inst` の `head[]`・`home`・`pal`・`pal_set` | 300 | I3 |
| G10 | drivers/video/geforce/gf_v2_enum.c (enum_head・enum_conn・enum_mode・edid、末尾の追記の G10) | gf4_ddc_probe・`ddc_read`・G11 | G1 の表 | — | 200 | I3 |
| G11 | drivers/video/geforce/gf_v2_life.c (`gf_v2_start`・`gf_v2_stop`・`gf_v2_disp`・`gf_v2_deadline`・`gf_v2_rc`) と G4 の追補 `gf_v2_fence_reset` (gf_v2_fence.c)。A0 の追補 (gf_v2.h・gf_v2_weak.c・gfx2_wiring.txt) を最初の commit で入れる。末尾の追記の G11 | G5 (`gf_v2_gart_start`)・G8 (`gf3_range_query_set`)・gf2_fence_*・層 4 の `links->failed` | G1 (表の stop・geforce_init)・G2・G10 | `gf_v2_inst` の `g3`・`ch`・`epoch`・`lost` | 130 | I2 |
| G4 | drivers/video/geforce/gf_v2_fence.c・hwdev/gf/accel_fill.c・accel_copy.c・accel_scale.c (*_emit の切り出し)。追補 (G7 の前): `*_emit` の `alu == GF3_ALU_KEEP` で `gf3_rop_set` を飛ばす分岐 (accel_fill.c・accel_copy.c) と、A0 の追補 (hwdev/gf/accel.h の `GF3_PAT_MONO`・`GF3_PAT_COLOR`・`GF3_ALU_KEEP` の 3 行、accel.h の `gf3_ckey_set`・`gf3_alpha_set` の宣言と drivers/video/geforce/gf_v2_weak.c のその weak と tools/checks/gfx2_wiring.txt の `I2 geforce gf3_alpha_set`・`I2 geforce gf3_ckey_set` の削除) | gf2_fence_* | G1・C3 経由・G11 (`gf_v2_fence_reset`、G11 の段が足す) | `ring`・`completed`・`failed_upto` | 220 | I2 |
| G5 | drivers/video/geforce/gf_v2_gart.c (§11-6 の「GART (G5)」) | `gf_inst_alloc/free/write32`・`gf2_ramht_add`・`gf2_fence_emit/wait`・`gf2_fifo_pause/resume` | C6 経由 (G1 の表)・G8 (`range_q`)・G11 (`gf_v2_gart_start`)・G1 (`gf_v2_gart_reset`) | `struct gf_v2` の `gart` | 200 | I1 |
| G7 | hwdev/gf/accel_rop3.c (`gf3_rop3_set`、§11-6 の「ROP3・パターン」の表の順) | gf2_emit・`gf3_format_get` | G1 | — | 120 | I2 |
| G8 | hwdev/gf/accel_surface.c (`gf3_surface_check` の GART の分岐と `gf3_range_query_set`、`gf3_surface_set` の DMA の handle の結び) | `c->range_q` (G5 の `gf_v2_gart_range` を G11 が設定) | gf3_* | `gf3_ctx.range_q`・`range_opaque` | 110 | I2 |
| G9 | drivers/video/geforce/gf_v2_alloc.c | `gf_vram_reserve/release` | C4 経由 | `node[]`・`surf.priv` | 120 | I1 |
| U1 | userland/lib/gfx/v2/*.c (format.c を共有) | 公開の gfx2_* | アプリ | — | 400 | I2 |
| D1 | assets/gfx/sync.cfg (新規) と build/core.yaml の 1 項目 (guest `/etc/gfx/sync.cfg`、tags `[gui]`)、既定の中身の host 試験 (末尾の追記の D1) | — (Y1 の sync_cfg.c を host で組む) | Y1 | — | 30 | I3 |
| K1 | platform/pc98/display_dual.c の GF の行と gf_outputs.[ch] の削除 (CONSOLE_DUAL_DESIGN.md:67・167。CONSOLE_DUAL の D2 と C1・C11 が main に入った後): display_dual.c の GF の行を gfx2 の列挙 (`gfx2_adapter_count/info`・`gfx2_head_info`・`gfx2_connector_info`) に差し替え、gf_outputs を消し、`CON_DUAL_MAX_*` を GFX2 の定数に | C1・C11 | console_dual.c | — | −40 | I3 |
| I1 | 面の道: C1・C3〜C7・C15〜C18・M1〜M3・X1・G5・G9 | 確保 → 写像 → lock → 解放 → AS の回収 (隔離を含む)。合格は §13 の I 列が I1 の試験全部 (**新帯の往復 `gfx2_map_selftest` と AS の回収を必ず含む**) + `gfx2_no_weak.sh I1` | | | 結線 | |
| I2 | 描画の道: C2・C8〜C10・C14・S1〜S11・G1・G4・G7・G8・G11・U1 | 混在の束・fence・v1 の橋・libos32gfx。合格は I 列が I2 の試験全部 + `gfx2_no_weak.sh I2` | | | 結線 | I1 の後 |
| I3 | 表示の道: C11〜C13・G2・G10・P1・T1・Y1〜Y4・D1・K1 | 列挙・mode・flip・全画面の出入り・同期層。合格は I 列が I3 の試験全部 + `gfx2_no_weak.sh I3` | | | 結線 | I2 の後 |
合計 カーネル約 7,200 行・ユーザーランド約 400 行。1,000 行を超えるファイルは無い。ATI の backend は別の設計。

- **順の制約**: exec/lease.c は M3 と C14 の両方が触るので M3 → C14 の順にする (同時に走らせない)。
- **PPC で組めること (完了条件)**: C14・M1・M2・M3 (exec/lease.c) と gfx/v2・gfx/sync の全 TU は PPC のクロスコンパイルが通ることを完了条件にする。`gfx/gfx_hw_submit.c`・`gfx/gfx_vram.c`・`mm/paging.c`・`mm/pgalloc.c`・`exec/lease.c` は `tools/ppc/selected_success.json` の PPC の初回成功集合に入っており、gfx/v2・gfx/sync の新しい TU も A0 がこの集合に足す。gfx/v2・gfx/sync は pc98 と x86 のヘッダを include しない (CPU に依る所は §10 の arch の口、表示の機種に依る所は provider と backend の表)。x86 の asm・pc98 のヘッダを既存の TU に増やさない、増やすなら `tools/ppc/known_header_dependencies.json` に組を足す。

### 12-1. 段ごとの失敗・入力の境界・試験
観点は (1) 失敗の戻り値と失敗の後の状態、(2) 入力の境界 (0・あふれ・整列・上限)、(3) §13-1 の試験の行。どの段も「失敗では共有の状態を変えない (全部か無し)」が既定で、例外だけを書く。装置や写像の後始末が失敗した物は §7-3 の KEEP と (5) の隔離の 1 つの道へ寄せる。手本と同じ所は手本の名だけを書く。

| 段 | (1) 失敗と後の状態 | (2) 入力の境界 | (3) §13-1 |
|---|---|---|---|
| C1 | `gfx2_adapter_register`: INVAL・FULL は表を変えない (検査を書く前に済ませる)。隔離の owner の失敗は §8 の行 (登録は 0)。`gfx2_sync_publish`: INVAL は `sync_tab` を変えない。`gfx2_caller_get`: INVAL は `*c` を変えない | adapter の番号 `>= GFX2_MAX_ADAPTERS` は `gfx2_adapter_get` が NULL。publish は `kind` が 1〜`GFX2_KIND_MAX`、`head < 2` | module・shim・init の 2 回目・sync の判定・隔離の owner |
| C2 | `gfx2_fmt_info` は INVAL、`gfx2_from_argb` の INDEX8 は NOSYS。状態は無い | `fmt` は 1〜10。PLANAR4 (0)・3D の形式・11 以上は INVAL | 形式の式・形式の番号 |
| C3 | `cpu_begin`・`quiesce`・`fence_wait_k` の TIMEOUT・STALE は面を変えない (TIMEOUT の後の LOST は §6-3)。commit の失敗は §11-2 のコード | 折り返しは §6-1 | fence の折り返し・失敗の完了・fence の順・cpu_end の吐き出し |
| C4 | `gfx2_surf_new` は作った順の逆に戻す (heap か GART か SYSMEM → `be->alloc` → `be->kva` → GART の写像 (§8 の「CPU から」))。表が満杯は NOSPC。destroy は §7-3 の表 | `w`・`h` は 1〜caps の `max_w`・`max_h`。`pitch = (w·cpp + pitch_align − 1) & ~(pitch_align − 1)` と `bytes = pitch·h` が u32 であふれる・`pitch > max_pitch` は INVAL。`fmt` は caps の `fmt_surface`、`pool` は 1〜3、`usage` の未定義の bit は INVAL | 状態遷移・作成の途中の失敗・解放の順 |
| C5 | §7-6 | §7-6 | heap |
| C6 | §8 の「C6 の作成の順と失敗」「解放」 | `n == 0` は INVAL、上限は `GFX2_GART_KB` | GART の作成の失敗・unbind の失敗 |
| C7 | `gfx2_map`・`gfx2_unmap` は §11-2 の行。lock は失敗で IDLE のまま (staging は §7-5) | lock の flags は READONLY・DISCARD・DONOTWAIT の外の bit が INVAL | 写像・DISCARD・同期の面の lock・staging の失敗 |
| C8 | 判定だけで状態は無い | `op` が 0 か `GFX2_OP_MAX` を越える・形式の番号が範囲外は INVAL。規則で禁止の組は NOSUPPORT (§4-2) | check_op と submit・GART への SW・LOST の後 |
| C9 | §5-3 (元から足りていた) | §5-3 | 束の写し・submit の検査・パターン・R9 |
| C10 | `gfx2_caps` は失敗で `*o` を書かない。`gfx2_caps_refresh` は §11-2 | `size` は 4 以上 (未満は INVAL)。`min(size, 256)` バイトだけ書く | caps の size |
| C11 | INVAL は `*o` を書かない | head は `a->heads` 未満、conn は `connectors` 未満、mode の `i` は `gfx2_mode_count` 未満、EDID の `block` は `edid_blocks` 未満。外は INVAL | 列挙の範囲 |
| C12 | §9-3 (元から足りていた) | §9-3 | quiesce・mode_set の途中の失敗 |
| C13 | flip の失敗は `pend_sid`・scan_ref を変えない。palette の失敗は `gfx2_pal` を変えない。present の `gfxsync_write_rows` の失敗は IO で `dirty` を残す (次の present でまた送る) | palette は `count >= 1` かつ `count <= 256 − first`。cursor は ARGB8888 で `cursor_w`・`cursor_h` 以内。present の矩形は面で切り、空なら 0 で何もしない | 走査面の解放・palette の範囲・present の失敗 |
| C14 | `v1_bridge_begin` の rc は記録だけで、v1 の入口は進む (TIMEOUT の後は adapter が LOST で待つ物は失敗の完了、BUSY は DIRECT の別名の lock の間で unlock が面の全体を書き戻す)。rebind は §6-5 (quiesce が TIMEOUT の役は lock も閉じず旧のまま、ほかは `gfx2_lock_revoke` → `alias_update`)。`gfx2_lock_revoke`: ALIAS でない面は INVAL で何も変えない、unmap の IO でも map_as・staging を外して IO | — | 別名の面・別名の更新・lock 中の rebind (lock した DIRECT の別名で mode を替える → 旧 sid の unlock は STALE、map_as・staging が無い、staging の頁は持ち主へ返った) |
| C15 | §7-3 (別名の `map_as` の拾いを足した) | — | 回収・unmap の失敗・staging の回収 |
| C16 | shim は §11-7。`gfx2_lost_be` の `gart_unbind` は 0 | — | shim |
| C17 | §7-4 (元から足りていた) | §7-4 | 監査との共存 |
| C18 | §7-2 (元から足りていた) | bytes は C4 が確かめた後 | SYSMEM の往復 |
| S1〜S11 | 入力は C9 の走査 1 で確かめ済み。クリップの後が空なら 0 で何も書かず `touched` は空 | 0 の幅・高さは空 | sw の op ごと (元から足りていた) |
| M1 | 全部 irq_save の中で全件か無し (§11-4)。`ledger_transfer_pinned` の `to` は `owner_ram` (pgalloc_owner.inc:14) を満たすこと | `n == 0`・頁の範囲のあふれは 0 (`pgalloc_free_n_owner` の `pfn_span_ok` と同じ)。pin が 255 を越える頁を含む・pin が 0 の頁の unpin は 0 で何も変えない | pin・pin の上限 |
| M2 | §7-4 の `paging_gfx2_map` の表 | §7-4 の表 | paging_gfx2_map |
| M3 | 範囲の書き換えだけ (元から足りていた) | — | 監査との共存 |
| X1 | §7-3 (元から足りていた) | — | 回収 |
| Y1〜Y4 | 設定の読みの誤り・範囲外は区画を捨てて既定 + kprintf (§10 の表と同じ扱い)。`gfxsync_bind` は FULL・INVAL で ctx を取らない。`write_rows`・`read_rows` は範囲外を INVAL で 1 バイトも書かない (present.c:18-25 と同じ検査)。`flush` は hook の rc。`unbind(NULL)` は何もしない | `unit` は 1・2・4、`gap_ns` は 0〜100000、`bind` の `va`・`bytes`・`pitch` が 0 は INVAL、矩形は `y + h` と `x + w` の u32 のあふれ込みで `bytes`・`pitch` に収める | 同期層・行の範囲 |
| T1・P1 | `gfxsync_bind` の失敗は v1 の init の失敗にし、今の pc98 への落ち (gfx_core.c:227-231) の道に乗せる | — | 同期層 (trident の 65・7・38 の場合を移す) |
| G1・G7 | emit の中の状態の設定の失敗は装置の失敗 (§11-6 の R8)。`gf3_rop3_set` は引数の誤りと 32bpp では何も積まない。G1 は `*_emit` の rc に関わらず既定へ戻す | `kind` は 0〜2、`fmt` は INDEX8・565 (ほかは EUNSUPPORTED・EFORMAT) | channel の既定・fence の順・パターンの詰め方・ROP3 の method の順 |
| G2 | mode_set の失敗は層 4 が退避した状態へ戻す (`gf4_change_end`、§9-3)、`home` は失敗で記録しない。flip の失敗は `head[]` を変えない。palette は最初の失敗の rc を返し `pal` は書いた後 (次の mode_set で入れ直す)。層 4 の窓口が閉じている (`gf_v2_disp() == NULL`) は IO | head は 2 未満、conn_mask はちょうど 1 bit で `c->nroutes` 未満、その route の `head_mask` に head が無ければ INVAL。dpms は 0〜3、cursor の show は 0・1、cursor の面は ARGB8888 で `kva` が非 NULL | mode_set の途中の失敗・GF の戻り先・GF の flip・GF の palette の入れ直し・v1 の走査を変えない |
| G10 | 列挙は書き先を失敗で書かない。DDC の失敗は EDID 無し (status は下の表) | head は 2 未満、conn は `c->nroutes` 未満、fmt は 1〜10、i は数えた件数未満、EDID の block は 0〜255 | GF の接続の種類と状態・GF の mode の列挙 |
| G11 | start の失敗は `lost = 1` のまま (層 3・4 は持ち主に任せ、何も戻さない)。stop の drain の失敗は層 4 の `links->failed` へ渡し、channel が OFF なら 0、OFF でなければ IO (中核が LOST_HELD) | — | GF の stop・GF の start |
| G4 | §6-3 (元から足りていた)。KEEP の追補は §11-6 | — | 輪の満杯・KEEP |
| G5 | bind・unbind は §8 の「物理の表」と §11-6 の「GART (G5)」。start の途中の失敗は取った inst を返し resume してから rc (全部か無し)。reset は失敗しない | `pfn == NULL`・`n == 0` は INVAL。`n > GF_V2_GART_PAGES`・入る隙間が無い・記録の枠の満杯は NOSPC。unbind は記録と完全に一致する時だけ。range は 1 つの bind の中、`off + bytes` のあふれ込みは ERANGE | GART の範囲・GART の頁表 |
| G8 | §11-6 (元から足りていた) | `off + bytes` のあふれ込みは ERANGE | GART の範囲 |
| G9 | `gf_vram_reserve` の失敗は rc をそのまま返し `priv` を変えない。release の知らない予約は INVAL | — | 状態遷移 (C4 の作成の途中の失敗で見る) |
| U1 | 中核の rc をそのまま返す。format.c は C2 と同じファイル | C2 と同じ | 形式の式 (元から足りていた) |
| D1・K1 | 失敗の道は無い (sync.cfg が無ければ Y1 が既定)。K1 は adapter が 0 なら GF の行を出さない | — | D1: sync.cfg の既定の中身 |

## 13. 受入 → 試験

### 13-1. host (libc 無し、各段のファイルを直に組む)。変異は「製品の行」を 1 つ変えて RED になること
| 受入 | 入力 → 観測 | 製品の行 (変異) | I |
|---|---|---|---|
| ABI | 全公開の構造体の sizeof・offsetof が §4-1・§5-1・§9-1 と一致 | os32_gfx2.h の HeadInfo の reserved[6] を [5] に | I1 |
| 形式の式 | 565・1555・8888 の全値の往復、YUV (16,128,128)→黒・(235,128,128)→白・(81,90,240)→赤 ±2 | format.c の 5→8 の `x>>2` を外す / YUV の `+128` を外す | I2 |
| fence の折り返し | last_write=0xFFFFFFF0・last_read=0x10・completed=0xFFFFFFF8 → 書く前の待ちが 0x10 を待つ | fence.c の順の待ちを max に / `>=` を `>` に / 0 を飛ばさない | I1 |
| 失敗の完了 | fake の epoch の変化 → 未完了の書きの先が LOST・lock が STALE・gen +1 | fence.c の failed_upto の比較を外す | I1 |
| 輪の満杯 | fake で 256 件未完了 → 257 件目で kick と最古の wait が呼ばれ、e[tail] が上書きされない。wait が TIMEOUT → submit の rc が TIMEOUT・adapter が LOST | gf_v2_fence.c の `r->count == GF_V2_RING` の分岐を外す | I2 |
| 束の写し | ops[0] = READBACK で data を &ops[1] に向ける → ops[1] は元の内容で実行、done・fence は最後に書いた値 | submit.c の copy_from を外し ops を直に読む | I2 |
| submit の検査 | n=0 → 0・done=0、fence の出力が書けない → INVAL・何も実行しない、走査 1 の 3 本目 INVAL → 何も実行しない、走査 2 の 3 本目で装置の失敗 → done=2 | submit.c の走査 1 の `goto out` を `break` に | I2 |
| パターン | SET_PATTERN の前の PAT → INVAL。色パターン 0x12345678 を INDEX8 に → 0x78、565 に → 0x5678 | submit.c の未設定の検査を外す / 下位を採るマスクを 8bit 固定に | I2 |
| 状態遷移 | §7-1 の表の全セル | surface.c の LOCKED_RO の書きの拒否を外す | I1 |
| DISCARD | fake の未完了の FILL → DISCARD の lock が last_write を待つ | lock.c の DISCARD の時の待ちを省く | I1 |
| 走査面の解放 | scan_ref=1 で free → CLOSING、flip_done の後に空 | surface.c の scan_ref の検査を外す | I3 |
| quiesce | fake の wait が TIMEOUT → mode_set は TIMEOUT、`be->mode_set` は呼ばれず、head・世代は不変 | modeset.c の `if (rc) return rc;` を外す | I3 |
| mode_set の途中の失敗 | 有効な mode で fake の `be->mode_set` が IO → 走査面・scan_ref・世代・`gfx_policy_reeval` の回数が不変 | modeset.c の失敗の return を世代の更新の後に動かす | I3 |
| pin | pin の頁で free・transfer・reclaim が全部拒否、transfer_pinned だけ通る | pgalloc_owner.inc の 3 つの検査を 1 つずつ外す | I1 |
| 回収 | GPU が終わらない → GART の頁が隔離の owner へ移り、owner_release は 0、AS の owner の頁数は 0。写像を外せない → IO → X1 が poison | owner_release.c の quarantine を destroy に / X1 の分岐を外す | I1 |
| 写像 | 3 区間の 2 つ目で失敗 → 1 つ目も外れる。外した後に頁の持ち主は不変 | lock.c の巻き戻しを外す | I1 |
| 別名の面 | 画面の持ち主は sid を得て submit 可、他の AS は INVAL、`v1_bridge_rebind` の後に古い sid は STALE | surface.c の SCREEN の例外を「誰でも」に | I2 |
| 同期の面の lock | DIRECT の別名の lock の va が gfx2 の帯の staging の写像 (VRAM の VA でも kmalloc の番地でもない)、unlock で write_rows に面の全体。SHADOW の別名と VRAM の別名の lock は INVAL | lock.c の staging の分岐を外す | I3 |
| check_op と submit | 全 op × 形式 × pool で how が同じ | submit.c が check_op でなく caps2d を見る | I2 |
| 同期層 | 設定の欠け・範囲外・hook の欠けで既定、gap の回数が今の trident の値 (65・7・38 の場合) と同じ | sync_bind.c の組み合わせの拒否を 1 つずつ | I3 |
| module | 古い世代の module を拒む・`v2=NULL` で動く (shim)・module の表の .text の外のポインタを拒む | backend_reg.c の abi の比較を外す | I1 |
| 隔離の owner (C1) | fake の `ledger_owner_new` が 0 → register は 0・ACTIVE・`gfx2_quar_owner == 0`。GART の面の隔離が IO・`gart_moved == 0`・頁は `as->owner` で pin のまま。2 回目の register で fake が成功 → `gfx2_quar_owner != 0` | backend_reg.c の失敗で `return` する形に / 再試行の `!gfx2_quar_owner` の条件を外す | I1 |
| 形式の番号 (C2) | fmt 0・11・0x40 → INVAL | format.c の範囲の検査を外す | I2 |
| cpu_end の吐き出し (C3) | fake の `arch_cpu_serialize`・`gfxsync_flush` の回数: VRAM の面の W で serialize 1・R で 0、DIRECT の別名の W で flush 1、SHADOW の別名の W で dirty が足され flush 0、SYSMEM・GART で 0 | fence.c の VRAM の分岐を外す | I1 |
| 作成の途中の失敗 (C4) | fake の `be->alloc` が NOSPC → heap の空きが元のまま (`n`・区間が同じ)・表の空きが同じ。`w·cpp` の pitch が u32 であふれる w・h → INVAL | surface.c の巻き戻しの `gfx2_heap_free` を外す / bytes のあふれの検査を外す | I1 |
| 解放の順 (C4・C6) | fake が呼ばれた順を記録: GART の面の free → `paging_lease_unmap` → `be->release` → `gart_unbind` → `pgalloc_unpin` → `pgalloc_free_n_owner`。LOST_HELD では gart_free が BUSY で何も呼ばない | surface.c の `be->release` を `gfx2_gart_free` の後に動かす / gart.c の unbind を unpin の後に | I1 |
| heap (C5) | init(0x1000, 0x10000) → alloc(0x1000, 0x1000) = 0x1000、alloc(0x100, 0x4000) = 0x4000 (頭の端数 0x2000〜0x4000 が空き)、全部 free → `n == 1` で全体。align 0・3 と bytes 0 → INVAL。同じ off の 2 回目の free・使用中でない off → INVAL で不変。区間が 2048 に届く分割 → NOSPC で不変。init(0xFFFFF000, 0x2000) → INVAL、init(x, 0) → alloc が NOSPC | vram_heap.c の後ろとの結合を外す / 二重の free の検査を外す / 上限の先の数え上げを外す | I1 |
| GART の作成の失敗 (C6) | fake の pgalloc が 3 頁目で 0 → 取った 2 頁が返り owner の頁数が元へ、NOSPC。fake の `gart_bind` が NOSPC → pin が全部 0・頁が返る・pfn の表が元のまま | gart.c の bind の失敗の巻き戻しから unpin を外す | I1 |
| unbind の失敗 (C6) | fake の `gart_unbind` が INVAL → `gfx2_gart_free` が IO・pin と頁は不変、destroy が KEEP を立てて BUSY、C15 で隔離・drain が返さない | gart.c の unbind の rc を見ずに進める | I1 |
| GART の頁表 (G5) | fake の instmem (配列) と fake の F2 (RUN・epoch 1)。start → +0 が `0x0002103d`・+4 が `0x07ffffff`、RAMHT に `GF3_HANDLE_GART_DMA` が inst の off で 1 件、pause と resume が 1 回ずつ。A (4 頁、pfn 0x111・0x222・0x333・0x444) の bind → `gpu_off == 0`・+8〜+20 が `pfn << 12` と 3 の OR。B (1 頁)・C (2 頁)・D (1 頁) を続けて bind → 4・5・7 頁目。A と C を unbind して 2 頁の bind → 小さい隙間の 5 頁目 (`gpu_off == 0x5000`)。4 頁の bind の 3 頁目の書きを fake で失敗 → 書いた 2 本が 0・記録が増えない・rc は IO。unbind の `gpu_off` か `n` の違いは INVAL で PTE と記録が不変。range: 1 つの bind の中は 0、2 つの bind にまたがる・`off+bytes` のあふれ込みは ERANGE、handle が VRAM_DMA は EINVAL。fake の F2 を epoch 2 に → range が ERANGE・bind が IO・unbind は書かずに 0。もう一度 start → 新しい頁表に残りの記録の PTE が全部書かれる。reset → 記録 0 件・epoch 0 | gf_v2_gart.c の失敗の時の項目の消去を外す / 最小の隙間の選びを最初の隙間に / start の書き直しを外す / 生きているかの判定から epoch の比べを外す | I1 |
| staging の失敗 (C7) | DIRECT の別名の lock: fake の pgalloc が 0 → NOSPC・IDLE・写像なし。DISCARD の lock の staging が全部 0 (前に 0xCC を置いた頁)。unlock で fake の unmap が IO → IO・IDLE・staging の欄が 0・頁は caller の owner のまま。正常の unlock の後、caller の owner の頁数が lock の前と同じ | lock.c の DISCARD の 0 埋めを外す / unlock の `gfx2_staging_drop` を外す | I1 |
| staging の回収 (C15) | 画面の持ち主の AS が DIRECT の別名を lock したまま終わる → owner_release が別名を拾い、`map_as != as`・staging の頁が返り・別名は IDLE で KEEP が無い。unmap が IO → 頁は返らず rc は IO | owner_release.c の `s->map_as == as` の拾いを外す | I1 |
| caps の size (C10) | size 8 → 8 バイトだけ書き残りは番兵のまま、size 3 → INVAL、size 300 → 256 バイト | caps.c の `min(size, 256)` を 256 固定に | I2 |
| 列挙の範囲 (C11) | `gfx2_mode_get` の i == count → INVAL で `*o` は番兵のまま、head == heads → INVAL | enum.c の i の範囲の検査を外す | I3 |
| palette の範囲・present の失敗 (C13) | palette(first 250, count 7) → INVAL・`gfx2_pal` 不変、count 0 → INVAL。fake の `gfxsync_write_rows` が IO → present は IO・dirty が残り、次の present で同じ矩形を送る | display.c の `256 − first` の検査を外す / 失敗の時に dirty を 0 にする | I3 |
| 行の範囲 (Y3) | bind の bytes 4096・pitch 64 に `write_rows(0, 63, 16, 2)` → INVAL で fake の VRAM が無変更。`x + w` のあふれ込み → INVAL | sync_write.c の境界の検査を外す | I3 |
| pin の上限 (M1) | 同じ頁を 255 回 pin → 256 回目は 0・数は 255。pin が 0 の頁を含む unpin → 0 で全部不変。`ledger_transfer_pinned` の `to` が DEVICE の owner → 0 | pgalloc_owner.inc の 255 の検査を外す | I1 |
| paging_gfx2_map (M2) | 帯の外・頁境界でない va・`npages == 0`・帯の末尾を越える npages・pa のあふれ・`PTE_PS` を含む flags・`PTE_USER` の無い flags → −1。既に present の PTE を含む範囲 → −1 で PTE 不変。PT が要る 2 つの PDE の 2 つ目で PT が取れない → −2・PDE と PTE が全部不変・1 つ目に取った PT は返る | paging_gfx2.inc の present の PTE の検査を外す / 失敗の時の PT の返却を外す | I1 |
| H1/H2 の回帰 | tools/tests/gfx_accel_hal_host.c が無変更で通る | — | I2 |
| fence の順 (P1-1) | fake の backend が呼ばれた順を記録。FILL(hw) → 記録が `emit(f)` → `fence_emit(f)` の順、`issued` は fence_emit の 0 の後だけ f。続く SW の FILL の cpu_begin が f を待つ。emit が IO → `failed_upto = f`・書き先が LOST・`issued` 不変 | submit.c で commit を emit の前に動かす / fence.c の `issued = f` を fence_emit の前に | I2 |
| sync の判定 (P1-2) | fake の ctx を DIRECT で publish → lock が staging・check_op が HW 可・present が NOP。SHADOW で publish し直す → lock は影・check_op が SW。publish(NULL) の後 → NOTFOUND で VRAM の面の扱い。C7・C8・C9・C13 の 4 か所が同じ答え | backend_reg.c の `gfx2_sync_get` が mode を写しの値で返す (publish し直しを見ない) | I3 |
| GART の範囲 (P1-3) | `range_q` 未設定で GART の面の `gf3_surface_check` → EINVAL。設定後: bind 済みの [0,8KB) は 0、[4KB,12KB) は ERANGE、`off+bytes` のあふれ込みは ERANGE。VRAM の面は今の結果と同じ | accel_surface.c の GART の分岐を予約の照合へ戻す | I2 |
| 別名の更新 (P1-4) | `gfx2_surf_alias_update` で gen+1・旧 sid が STALE・kva は渡した値・heap/GART/`be->alloc`/`be->release` の呼び出しが 0 回。lock 中は BUSY で何も変わらない。`gfx2_surface_free(別名)` は INVAL | surface.c の update で gen+1 を外す / destroy の ALIAS の拒否を外す | I2 |
| unmap の失敗 (P1-5) | fake の `paging_lease_unmap` が失敗 + `be->release` が BUSY → owner_release は IO、全部の面で `map_as != as`・`owner != as->owner`・vaspace の枠が空、KEEP の面の backing が heap/GART に戻らない。その後 as を 0xCC で埋めて free し、CLOSING の見回りと drain を回しても as を読まない (host の番兵) | owner_release.c の `gfx2_map_detach` を外す / destroy の IO で KEEP を立てない | I1 |
| .text の検査 (P1-6) | module の表は fake の `module_record` の text の中で 0、1 つを外にすると INVAL。組込みの表 (`v2_mod == NULL`) は abi と非 NULL だけで 0、hook を 1 つ NULL にすると INVAL | backend_reg.c の `module_text_contains` の呼び出しを外す / 非 NULL の検査を外す | I1 |
| 監査との共存 (R1) | fake の AS に lease 1 本と gfx2 の写像 (VRAM 2 頁・GART 3 頁) を張る → `lease_check` と `gfx2_map_audit` が 0。gfx2 の帯の PTE を 1 つ書き換え → `gfx2_map_audit` が −1・`lease_check` は 0。写像の無い PTE を帯に 1 つ足す → −1。lease の base を `MEM_LEASE_ALLOC_END` に → `lease_check` が INVAL | lease.c の PTE の走査の上限を `MEM_LEASE_MAX_PDES` に戻す / map_audit.c の数え上げの比較を外す | I1 |
| SYSMEM の往復 (R2) | SYSMEM の 64×64 XRGB8888 を作る → 頁が持ち主の owner に連続で n 頁・`kva` が P2V・中身 0 → lock の va に書いて unlock → sw の READBACK が同じ値 → free で owner の頁数が元へ。pgalloc の空きを fake で 0 → NOSPC・表は不変。AS の回収 (unmap が IO) → KEEP・頁は `as->owner` のまま・drain で返らない | sysmem.c の free を外す / owner_release.c の KEEP の飛ばしを外す | I1 |
| GART への SW (R3) | GART の面 (fake の pfn を飛び飛びに 3 頁) を作る → 持ち主の AS の文脈で SW の FILL が `map_va` 経由で 3 頁全部に正しく書く (pfn ごとの頁を直に見て確かめる)。別の AS の文脈 (`gfx2_cpu_visible` が 0) では check_op が NOSUPPORT・submit が NOSYS | check_op.c の (5) の可視の判定を外す (`kva` だけを見る) / sw の base を pfn[0] の P2V に | I1 |
| shim (R4) | Trident・PC-98 の fake の v1 (v2 == NULL) を登録 → `a->be == &gfx2_v1_shim`。SYSMEM の面へ FILL の束 → SW で終わり `kick`・`emit` が呼ばれない、fence_query が completed == issued。unregister の後 → `a->be == &gfx2_lost_be`、submit は STALE、NULL の参照が無い (host の番兵) | backend_reg.c の shim の据え付けを外す (be = v1->v2 のまま) | I1 |
| init の 2 回目 (R5) | `gfx2_adapter_register(GF, ACTIVE)` を 2 回 → adapter は 1 つ・gen と面は不変。GF の init の失敗を模して `register(pc98, ACTIVE)` → GF の ACTIVE が外れ gen+1・GF の heads の scan_ref が 0・別名が pc98 の adapter へ移り旧 sid は STALE・GF の面への submit は NOSYS、free は通る。`gfx_shutdown` の模し (GF → pc98) も同じ結果 | backend_reg.c の `adapter_by_v1` の照合を外す (毎回新しい枠) / activate の旧 ACTIVE の外しを外す | I1 |
| LOST の後 (R6) | fake の装置の失敗で LOST → `gfx_policy_reeval` が 1 回呼ばれ、fake の `direct_ok` が 0 で v1 の hook が SHADOW。v2: SYSMEM と VRAM (kva あり) の面への FILL は SW で書け `be->emit` は 0 回。自動では戻らない (fence_query・vblank を回しても LOST)。`gfx2_mode_set` の成功で OK に戻る | fence.c の `gfx2_adapter_lost` の reeval を外す / check_op.c の (2) を外す (LOST でも backend の check を呼ぶ) | I2 |
| channel の既定 (R8) | fake の GF の channel の記録で、v2 の色パターンの FILL (kind 2) → 続く v1 の `gf3_fill` (GXxor) の時の PATTERN_SELECT が MONO・パターン全 1。`*_emit` が INVAL を返した時も戻しが積まれる | G1 の emit の戻しを外す / INVAL の時に戻さない | I2 |
| ROP3・パターン (G7) | fake の channel の記録: mono kind の 565 → 0x0000 PATTERN・0x0300 = 1・0x030c = 1・0x0310 の 4 語 = {bg\|0xffff0000, fg\|0xffff0000, mono[0], mono[1]}・0x0000 ROP・0x0300 = rop3・subc の 0x02fc = 1 の順。INDEX8 の kind 2 → 0x0400 から 16 語 (color[0..3] が 1 語目の bits 0-7・8-15・16-23・24-31)。32bpp → 何も積まず EUNSUPPORTED。G1 の詰め方: mono 8B {0x80,0,0,0,0,0,0,0x01} → mono[0] = 0x80・mono[1] = 0x01000000 | accel_rop3.c の 0x030c の値を kind で分けない / 4b の詰めを上位から / G1 の詰めのシフトを逆に | I2 |
| GF の KEEP (G4 の追補) | `gf3_fill_emit(.., GF3_ALU_KEEP, ..)` → 0x02fc・ROP・パターンの method が 1 つも積まれず、GDI の色と矩形だけ。alu 0〜15 は今のまま | accel_fill.c の KEEP の分岐を外す | I2 |
| lock 中の rebind (C14・C7) | DIRECT の別名を lock (staging あり) → `v1_bridge_rebind` → 旧 sid の unlock が STALE、map_as・staging が 0、staging の頁が持ち主の owner に返った (owner の頁数が lock の前と同じ)。ALIAS でない面の `gfx2_lock_revoke` → INVAL で不変 | lock.c の revoke の `staging_unmap` を外す (alias_update が BUSY) | I2 |
| NOSUPPORT の戻り値 (R9) | RGB565 → INDEX8 の STRETCH を含む 3 本の束 → `OS32_ERR_NOSYS`・done=0・何も実行しない。欄の誤り (MONO に src) は INVAL | submit.c の NOSUPPORT を INVAL に返す |  I2 |
| weak の検査 (P2-2) | 検査の対象は build/out/kernel.elf と kernel.map。対象の関数を weak だけの版でリンク → `gfx2_no_weak.sh I1` が 1、強い実体のある版で 0。weak の仮の実体と同じ名の強い実体が a0_weak.o の区間に来た版 (人工) も 1 | gfx2_no_weak.sh の型の検査を記号名の検査に戻す | I1 |

sw の op ごと (各 S の host 試験。期待画素は §3-3 の式から手で出した値):
| 段 | 入力 | 期待 | 変異 (その 1 行) | I |
|---|---|---|---|---|
| S1 fill | 4×4 INDEX8 全 0、(1,1,2,2) に 7 | 内側 4 画素だけ 7 | 列の終わりの `<` を `<=` | I2 |
| S2 copy | 1 行 [0..7]、(0)→(2) 幅 6 | [0,1,0,1,2,3,4,5] | 重なりの向きの判定 (`dx > sx` で後ろから) を逆に | I2 |
| S3 rop | ROP3 0x66 (S^D)、D=0x0F・S=0xFF / 0x5A (P^D)、mono P=0xAA の行 | 0xF0 / 交互に反転 | ROP3 の添字 `(P<<2)\|(S<<1)\|D` の P と D を入れ替え | I2 |
| S4 rop4 | mask 0xF0、前景 ROP 0xCC・背景 0x00 | 左 4 画素 = S、右 4 = 0 | mask の 1/0 の意味を逆に | I2 |
| S5 mono | 0xA0・幅 3・fg=1・bg 透過 | [1,_,1] | MSB 左を LSB 左に | I2 |
| S6 hostdata | 2×2 の元、key=3 | key の画素だけ書かない | key の比較を外す | I2 |
| S7 readback | 2×2 の面 | 同じ 4 画素・pitch どおり | 行の進みを w*cpp に | I2 |
| S8 stretch | 最近点 [10,20] 2→4 / 双線形 [10,20] 2→4 | [10,10,20,20] / [10,13,18,20] | 双線形の `− 32768` を外す | I2 |
| S9 ckey | src [1,2,1]・key 1、dst [9,9,9] | [9,2,9] | 範囲の上端の `<=` を `<` | I2 |
| S10 alpha | src 0x80404040 (premult)・dst 0xFF000000 | 0xFF404040 | div255 の `+128` を外す (α が 254 になる) | I2 |
| S11 line | (0,0)→(2,1) | (0,0)・(1,0) だけ | 副軸へ進む `e > 0` を `e >= 0` ((1,1) になる) | I2 |
| GF の stop (G11) | fake の channel が RUN・同じ epoch で drain が通る → 0、`lost` が 1、`links->failed` は 0 回。drain が ETIME → `links->failed` が 1 回、fake が OFF にすれば 0・しなければ IO。epoch が違う・OFF → drain も failed も呼ばず 0 | gf_v2_life.c の drain の成功の `return 0` を外す (成功でも failed が呼ばれる) | I2 |
| GF の start (G11) | epoch が変わった後 → 輪が空・`failed_upto` が輪の最新・`gf3_range_query_set` → `gf_v2_gart_start` の順・`lost` 0。層 3 が ready でない → GF3_ESTATE・`lost` 1・gart_start は 0 回 | gf_v2_life.c の `gf_v2_fence_reset` の呼び出しを外す | I2 |
| GF の戻り先 (G2) | v1 の走査 (head 0 が enabled、G9 の予約でない) の上で v2 の面へ mode_set → `home` に v1 の front・mode id・route の番号・palette、層 4 の set_mode に `{gpu_off, priv[0], priv[1]}`。別名の面で mode_set → 層 4 に home の値、`home.valid` 0、palette を home から入れ直す。v2 の mode_set が失敗 → `home.valid` 0 | gf_v2_display.c の別名の分岐で home の代わりに引数の mode を使う | I3 |
| GF の flip (G2) | 非同期の flip → `pend_sid`、vblank の数が `after` のまま → flip_done は BUSY、数が進む → 0・retired = 前の走査の sid、続けて呼ぶと retired 0 | gf_v2_display.c の数の比べの BUSY を 0 に | I3 |
| GF の palette の入れ直し (G2) | head 0 が 565 の間に palette(10, 2) → 層 4 の palette_set は 0 回・`pal` に残る。INDEX8 へ mode_set → palette_set(0, 256) が 1 回で 10・11 番が写っている | gf_v2_display.c の mode_set の後の入れ直しを外す | I3 |
| v1 の走査を変えない (G2) | G9 の予約の front で `gf_hook_scan_changed` → `selected_front`・`gfx_policy_scan_changed` の回数が不変。v1 の予約の front → 今までどおり更新 | backend_geforce.c の `gf_v2_owns` の 1 行を外す | I3 |
| GF の接続の種類と状態 (G10) | 同じ connector の ANALOG・TMDS の 2 本 → 両方 DVI_I。fake の EDID の byte 20 bit7 が 1 → TMDS が ON・ANALOG が OFF、edid_blocks = 1 + byte 126。DDC が無応答 → TMDS は OFF・ANALOG は UNKNOWN。clone_mask は 0。TV は tv_sense の値 | gf_v2_enum.c の相方の検出を外す (DVI_I が VGA・DVI_D になる) | I3 |
| GF の mode の列挙 (G10) | 2 つの connector の mode のうち head の head_mask に入る物だけ、fmt で絞る。i == 件数 → INVAL で `*o` は番兵のまま。refresh_mhz は 640x480 (25175kHz・800×525) で 59940 | gf_v2_enum.c の head_mask の絞りを外す | I3 |
| sync.cfg の既定の中身 (D1) | Y1 の sync_cfg.c を host で組み、assets/gfx/sync.cfg を読ませる: `pegc` → SHADOW・unit 4・PLAIN・NONE・read 0・SHADOW、`pci:1023:9397` → `pci:1023:*` に落ちて SHADOW・unit 4・GAP・gap 80・NONE・read 1500・SHADOW (key `pci:1023:*`)、`pci:10de:0221` → NOTFOUND と既定 | assets/gfx/sync.cfg の `gap_ns=80` を `gap_ns=8` に | I3 |

### 13-2. NP21/W (PEGC)
- 列挙・能力の出力 (I3)、v2 の sw の 2D の画面の CRC (I2)、GUI ↔ 全画面アプリの出入りで v1 の DISPLAY の lease 中の v2 の投入が BUSY、抜けた後に gshell の画面が戻る (I3)、アプリの終了で面・写像・GART が回収される (台帳の自己検査、I1 の必須)。
- 監査との共存 (R1): v2 の面を lock して写像を持ったアプリが動いている間に `kselftest_run_audit` (kernel/kaudit.c:17-40) を回し、`as_audit_fail` が増えないこと (I1 の必須)。
- 新しい帯の写像の往復: カーネルの自己検査 `gfx2_map_selftest` (paging_map_user_keep_selftest と同じ形) が F8000000 に張り、アプリの文脈から読めて、外した後に PTE が 0・PT が AS の破棄で返ることを見る (I1 の必須。I1 の合格の前に通す)。
- 実行時の RED: 橋 (C14) を外した版で v1 の present と v2 の sw の書きが混ざった時に画面の CRC が変わること (I2)。

### 13-3. 実機 (Ra266、GeForce 6200・Trident)
- GF: op ごとに hw と sw の READBACK を比べる (exact は一致、他は最大差 ≤ §3-3。GART の面 (飛び飛びの pfn) を書き先と読み元にした FILL・COPY と、unbind・再 bind の後の同じ比べを含む、I1・I2)、混在の束 (hw・sw・hw) (I2)、走査中の面の free の後に表示が乱れない (カメラ、I3)、fence の待ちの時間 (I2)。
- Trident: sync.cfg の gap_ns=80 で欠けが 0 (11-08 の掃引と同じ台本)、sync.cfg が無い時は既定で動く (I3)。
- GF の表示 (I3): v2 の面への mode_set (640x480・800x600 の INDEX8・565・X888) が映り、全画面を抜けると gshell の画面と色が戻る (カメラ)。vblank の flip で裂けない、非同期の flip の flip_done が次の vblank で retired を返す、565 の後の INDEX8 で palette が入る、enum_conn の状態が繋いだ monitor と合う (VGA・DVI の差し替え)。

## 14. 未決
- I1 結線時の先行依存: C4 の作成に必須の C10 の能力初期化・照会、C13 のパレットの影、C9 の SW 関数表、Y4 の ctx の記憶域は I1 で強い実体を置く。能力の shim は現在の GfxMode から面の上限を作り、未実装の描画能力は広告しない。描画能力の合成は C10/I2、head の能力は I3 で完成させる。
- I1 の R2・R3・R4 の host 結線は、実際の SYSMEM/GART backing・写像・CPU の読み書き・shim の fence・AS 回収までを組む。表の SW の hook と公開 submit の描画部分は I2 の部品なので、束の FILL/READBACK そのものの受入は I2 に残る。I1 の CPU の道の観測は `test_gfx2_i1.py`、ゲストは `gfx2_i1.bin`。
- `gfx2_map_selftest` は kdebug の有無によらず exec_init の後で一度呼び、結果・回数を kernel.map の同名変数で公開し audit_fail に加える。既存の `paging_lease_unmap` は空の追加 PT をその場で返すため、PTE=0 または PDE/PT が既に除去済みを確認し、AS 破棄後の owner と空き頁数まで照合する。
- 無し。ユーザー判断 (2026-11-10): LOST の後は自動の回復をしない (§6-3)、組込みの表の kernel .text の検査と in_submit の BUSY は持たない (§11-7・§5-3)。PM 決定: #1 遅延確保の上限 8MB (`GFX_GART_KB`)、不足は NOSPC。#2 `TRI_WRITE_GAP` は /etc/gfx/sync.cfg へ (引き継ぎと優先順は配備の手順、既定 80ns)。#3 はユーザー決定「AとC」で確定 (§9-1)。
- GF の HW の色鍵の COPY と α は、nouveau の NV40 と同じく 3D (curie 0x4497、xf86-video-nouveau の nv40_exa.c) で行う別の設計にする (§11-6 の「色鍵・α」)。それまでは sw。
- 将来 #3 を同時出力へ広げる時に直す所 (今は直さない、記録だけ): §9-1 の ACTIVE の 1 個の決まり / C1 の `gfx2_adapter_activate` の ACTIVE を 1 個にする付け替えと `gfx2_sync_publish` の kind ごと 1 つの表 / head 系の口の NOSYS の分岐 (C11〜C13) (C は別票 CONSOLE_DUAL)。

- MONO・HOSTDATA の非同期化は後の最適化。G1 は既存の同期の層 3 の口を呼び、emit の中で完了を待つ (PM 決定 2026-11-11)。

## 反論
- P1-5 の直し案「C7 の明示的な切離し口で map_as/VA 管理を隔離へ移す」のうち「VA 管理を隔離へ移す」は採らない: AS が消えた後の VA の bitmap は意味を持たないので、隔離へ移さず `gfx2_vaspace_drop(as)` で捨てる。backing を返さない (KEEP) ことで PTE が残っても安全側にした。
- P1-1 の「途中失敗の扱い」で、emit が装置の失敗を返した命令も予約の番号を `failed_upto` にして書き先を LOST にする (描画が途中まで積まれた可能性を消せないため)。検査の食い違い (INVAL・NOSYS) だけは何も積んでいない決まりにして番号を捨てる。

## 確定 (2026-11-09、astra の第 4 回、PM が反映)
- F1 (P1、§6-4・§11-3 の同期の ctx): 同期の対象は同期の対象の別名面だけ。SHADOW は `view.base` = RAM の正本・`view.sync = NULL`、DIRECT の対象面だけ `view.sync = ctx`、SHADOW の転送は C13 の present、通常の面は ctx を拾わない。
- F2 (P1、§9-1・C1、決定「AとC」の C): C は別票 CONSOLE_DUAL に切り出し、§9-1 は A と別票の受け持つ経路の列挙だけ。C1・I3・§13-3 から C の仕事を外した。
- F3 (P2、§11-7・§12 T1): A0 の Trident の import の一覧と生成物に `gfx2_sync_publish` を足した。
- F4 (P3、§1 D7): geforce_module.c の init と検査 (:193-230) と load の成功の後の公開 (:307) を分けて書いた。

## 確定の後の追記 (2026-11-10、今の main cfb9cec3 との前提の照らし直し、PM 決定)
- **走査の変化の口 (§1 D3・§9-3)**: G3 (93f80764・6c8bc8d4) が `gfx_policy_scan_changed()` (`gfx/gfx_policy.c:67-76`、宣言 `gfx/gfx_policy.h:14-16`) と `gfx_policy_gen_get()` を足した。§9-3 の `gfx_scan_changed = 1; gfx_policy_reeval();` の 2 行はこの 1 口に書き換えた。module の側の G* から呼ぶ所も同じ口を使う。
- **v2 の写しの位置と ro の検査 (§1 D7・§11-7)**: G3 が `geforce_start` に `direct_ok` の text の検査と `direct_ops` の表が module の ro の中にあるかの検査 (`drivers/video/geforce/geforce_module.c:216-241`) を足し、公開は `:334`。v2 の写しは `:241` の後・`:334` の前に置き、`candidate.v2` の表にも `direct_ops` と同じ「ro の中」の検査を掛ける (今は .text の検査だけの設計だった)。`backend_geforce.c` の表は `:749-767` に移った (行番号だけ)。
- **同期層のコピー (§10・§11-5)**: 第 5 段 A (28b80560) が `gfx/gfx_internal.h:71-78` の `_memcpy_w/_memcpy_d` の rep movs を `include/copy_units.h` → `arch/{x86,ppc}/arch_copy.h` の `arch_copy16/32` に替えた。gfx/sync の新しい行コピーは `arch_copy*` を使い asm を書かない。gfx/sync・gfx/v2 の host 試験の写しの一覧には `include/copy_units.h`・`arch/*/arch_copy.h` を入れる (56b97fc3 の型)。
- **PPC の選択集合 (C14・M1〜M3・gfx/v2・gfx/sync)**: PPC のクロスコンパイルが通ることを完了条件にした。範囲と決まりは §12 の「PPC で組めること」。
- **公開ヘッダの型の契約 (A0)**: 第 4 段 B の `build/abi_contract.mk:4-13` が公開ヘッダ単独の gnu89 の probe と全 C の規則の char/enum の旗の検査を持ち、第 4 段 A の `include/type_contract.h` が ILP32・enum 4B・u64 の member 整列 (x86 4・ppc 8) を固定した。A0 で `sdk/include/os32/os32_gfx2.h` を abi_contract.mk の gnu89 の probe に足す。GFX2_Caps (256B)・GFX2_Op (80B) は u64 を使わず u32 だけで組み、ppc の整列 8 と offset を両立させる。KAPI 83→84 は USB の `usb_stat_get` と先に入った方が 84 (後の方が上げる)。

## 確定の後の追記 (2026-11-10、Fable 5.2 のレビュー、ユーザー判断 2 件と PM 決定)
レビューは `~/os32-tmp/run/hal-v2/REVIEW.fable52.md` (main 2259debc)。ユーザー判断は DECISIONS.md の末尾 (「推奨で良い」)、他は直し案どおりの PM 決定。file:line は 2259debc で確かめた値。
- **R1 (P1、監査との共存)**: `lease_check` (exec/lease.c:167-231) は lease の PT の全 PTE を lease の表と照らす (:212-229)。`lease_audit_all` (:363-、:380) と kernel/kaudit.c:22 から全 AS に回るので、gfx2 の写像は `LEASE_INVAL` に数えられる。M3 が lease の照合を `MEM_LEASE_ALLOC_END` までに縮め (:176-177・:212)、C17 の `gfx2_map_audit` が gfx2 の帯を面の表と照らす (§7-4 のコード、試験 §13-1・§13-2)。
- **R2 (P1、SYSMEM の backing)**: 物理で連続の頁を `pgalloc_alloc_n_owner` (mm/pgalloc.h:228、範囲は drivers/video/trident/resources.c:171 の形) で持ち主の owner から取り、`kva = P2V`、自前の上限は持たず NOSPC。返し方・隔離は §7-2 の表、担当は新しい段 C18 (§12)。
- **R3 (P1、GART の面の SW)**: 頁が不連続なので `kva` は NULL にし、作成の時に持ち主の AS へ連続の VA で張って `map_va` を sw の base にする (§7-2・§8)。CR3 が持ち主の AS でない時は `gfx2_cpu_visible` が偽で NOSUPPORT (§4-2 の (5))。NOSUPPORT にする案は採らない (SW で補う決定 2026-11-08 に合わせる)。
- **R4 (P1、v2 の無い backend)**: `a->be` を常に非 NULL にする。v2 == NULL の backend (Trident・PC-98) は C16 の `gfx2_v1_shim`、unregister の後は `gfx2_lost_be` (§11-7 のコード)。submit の `kick` は HW の命令があった時だけ (§5-3)。
- **R5 (P1、gfx_init のたびの選択)**: backend の選択は gfx_init のたびに走り (gfx/gfx_core_init.inc:8 → gfx_core.c:203-238 → provider の `display_provider_select` (platform/pc98/gfx_selector.c:53-108)、`gfx_init`:295・`gfx_init_200`:300)、init の失敗で pc98 へ落ちる (gfx_core.c:227-231)。`gfx2_adapter_register` は同じ v1 で冪等、ACTIVE の付け替えは `gfx2_adapter_activate` (旧の heads を切り離し gen+1、別名は `v1_bridge_rebind` で移る)。`gfx_shutdown` (gfx_core.c:356-384) の GF → pc98 の代入 (:380) の後にも呼ぶ (§11-7)。
- **R6 (P2、LOST の後、ユーザー判断 1)**: 自動の回復はしない。v1 は `gfx2_adapter_lost` → `gfx_policy_reeval` (gfx/gfx_policy.c:30-59) で `geforce_direct_ok` (backend_geforce.c:121-124) が 0 → SHADOW、v2 は kva のある面を SW で続け、復帰は次の mode_set (§9-3・C1 の再登録) か再起動 (§6-3)。
- **R7 (P2、呼び手の識別)**: `caller_access` (include/redir_access.h:11-16) の 1 回の値から `struct gfx2_caller {app_id, as, owner}` を作る `gfx2_caller_get` (C1) に寄せ、面は `owner`、画面の持ち主は `app_id == gfx_screen_owner()` (gfx_core.c:351-354) で照らす (§6-5・§7-1)。
- **R8 (P2、channel の状態)**: v1 の DIRECT の blit は `gf3_rop_set` だけを結び直す (hwdev/gf/accel_copy.c:54)。G1 の emit は色鍵・α・パターンを変えた命令の後に既定へ戻して返す (§11-6)。
- **R9 (P2)**: 走査 1 で NOSUPPORT の命令がある束は `OS32_ERR_NOSYS` (§5-3)。
- **R10 (P2)**: `gfx2_no_weak.sh` の対象は build/out/kernel.elf と kernel.map (build/config.mk:190 の `-Map=$(BUILD_OUT)/kernel.map`、build/kdebug.mk:25)。
- **R11 (P2)**: caps の装置ごとの埋め方 (§4-1)、sync.cfg の書式と項目と鍵の照合 (§10)、Trident の移し方 (§10) をリポジトリ外の r1 の参照から本文へ移した。Trident の行番号は drivers/video/trident/present.c:18-25・26-56、regs.c:117-138、trident_module.c:198-199、trident.h:581。
- **R12 (P2、CONSOLE_DUAL)**: CONSOLE_DUAL_DESIGN.md:67・167 に合わせ、段 K1 (display_dual.c の GF の行を gfx2 の列挙に差し替え、gf_outputs を消す) を §12 に足し I3 に入れた。票 TASK_HAL_V2 の CONSOLE_DUAL は「確定設計あり」。
- **R13 (P2)**: `SYS_GFXSYNC_CFG` は A0 が include/config.h:41 の次に足す。直列化は中核に置かず、Y3 が arch の口 `arch_cpu_serialize()` を呼ぶ (§10)。
- **R14・R15・R16 (P3)**: 貼り込みの残り 5 か所を消した。backend_geforce.c の表は :749-767、geforce_module.c の検査は :217-253・公開 :334 に直した (§1・§11-7)。caps2d の bit は `GFX2_CAP2D(op) = 1u << op` (§4-1・§11-1)、`gfx2_cursor_image` は呼んだ時に写し切り面の参照を持たない (§9-2、backend_geforce.c:336-347 と同じ)、T1 は write_gap.c の旧パスに空の規則 (§10、96c5835e の型)。
- **過剰 (ユーザー判断 2)**: 組込みの表の kernel .text の検査 (`__os32_text_start`/`end`) と submit の `in_submit` の BUSY は持たない。module の表の .text・ro の検査は残す (geforce_module.c:217-238 と同じ型)。`be->kick` と 4 重の回収は残す。
- **試験の欠け**: §13-1 に R1 (監査との共存)・R2 (SYSMEM の往復)・R3 (GART への SW)・R4 (shim)・R5 (init の 2 回目)・R6 (LOST の後)・R8 (channel の既定)・R9 の行、§13-2 に監査の実行時の行を足した。

## 確定の後の追記 (2026-11-11、A0 の設計の欠け、PM 決定)
- **帯の定数 (§7-4・§11-2・§11-4・§12)**: `MEM_GFX2_MAP_BASE 0xF8000000UL`・`MEM_GFX2_MAP_END 0xFE000000UL` は A0 が include/memmap.h に置く (`gfx2_vaspace` を A0 だけで組むため)。M3 は `MEM_LEASE_ALLOC_END`・`MEM_LEASE_ALLOC_PDES` と exec/lease.c の書き換え。生成の地図は変えず、A0 は check_p2v.py の `USER` に帯を足す。
- **v1 の列挙 (§11-2・§11-7)**: C11 の `gfx2_v1_enum_head/conn/mode` を gfx2_int.h (A0) に宣言。引数は `be->enum_*` の ctx を `const GfxBackend *v1` に替えた形で、C16 の `shim_enum_*` は `a->v1` を渡して呼ぶだけ。
- **caps の入れ直し (§11-2・§11-7)**: `void gfx2_caps_refresh(struct gfx2_adapter *a)` を gfx2_int.h (A0) に宣言、実体は C10。`be->caps` が 0 の時だけ `a->caps` を入れ直し、失敗では変えない。
- `shim_edid` (C16 の static) は v1 に EDID の hook が無いので `OS32_ERR_NOSYS` を返す (§11-7 の「NULL なら NOSYS」と同じ形)。
- **A0 のレビューの直し (2026-11-11、PM 決定)**: build の一覧は gfx/v2・gfx/sync を `$(wildcard gfx/v2/*.c gfx/sync/*.c)` で拾う (A0 の時点で後の段の名前を書けないため。gfx/v2・gfx/sync に置いた .c はすべてカーネルに入る)。`GFX2_OWNER_SCREEN` は `0xFFFFFFFFUL` (ledger の owner の番号は `LEDGER_MAX_OWNERS` 未満なので、どの owner とも重ならない)。`gfx2_quar_owner` は C1 が最初の登録で `ledger_owner_new(LEDGER_KIND_MODULE, …)` で取る (今の ledger は AS・MODULE・DEVICE だけを受け付け、DEVICE は RAM を持てないので MODULE。PERSIST の種別は作らない)。gfx2_* の KAPI の入出力ポインタは、すべて kapi.json で大きさを宣言して生成の検査に通す。

## 確定の後の追記 (2026-11-11、実装の第 1 波の設計の欠け、設計者決定)
- **C1 隔離の owner**: 最初の `gfx2_adapter_register` で `ledger_owner_new(LEDGER_KIND_MODULE, 0, "gfx2-quar", …)`。DEVICE は `owner_ram` (mm/pgalloc_owner.inc:14) が拒み RAM を移せないので MODULE。失敗しても登録は 0・ACTIVE は成り立ち、次の登録で取り直す。0 の間の隔離は移さず IO → 既存の poison の道 (§8)。
- **C3 CPU の書きの吐き出し**: backend の hook は足さない。`gfx2_cpu_end` が VRAM の面は `arch_cpu_serialize()` (nouveau_dma.h:82-85 の `mb()` に当たる)、DIRECT の別名は `gfxsync_flush`、SHADOW は dirty、SYSMEM・GART は何もしない (§6-2)。
- **C4・C6 解放の順と分担**: TTM の `ttm_bo_release` の順 (fence → CPU の写像 → GPU の写像 → GART の unbind → 頁) に 1 つにした。C4 の destroy が `gfx2_unmap` → `be->release` → `gfx2_gart_free` を呼び、C6 の `gfx2_gart_free` が中で `gart_unbind` → unpin → 解放 (§7-3 の表・§8)。口は分けない。
- **C6 bind・unbind の失敗**: `gart_bind` は backend の全部か無し (nvkm/subdev/mmu/vmm.c:750-755)、中核は pin・頁・表を戻してその rc。`gart_unbind` は失敗しない (nouveau の nv04 の unmap は 0 を書くだけ、vmmnv04.c:59-64)、0 以外は呼び手の誤りで IO → KEEP → 隔離 (§8)。`gfx2_lost_be` の `gart_unbind` は 0。
- **C7 DIRECT の staging**: kmalloc でなく呼び手の owner の連続の頁 (§7-2 の SYSMEM と同じ形)、lock から unlock までだけ gfx2 の帯へ張る。unlock は write_rows → cpu_end → unmap → staging_drop (§7-5)。§6-5 の別名の lock の禁止は DIRECT の別名を除く形に書き換え、C15 は `map_as == as` の別名も拾う (§7-3)。`struct gfx2_surf` に `staging_pfn, staging_n, staging_owner` (A0 の追補)。
- **C5 heap**: nvkm_mm の head 側 (nvkm/core/mm.c:47-158) の first-fit・分割・結合。align は 2 の冪、0 の bytes・二重の free は INVAL、区間の上限は先に数えて NOSPC、失敗で不変 (§7-6)。
- **M2 paging_gfx2_map**: `paging_lease_map` (mm/paging_lease.inc:59-133) と同じ検査と全部か無し。flags は PRESENT・USER・RW・PCD・PWT だけ、既存の PTE は −1、PT が取れなければ −2 (§7-4)。
- **全段の洗い直し**: §12-1 に段ごとの失敗・入力の境界・試験の表を置き、§13-1 に足りない試験の行 (隔離の owner・cpu_end・作成の途中の失敗・解放の順・heap・GART の失敗・staging・caps・列挙・palette・行の範囲・pin の上限・paging_gfx2_map) を足した。
- **C1 の heap の失敗 (PM 決定)**: 登録で `be->heap_range` が 0 以外を返すか、`gfx2_heap_init` が INVAL を返したときは、`base = size = vram_phys = 0` で `gfx2_heap_init(&a->heap, 0, 0)` をやり直して空の heap にし、登録はそのまま成り立たせる (VRAM の面の確保が NOSPC になるだけ、shim と同じ扱い)。kprintf を 1 回出す。再試行は次の登録 (gfx_init のたび) で自然に行われる。
- **別名の面の lock (ユーザー決定 2026-11-11)**: 「directx9cの段階でそういう仕様であればそれで」。DirectX 9c の実行環境では、DirectDraw 7 の primary surface の Lock と、Direct3D 9 の lockable な back buffer (D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) の LockRect が使える。これに合わせ、DIRECT の別名の lock は許す (§7-5)。
- **C6 の上限と隔離の後始末 (PM 決定、C6 の第 2 回の欠け)**: 上限は gart.c の定数 `GFX2_GART_KB 8192` (表は static の 2,048 本、system.cfg の設定は持たない。要れば後で足す)。隔離へ移すのは割当の全部の頁 (頁は不連続なので、先に全頁の持ち主と pin を確かめ、通ったら 1 頁ずつ `ledger_transfer_pinned(pfn, 1, from, quar)` を呼ぶ。シングルタスクで確かめた後に状態は変わらないので、確かめの後の 1 頁ずつの移しは失敗しない。確かめで落ちたら 1 頁も移さずに IO)。C15 の drain は、fence の完了を見てから同じ handle で `gfx2_gart_free` を呼ぶだけで、C6 の free は表に記録した持ち主 (隔離の後は `gfx2_quar_owner`) で unbind → unpin → free → 表の枠を返す。drain のための別の口は作らない。
- **C3 の access と EMIT の期限 (PM 決定、C3 の第 2 回の欠け)**: `gfx2_cpu_begin/end` の `access` は bit で、`GFX2_CPU_R 1u`・`GFX2_CPU_W 2u` (読み書きは 3)。gfx/v2/gfx2_int.h に置く (A0 の追補、C3 が入れる)。`GFX2_EMIT_DL` は fence.c の中だけの定数で、1,000,000µs (1 秒、FIFO の空き待ちの上限。GF の既存の待ちの 1000µs の上限で実機が次々に -1 になった前例があるので短くしない)。ほかの `GFX2_*_DL` (REBIND・RELEASE・MODESET) も、使う段の中だけの定数で同じ 1 秒にする。
- **C4 の SYSMEM と destroy の IO (PM 決定、C4 の第 2 回の欠け)**: SYSMEM の面は backend の `alloc`・`release` を呼ばない (backend が知る必要の無い RAM の面。shim の `alloc = shim_nospc` は VRAM と GART の面だけに効く)。`be->alloc`・`be->release` を呼ぶのは pool が VRAM か GART のときだけ。destroy で `gfx2_unmap` が IO のときは、C4 は KEEP を立てて BUSY で戻るだけで `map_detach` を呼ばない (map_as を残し、C15 が `map_as == as` を見て unmap の再試行と poison を行う)。

## 確定の後の追記 (2026-11-11、G5 の設計の欠け、設計者決定)
実装者の報告は `~/os32-tmp/run/halv2-g5/RESULT.md`。nouveau の file:line は `~/os32-tmp/ref/linux/drivers/gpu/drm/nouveau/` の写し。細目は §11-6 の「GART (G5)」。
- **手本の経路 (前提の直し)**: Ra266 の GF6200 は 10de:0221 = NV44A で、nouveau の `nv4a_chipset` は `.mmu = nv04_mmu_new` (nvkm/engine/device/base.c:677-688)。NV44 の専用 GART (mmu/nv44.c・vmmnv44.c) ではなく、nv04 の頁表付きの PCI の DMA の物を移す (docs/decisions/realhw/GF_FEATURE_COMPARE.md の 5 の行と同じ)。G5 は MMIO を書かず、`gf2_fb_init` の 0x100800・0x100850 との共存の問題は起きない。§8 の「物理の表」・§12・§12-1・§13-1 と、上の C6 の行の flush の記述を書き換えた。
- **欠け 1 (DMA の物の保持と寿命)**: GART の ctxdma は頁表の物そのもの (usernv04.c:51-55)。`gf2_dma_new` は使わず、`struct gf_v2` の `gart.obj` (A0 の追補) に頁表の inst を持ち、`gf2_ramht_add` で `GF3_HANDLE_GART_DMA` を登録する。作るのは G5 の `gf_v2_gart_start` で、呼ぶのは G11 の `gf_v2_start` (v1 の起動の後と LOST の間の mode_set)。消すのは F2 の停止 (command.c:44-59) で、G5 は消す口を持たない。unregister で F2 が OFF になった後に G1 が `gf_v2_gart_reset` で記録を捨てる。
- **欠け 2 (頁表)**: instmem に 0x20008 バイト・整列 0x1000・0 埋め (vmmnv04.c:73-77、vmm.c:1158-1159)。見出し `0x0002103d`・`0x07ffffff` (vmmnv04.c:135-139)、PTE `(pfn << 12) | 3`、外すときは 0 (vmmnv04.c:45-64)。装置への設定のレジスタは無い。flush は無い (vmmnv04.c:92-99・vmm.c:127-135)、書きは `gf_inst_write32` の読み返しまで。bind の書きの失敗は書いた分を 0 に戻して IO、unbind の書きは rc を見ない。停止の時の解放は F2 の停止。
- **欠け 3 (範囲の持ち方)**: A0 の `gart_handle/base/pages` を `struct gf_v2_gart gart` に置き換える。bind ごとの記録 `{first, n, pfn}` を first の昇順に `GF_V2_GART_BINDS` (= `GFX2_MAX_SURFACES`) 件まで持ち、割り当ては最小の隙間 (vmm.c:1773-1786)。unbind は記録との完全な一致だけ、range は 1 つの記録の中だけを 0。pfn は C6 の static の表を unbind まで借りる (§8 の「物理の表」に契約を足した、今の gart.c が満たす)。F2 の recover の後の start は記録から PTE を書き直す。
- **A0 の追補 (G5 の段で入れる)**:
  - drivers/video/geforce/gf_v2.h: `struct gf_v2 {` の前に次を足し、`u32 gart_handle, gart_base, gart_pages;      /* G5 */` の行を `struct gf_v2_gart gart;                      /* G5 だけが書く */` に替え、`gf_v2_gart_range` の宣言の後に 2 行を足す。
    ```c
    #define GF_V2_GART_PAGES 32768u                  /* nv04 の頁表の PTE の数 (vmmnv04.c:75 の bits 15)、128MB */
    #define GF_V2_GART_PT_BYTES (8u + 4u * GF_V2_GART_PAGES) /* 見出し 8 + PTE (vmm.c:1158)、0x20008 */
    #define GF_V2_GART_BINDS GFX2_MAX_SURFACES      /* bind の記録の枠 (GART の面 1 つに 1 件) */
    struct gf_v2_gart_bind { u32 first, n; const u32 *pfn; }; /* first は頁の番号。pfn は C6 の表を unbind まで借りる */
    struct gf_v2_gart {
        struct gf2_object obj;                       /* GART の ctxdma = 頁表の物 (obj.inst が頁表)。動かさない */
        u32 epoch;                                   /* obj を作った F2 の epoch。0 = 無い */
        u32 count;                                   /* bind[] の件数、first の昇順 */
        struct gf_v2_gart_bind bind[GF_V2_GART_BINDS];
    };
    ```
    ```c
    int gf_v2_gart_start(struct gf_v2 *, u32 dl);    /* G5。v2 で層 3 が ready になった直後に G11 の gf_v2_start が呼ぶ */
    void gf_v2_gart_reset(struct gf_v2 *);           /* G5。unregister の道で F2 が OFF になった後に G1 が呼ぶ */
    ```
  - hwdev/gf/accel.h: `#define GF3_HANDLE_NOTIFY ...` の次の行に足す。
    ```c
    #define GF3_HANDLE_GART_DMA (GF2_HANDLE_LAYER3_FIRST + GF3_OBJECT_COUNT + 1)
    ```
  - drivers/video/geforce/gf_v2_weak.c: `gf_v2_gart_range` の weak の後に足す (G1・G11 が G5 より先に入っても組めるように)。
    ```c
    __attribute__((weak)) int gf_v2_gart_start(struct gf_v2 * a0, u32 dl)
    {
        (void)a0;
        (void)dl;
        return OS32_ERR_NOSYS;
    }
    __attribute__((weak)) void gf_v2_gart_reset(struct gf_v2 * a0)
    {
        (void)a0;
    }
    ```
  - tools/checks/gfx2_wiring.txt: `I1 geforce gf_v2_gart_range` の次に `I1 geforce gf_v2_gart_reset`、`I1 geforce gf_v2_gart_start` の 2 行。
- **G1・G8・G11 との分担**: §11-6 の「GART (G5)」の「分担」の行が正。G1 の行・G8 の行・G11 の行 (§12) にも書いた。
- **実機でしか確かめられない点** (NP21/W に GeForce は無い): Ra266 の PCI のブリッジを通した bus master の到達性と番地の対応 (GF_FEATURE_COMPARE の U3)、頁表付きの ctxdma で GART の面への 2D と M2MF が正しい頁に届くこと (飛び飛びの pfn で)、flush 無しで bind の直後の描画と unbind・再 bind の後の描画が古い頁を見ないこと、instmem の予約に頁表 (0x20008) が収まり `gf_inst_alloc` が通ること。§13-3 の GF の行の GART の面の READBACK の比べで見る。
- **G4 の完了の照会 (PM 決定、G4 の第 1 回の欠け)**: 下位の層に待たない照会 `int gf2_fence_poll(struct gf2_chan *, struct gf2_fence, int *done)` を足す (hwdev/gf/fence.c と command.h、G4 が入れる)。中身は `gf2_fence_wait` の 1 周と同じで、`fence_state(ch)` の失敗はそのまま返し、epoch が違えば `GF2_EFAULT`、`GF2_FENCE_REFERENCE` を読んで `*done = (i32)(observed - seq) >= 0`、待たない (手本 nouveau_fence.c の nouveau_fence_done → fence->ops->read = nv10_fence_read の参照カウンタの読み)。`gf_v2_fence_state` はこれで完了済みの番号を返し、EFAULT は §6-3 の LOST の道へ。

## 確定の後の追記 (2026-11-11、I2 の設計の欠け、設計者決定)
実装者の報告は `~/os32-tmp/run/halv2-{c8,c9,c10,c14,g1,g3,g6,g7}/RESULT.md`。止まった段のほか、I2・I3 の全段を「段どうしの署名と分担」「GeForce の method・値・順」「共有の型の値の決まり」の 3 つで洗い直した。X = `~/os32-tmp/refs/geforce6200/xf86-video-nouveau/src/`、N = `~/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/`、E = `~/os32-tmp/refs/geforce6200/envytools/`。

**止まった段**
- **C14 (rebind の前の lock)**: C7 の追補 `int gfx2_lock_revoke(struct gfx2_surf *)` を lock.c に足す (unlock の static `staging_unmap` を使うため lock.c、カーネルの呼び手なので caller を取らない)。書き戻しと cpu_end はしない (rebind は v1 の作り直しの後なので中身は前の形)。rebind の順は quiesce → revoke → `alias_update` (形無しの役も同じ順)。v1 の lease は `alias_update` が見ないので閉じる物ではない。§6-5 のコード、§11-2 の表、§12・§12-1・§13-1 の行。宣言の全文: `int gfx2_lock_revoke(struct gfx2_surf *s);   /* C7。C14 だけが呼ぶ。0/IO/INVAL */` を gfx2_int.h の `gfx2_staging_drop` の次の行に。
- **G1・G4・G7 (設定と描画の分担)**: nouveau は設定 (X/nv04_exa.c:31-85 の `NV04EXASetROP`・`NV04EXASetPattern`) と描画 (:141-160) を分ける。G7 の `gf3_rop3_set` が設定、G4 の `*_emit` が描画で、`alu == GF3_ALU_KEEP` の時だけ `gf3_rop_set` を飛ばす (G4 の追補)。v1 の GX の道 (alu 0〜15) は変えない。G1 は単色の塗り (0xF0)・SRCCOPY (0xCC) を今の GX の道で、ほかを `gf3_rop3_set` → `*_emit(KEEP)` → `gf3_rop3_set(.., 0xcc, NULL, ..)` の戻しで描く。method・値・順は §11-6 の「ROP3・パターン」の表。
- **G7 (パターンの値)**: `gf3_pattern.kind` は 0 (= NULL、MONO・全 1)・1 MONO・2 COLOR。`mono[i]` は行 4i〜4i+3 を下位から、各バイトは MSB が左 (層 3 の初期化が CGA6 = 1、E/rnndb/graph/nv1_ctxobj.xml:37-39、E/docs/hw/graph/2d/pattern.rst:288-299)。色は 0x030c = 2 の後に INDEX8 は 0x0400×16 語・565 は 0x0500×32 語 (X/hwdefs/nv01_2d.xml.h:144-168)。32bpp の ROP は X/nv04_exa.c:52-56 と同じく断る。A0 の追補 (G4 の追補で入れる): hwdev/gf/accel.h の `struct gf3_pattern` の次に
  ```c
  #define GF3_PAT_MONO 1
  #define GF3_PAT_COLOR 2
  #define GF3_ALU_KEEP 0xffu   /* *_emit の alu: gf3_rop_set を呼ばず設定済みの状態で描く */
  ```
- **G3・G6 (廃止)**: N/nvkm/engine/gr/nv44.c:83-101 (NV44A の GR の class) に chroma が無く、xf86-video-nouveau は IMAGE_BLIT の COLOR_KEY に NULL を結ぶだけ (X/nv_accel_common.c:421-422)、NV40 の α は 3D (X/nouveau_exa.c:504-507 の NV40EXA*Composite)。2D の手本が無いので GF の check は鍵の COPY と ALPHA を NOSYS にし sw で行う。G4 の追補で accel.h の `gf3_ckey_set`・`gf3_alpha_set` の宣言、gf_v2_weak.c のその weak、gfx2_wiring.txt の 2 行を消す。HW 化は 3D の別設計 (§14)。
- **C8 (check の入力)**: 判定の本体は内部の `gfx2_check_exec`、公開の `gfx2_check_op` は仮の exec を作って同じ関数を呼ぶ (§4-2)。`gfx2_cpu_visible` を使う (5) は C8 (§7-2 の C9 の記述を直した)。宣言の全文 (gfx2_int.h、C8 が足す): `int gfx2_check_exec(struct gfx2_adapter *a, const struct gfx2_exec *ex, u32 *how);   /* C8。0/INVAL、*how = GFX2_HOW_* */`。C9 は C8 の後。
- **C9 (表の持ち主)**: `gfx2_sw_table` は I1 の gfx/v2/sw_table.c のまま (C9 は読むだけ)。表に無い振り分けは C9 の run_one: COPY で SRCKEY か DSTKEY → `gfx2_sw_ckey`、FILL で rop が 0xF0 でないか PAT、COPY で rop が 0xCC でないか PAT → `gfx2_sw_rop`、READBACK → `gfx2_sw_readback`、ほかは表。
- **C10 (sync の鍵)**: Y4 の `gfxsync_profile` を足す。C10 の `gfx2_caps` が照会のたびに head 0 の `gfx2_sync_get` で cpu_direct・sync_mode・sync_profile を埋める (bind の時刻に依らない。未 bind は 0・0・"")。宣言の全文 (include/gfx_sync.h の `gfxsync_mode` の次、C10 が足す): `void gfxsync_profile(const GfxSyncCtx *, char out[16]);   /* Y4。bind で当たった sync.cfg の区画の名前、既定に落ちた時と NULL は "" */`。gfx/sync/a0_weak.c に `out[0] = 0` の weak。gfx/sync/sync_int.h の `struct gfxsync_cfg` の末尾に `char key[16];   /* Y1: 当たった区画の名前 */` (Y1 の段で足す)。Trident の shim の 3 欄はこれで埋まる (shim は埋めない)。

**洗い直しで埋めた所 (I2)**
| 何 | 決め | 担当 |
|---|---|---|
| `ex->clip` | FILL・COPY・STRETCH・MONO・HOSTDATA・ROP4・ALPHA = 書き先の矩形 ∩ 書き先の面 ∩ CLIP。LINE = 面 ∩ CLIP。READBACK = 元の矩形 (CLIP は INVAL) | C9 |
| 元の矩形 | 元の面に全部収まること、外れは走査 1 で INVAL。クリップは書き先だけ | C9 |
| rop | 字義どおり。FILL・LINE は 0xF0 が普通の塗り、S を使う ROP3 (`((rop >> 2) ^ rop) & 0x33` が 0 でない) は INVAL。COPY は 0xCC が普通の写し。STRETCH は 0xCC だけ。MONO・HOSTDATA・READBACK・ALPHA・SET_PATTERN は 0。PAT の無い P は fg の単色で、COPY・ROP4 も fg を使う欄に足す (P を使う時だけ意味を持つ)。os32_gfx2.h に `#define GFX2_ROP_SRCCOPY 0xCC`・`#define GFX2_ROP_PATCOPY 0xF0` (C9 が足す) | C9・S1〜S4 |
| ROP4 | rop の下位 8 bit = 前景 (mask の 1)、上位 8 bit = 背景。mask は MONO と同じく、クリップ前の dx の列がその行の byte 0 の bit7 | S4・C9 |
| HOSTDATA の鍵 | flags の SRCKEY がある時だけ skey を使う | S6 |
| READBACK | kbuf は op.pitch の間隔で (sx, sy) が先頭 | S7・C9 |
| STRETCH | 双線形の `fx` は u64 で計算 (整数)。端は元の矩形の端で複製。YUV は RGB にしてから補間。INDEX8 に FILTER は NOSUPPORT (C8 の規則) | S8・C8 |
| 鍵 | DSTKEY = 書き先が範囲の中の画素だけ書く (DirectDraw の DDBLT_KEYDEST)。両方の時は両方の条件の論理積 | S9 |
| ALPHA | ALPHA_PIXEL が無い時は元の α を 255 とし、定数 α だけを掛ける。sw ≠ dw は §3-3 の最近点、FILTER は INVAL | S10 |
| sync の面の行 | S* は公開の `gfxsync_read_rows`/`write_rows` を h = 1 で呼ぶ (`gfxsync_row_*` は gfx/sync の中だけ)。その rc は `gfx2_sw_fn` の戻り値で返し (0/INVAL/IO)、C9 は走査 2 の装置の失敗と同じに扱う。読んで書き戻す作業の行は 1 か所: gfx2_sw.h に `#define GFX2_SW_ROW_BYTES 16384u` と `extern u8 gfx2_sw_rowbuf[2][GFX2_SW_ROW_BYTES];`、実体は sw_table.c (C9 が足す)。シングルタスクで入れ子は無い | S*・C9 |
| C2 の宣言 | U1 と同じ format.c を組むため、gfx/v2/gfx2_fmt.h (types.h と os32_gfx2.h だけを include) に §11-3 の `gfx2_fmt_info` から `gfx2_div255` までを移し、gfx2_sw.h はそれを include する (C2 が A0 の追補として入れる) | C2 |
| 形式 5〜10 | ARGB1555 の α は 0→0・1→255。A8 → (a,0,0,0)、L8 → (255,l,l,l)。7〜10 の `from_argb` は NOSYS。YUY2・UYVY は S8 が 4 バイトの組から `gfx2_yuv_argb` を呼び、`to_argb` には来ない。`pal256 == NULL` は呼び手の誤り (C9 は常に `gfx2_pal`) | C2 |
| G4 の期限と rc | `gf_v2_*` の dl は gf2 と同じ絶対の期限。GfxBackendV2 の hook の `dl_us` は相対 µs で、G1 の hook が backend_geforce.c:163-172 の `display_deadline` と同じ形で絶対に直す。GF2_E* → OS32_ERR_* は G1 の hook (GF2_ETIME → TIMEOUT、GF2_ENOSPC → NOSPC、GF2_EINVAL・GF3_EINVAL・ERANGE・EFORMAT → INVAL、GF3_EUNSUPPORTED → NOSYS、ほかは IO)。G4 は GF2 の値のまま返す | G1・G4 |
| G4 の完了 | `gf_v2_fence_state` は tail から `gf2_fence_poll` で済んだ項目を外し、completed = 最後に外した v2 (輪が空なら発行済みの最新)。輪に無い v2 の `gf_v2_fence_wait` は完了 (0) | G4 (追補) |
| G4 の GART の面 | accel_scale.c の SIFM の元の DMA に src の `dma` (0 なら VRAM_DMA) を結ぶ。重なりの判定 (accel_copy.c:47-51・accel_scale.c:47-50) は `dma` が違えば重ならない | G4 (追補) |
| G8 の DMA の結び | `gf3_surface_set` は毎回 FORMAT (0x0300) の前に SURFACE2D の 0x0184 の 2 語 `{src->dma ? src->dma : GF3_HANDLE_VRAM_DMA, dst->dma ? dst->dma : GF3_HANDLE_VRAM_DMA}` を積む (初期化と同じ method、hwdev/gf/accel_objects.c:38・89)。毎回積むので v1 の命令は VRAM に戻る。nouveau の ddx は SF2D を VRAM だけで使うので順の手本は無い (§13-3 の GART の比べで確かめる) | G8 |
| GART の面の転送 | M2MF は GART の handle を拒む (accel_m2mf.c:72-73) ので、GART の面の READBACK・HOSTDATA は G1 の check が NOSYS (sw) | G1 |
| U1 | 置き場は userland/lib/gfx/v2/ (ヘッダ os32gfx2u.h)、口は `int gfx2_color_pack(u32 fmt, u32 argb, const u32 *pal256, u32 *out)` (INDEX8 は pal256 から RGB の差の 2 乗の和が最小、同点は小さい番号。pal256 はアプリが渡す、読む KAPI は足さない) と KAPI の生成の wrapper の再輸出だけ。MANAGED は今回は持たない (欄だけ)。build/libs.mk の wildcard に userland/lib/gfx/v2 と gfx/v2/format.c の 2 行を U1 に許す | U1 |

**洗い直しで埋めた所 (I3、実装の依頼の前に PM が依頼文に写す)**
| 何 | 決め | 担当 |
|---|---|---|
| `a->heads` | C1 の `gfx2_caps_refresh` の呼び出しの直後に `a->heads = a->caps.heads < 2 ? a->caps.heads : 2` (C1 の追補、PM の小さな直し)。0 のままだと別名・screen_surface・head 系が全部 INVAL | C1 |
| パレット・screen_surface の実体 | `gfx2_pal*` は I1 の gfx/v2/palette_state.c、`gfx2_screen_surface` は surface.c のまま。C13 は定義しない | C13 |
| hook の dl_us | GfxBackendV2 の `dl_us` は全部相対 µs、backend が自分の時計で絶対に直す | 全 backend |
| 公開の列挙の値 | os32_gfx2.h に足す (C11 の段で A0 の追補): `GFX2_CONN_VGA 1・DVI_I 2・DVI_D 3・TV_COMPOSITE 4・TV_SVIDEO 5・PC98_ANALOG 6`、`GFX2_CONN_UNKNOWN 0・ON 1・OFF 2` (status)、HeadInfo の `attached` は 0/1、`fmt_scanout` は §4-1 と同じ形式の bit の mask | C11 |
| mode の id | v1 の列挙は `id = GfxMode.hw_mode`、0 なら `index + 1`。GF は `gf4_mode.id`。`enum_mode` の i が範囲外は INVAL で、`gfx2_mode_count` は INVAL まで数える。fmt は 1〜10 (0 は INVAL) | C11・C16・G10 |
| AdapterInfo | name = v1 の name、driver = kind の名前、PCI の欄は 0 (今回は持たない)。ONBOARD は C1 が `display_provider_each` の PC-98 の内蔵の登録で立てる | C11・C1 |
| HeadInfo | `be->enum_head` の後に中核の `a->head[h]` の mode_id・scan_sid・conn_mask で上書き | C11 |
| v1 の列挙 | head 0・conn 1 本 (PC-98・PEGC は PC98_ANALOG、Trident は VGA)。PLANAR4 は列挙しない | C11 |
| C12 の部品 | `conn_possible`・`clone_ok`・`mode_find` は modeset.c の static (enum_conn の possible_heads・clone_mask、fmt 1〜10 を enum_mode で探す)。全画面は既存の `gfx_fullscreen_owner` (gfx/gfx_policy.h:12) | C12 |
| 面の大きさ | w・h は mode と一致、pitch は backend が拒めば INVAL。caps の `*_align` の 0 は 1 と読む | C12 |
| shim の mode_set の後 | C12 は shim の adapter の mode_set の成功の後に `v1_bridge_rebind()` を呼ぶ | C12 |
| 全画面を抜ける | `restore_on_leave` を読むのは v1 の DISPLAY の lease の終わり (exec/lease.c の lease の終わりの 1 か所、C12 の追補)。`gfx2_head_restore_v1` は v1 の今の mode で `be->mode_set` を呼び直し、走査面を DISPLAY の別名へ | C12・C13 |
| flip_done | C13 が `gfx2_vblank_wait`・`gfx2_flip`・`gfx2_fence_query` の入口で呼ぶ。0 かつ retired_sid ≠ 0 = 反映済み、BUSY = まだ、NOSYS (shim) = 同期で済んだとして即座に差し替え。保留中の 2 回目の flip は BUSY | C13 |
| present | DISPLAY の別名へ、rect と dirty の交わりを送り、成功で dirty を 0。同期層でない (NOTFOUND) と DIRECT は 0 で何もしない | C13 |
| palette | `be->palette` は全 head に同じ値 | C13・G2 |
| sync の値 | sync_int.h に (Y1 の段で A0 の追補): `GFXSYNC_M_PLAIN 0・GAP 1・SYNC_READ 2・WAIT_PORT 3`、`GFXSYNC_F_NONE 0・READBACK 1・SERIALIZE 2`、`GFXSYNC_R_SHADOW 1・DIRECT 2`。rows の x・w は画素、cpp は t.fmt (GFX2_FMT) から | Y1〜Y4 |
| sync.cfg の読み | sysconfig では読めない (init/sysconfig.c:31 の 1KB・1 行 1 組) ので、Y1 が vfs で `SYS_GFXSYNC_CFG` を読み自前で解く。書式: `[鍵]` の区画、`名=値` を空白で区切って 1 行に複数、`;` から行末は注釈、鍵と値は大文字小文字を区別しない、16 進は小文字に正規化、ファイルは 4KB まで。bind の鍵は 1 つ、Y1 が `pci:V:D` → `pci:V:*` と落とし、無ければ既定 | Y1 |
| T1 の許可 | backend_trident.c (:196-198 の present_method・:491- の bind し直し・:581 の `.set_gap`)・trident.h:583・trident_module.h:68 を T1 に足す。sync.cfg の method が起動の既定に勝ち、診断の PRESENT の一時の切り替えは今のまま | T1 |
| P1 の許可と hook | backend_pegc.c の init・終了の道 (bind・publish・unbind) を P1 に足す。caps・enum・heap_range・kva 以外の hook は C16 の shim と同じ動き (palette は v1 の set_palette、fence_state は 0・0) | P1 |

**次の設計の回に送った所**: G2 の層 4 との結びと D1 の置き場・配備は「確定の後の追記 (2026-11-11、I3 の G2・D1、設計者決定)」で決めた。K1 の対応表は前提の CONSOLE_DUAL の D2 が未着地なので残す。

**ユーザーの判断が要る分岐**: GF の色鍵の COPY と α の HW 化。nouveau の手本に従うと NV44A の 2D には無く、3D (curie) の別設計になる。今回は sw (Win32・DirectX の互換で色鍵の blit が多いアプリは CPU の負荷になる)。
- **GF の色鍵の COPY と α (ユーザー決定 2026-11-11)**: 推奨の「今は sw、HW は後で 3D で」。NV44A の 2D に手本が無いので、HAL v2 では sw で行い (段 G3・G6 は廃止)、HW 化は nouveau の 3D (curie) を手本にした別の設計として後の票に残す。

## 確定の後の追記 (2026-11-11、I3 の G2・D1、設計者決定)
I2 の追記で残した「G2 の層 4 との結び」と「D1 の置き場と配備」を決めた。N = `~/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/` (c03c1a1)、N6 = `~/os32-tmp/refs/geforce6200/nouveau-v6.1/drivers/gpu/drm/nouveau/` (nouveau_connector.c・nouveau_display.c は N の sparse checkout に無いので v6.1)。OS32 の層 4 (hwdev/gf/display/) は N/dispnv04 を移し終えているので、G2 は層 4 の口を呼ぶだけで MMIO を 1 つも書かない。1 ファイル 400 行以下に収めるため G2 を 3 段に分けた: **G2** (gf_v2_display.c、表示の hook)・**G10** (gf_v2_enum.c、列挙)・**G11** (gf_v2_life.c、v2 の開始・停止と共通の部品)。G11 は G1 が呼ぶので I2、G2・G10 は I3。G2 は G10 の後に着手 (`gf_v2_route_index` を呼ぶ)。

**層 4 の取り方 (G2・G10・G11 共通)**
- ctx: `gf_v2_disp()` = `geforce_context && geforce_context->display_out ? *geforce_context->display_out : NULL` を呼ぶたびに引き、`ops->ctx` (struct gf4_ctx) を使う。backend_geforce.c:145-149 と同じで、C2 が change・recovery・失敗の間は窓口を閉じる。NULL は `OS32_ERR_IO`。写して持たない。
- 期限: hook に `dl_us` がある物 (flip・vblank_wait) は `now + dl_us`、無い物は `now + GF3_TIMEOUT_US` (v1 の hook と同じ)。`gf_v2_deadline(ch, us, &dl)` は backend_geforce.c:163-172 の形 (時計の hook が無ければ GF2_EIO)。
- rc: `gf_v2_rc` が G1・G2・G10 の唯一の写し。G1 の `map_rc` (I2 の追記の「G4 の期限と rc」) もこれにする。

| GF の rc | OS32 |
|---|---|
| 0 | 0 |
| GF2_ETIME・GF4_ETIME | TIMEOUT |
| GF2_ENOSPC・GF4_ENOSPC | NOSPC |
| GF2_EINVAL・GF3_EINVAL・GF3_ERANGE・GF3_EFORMAT・GF4_EINVAL・GF4_ERANGE・GF4_EFORMAT | INVAL |
| GF3_EUNSUPPORTED・GF4_EUNSUPPORTED・GF4_ENODEV | NOSYS |
| GF3_EBUSY・GF4_EBUSY | BUSY |
| ほか (GF4_ESTATE を含む) | IO (backend_geforce.c:150-162 と同じ) |

**G11 (drivers/video/geforce/gf_v2_life.c) — v2 の開始・停止**
```c
#define GF_V2_STOP_US 1000000u   /* GFX2_*_DL と同じ 1 秒 (実装の第 1 波の追記の C3) */
/* v2 の (再) 開始。N/nouveau_drm.c:1006-1027 (resume: 物の木 → fence → 表示) のうち v2 が持つ物
 * (fence の輪と GART の頁表) の分だけ。層 3・4 を作るのは持ち主 (gf3_service、gf4_service_* recover.c:273-337) */
int gf_v2_start(struct gf_v2 *g, u32 dl)
{
    const struct gf4_test_ops *ops = gf_v2_disp();
    struct gf4_ctx *c = ops ? ops->ctx : NULL;
    int rc;
    g->lost = 1;
    if (!c || !c->ready || !c->accepting || !c->ch || !c->accel || c->ch->state != GF2_RUN ||
        !c->accel->ready || c->accel->epoch != c->ch->epoch) return GF3_ESTATE;
    if (g->epoch != c->ch->epoch) gf_v2_fence_reset(g);   /* 前の epoch の印は F2 の停止で消えた */
    g->g3 = c->accel; g->ch = c->ch;
    gf3_range_query_set(g->g3, gf_v2_gart_range, g);
    rc = gf_v2_gart_start(g, dl);
    if (rc) return rc;
    g->epoch = c->ch->epoch; g->lost = 0;
    return 0;
}
/* GfxBackendV2.stop。N/nouveau_chan.c:66-80 (nouveau_channel_idle) で止まっているかを見て、
 * 止まらなければ N/nvkm/engine/fifo/chan.c:227-240 (nvkm_chan_error: channel を止める) と同じく
 * 層 4 の失敗の持ち主へ渡す (backend_geforce.c:306-313 の v1 の drain の失敗と同じ道) */
int gf_v2_stop(void *ctx)
{
    struct gf_v2 *g = ctx;
    const struct gf4_test_ops *ops = gf_v2_disp();
    struct gf4_ctx *c = ops ? ops->ctx : NULL;
    struct gf2_fence f; u32 dl = 0; int rc;
    g->lost = 1;
    if (!g->ch || g->ch->state == GF2_OFF || g->ch->epoch != g->epoch) return 0;
    rc = gf2_fault_check(g->ch);
    if (!rc) rc = gf_v2_deadline(g->ch, GF_V2_STOP_US, &dl);
    if (!rc) rc = gf2_fence_emit(g->ch, &f, dl);
    if (!rc) rc = gf2_fence_wait(g->ch, f, dl);
    if (!rc) return 0;                       /* GPU は止まっている。v1 は同じ channel で SHADOW を続ける */
    if (c && c->links && c->links->failed) (void)c->links->failed(c->links->owner, rc, dl);
    return g->ch->state == GF2_OFF ? 0 : OS32_ERR_IO;   /* OFF でなければ中核が LOST_HELD */
}
/* G4 の追補 (G11 の段が gf_v2_fence.c に足す)。N/nouveau_fence.c:79-94 (context_kill) と同じく残りを全部失敗の完了に */
void gf_v2_fence_reset(struct gf_v2 *g)
{
    struct gf_v2_ring *r = &g->ring;
    if (r->count) g->failed_upto = r->e[(r->head + GF_V2_RING - 1) % GF_V2_RING].v2;
    *r = (struct gf_v2_ring){0};
}
```
- LOST の 2 つの形: (1) 柔らかい LOST (fence の待ちが 1 秒を越えたが drain は通った、または他の道の recover で epoch が変わった) は層 4 が生きているので、次の mode_set (G2) か v1 の起動の道 (G1) の `gf_v2_start` で戻る。(2) 固い LOST (drain も通らない) は `links->failed` → `gf4_service_failed` (recover.c:341-350) が表示を止め `reusable = 0`、v1 は次の present で `geforce_fail` (backend_geforce.c:528-533、`failed` は戻らない) へ進み PC-98 に切り替わる。以後 `gf_v2_start` は層 4 が ready でないので GF3_ESTATE、mode_set は IO で、戻るのは再起動だけ (ユーザー判断 2026-11-10「自動の回復をしない」の範囲。下の「ユーザーの判断が要る分岐」)。
- `links->failed` の中の `geforce_display_failed` (gfx/gfx_core.c:414-419) は中核の `gfx2_adapter_lost` の中から呼ばれる入れ子になるが、v1 の `geforce_set_mode` の drain の失敗 (backend_geforce.c:313) と同じ入れ子で、adapter は既に LOST・gen+1 なので C14 の rebind は待つ物が無い。

**G1 への結び (G1 の段が入れる行。G1 は再依頼の前なので G1 の行 §12 に書いた)**
- `gf_v2_inst` は static にせず定義し、gf_v2.h の extern で読む。表の `.stop = gf_v2_stop`。check は `gf_v2_inst.lost || !g->g3` の間は全部 NOSYS。`geforce_direct_ok` (backend_geforce.c:121-124) に `&& !gf_v2_inst.lost`。
- v2 の初期化は backend_geforce.c の module の側 (`#ifdef OS32_GEFORCE_MODULE`) の static 1 つで、`geforce_init` の 2 か所 (:608 の `if (active) return;` を `{ geforce_v2_start(); return; }` に、:629-631 の後) から呼ぶ。v1 の起動が通るたび (gfx_init のやり直し = C1 の再登録の道) に柔らかい LOST から戻る。
```c
static void geforce_v2_start(void)
{
    const struct gf4_test_ops *ops = display_ops();
    u32 dl = 0;
    if (ops) (void)display_deadline(ops->ctx, &dl);
    int rc = gf_v2_start(&gf_v2_inst, dl);          /* 失敗は lost = 1 のまま (v2 は SW、v1 は SHADOW) */
    if (rc) kprintf(KLOG_ATTR, "[gf_v2] start rc=%d\n", rc);
}
```

**v2 の走査面と v1 の selected_front の関係 (G2)**
- 層 4 の走査の確定は 1 か所 (`gf4_scan_changed`、hwdev/gf/display/scan.c:235-279) で、head 0 なら `gf_hook_scan_changed` (backend_geforce.c:247-270) が v1 の `selected_front`・framebuffer・mode の表・`hook_scan` を書き換える。v2 の面をそのまま v1 に渡すと、v1 の present (SHADOW の upload、:561-602) と HW の塗り (gf_hook.c) が全画面アプリの走査面に書き、全画面を抜けた時に戻る先も失う。
- 決め: **v2 の面は v1 に渡さない**。G2 が backend_geforce.c:249 の後に `if (gf_v2_owns(front)) return;` の 1 行を足す。`gf_v2_owns` は front の `reservation_id`・`generation` が G9 の `node[]` の生きた予約 (owner が非 NULL) と一致すれば 1 (scan.c:60-64 の照合と同じ)。v2 が head 0 を走査している間、v1 は自分の front (戻り先) に描き続け (見えない)、v1 の DIRECT もそこを指す。D3D9 の排他の全画面で GDI の画面が裏に残るのと同じ形。
- 戻り先 `home`: head 0 で v1 の走査 (enabled で `gf_v2_owns` でない front) の上に v2 の面を mode_set する時、G2 が層 4 の `state->head[0]` から `{front, mode.id, route の番号 (gf_v2_route_index), palette[768]}` を写す。v2 の面どうしの mode_set・flip では写さない。mode_set が失敗したら写しを捨てる。
- 抜ける時: C13 の `gfx2_head_restore_v1` は DISPLAY の別名の面で `be->mode_set` を呼ぶ (§9-2・I3 の追記の「全画面を抜ける」)。G2 は別名の面 (`GFX2_SF_ALIAS`) の mode_set を「戻り先へ戻す」と読み、`m`・`conn_mask` (C12 の検査にだけ使われた) の代わりに `home` の route・mode id・front で `ops->set_mode` を呼び、成功で INDEX8 なら `home.pal` を `gf4_palette_set(c, 0, 0, 256, ..)` で入れ直し、`home.valid = 0`・`pal_set = 0`。`home` が無い (v2 が head 0 を取っていない) 時は、層 4 の head 0 の front の off が別名の `gpu_off` と同じなら 0、違えば INVAL。head 1 の別名は INVAL (v1 は head 0 だけ、backend_geforce.c:173-176)。v1 の front の予約は層 4 の scan の node なので v2 の間も返されない (scan.c:144-156 の discard は v1 の set_mode でだけ走る)。

**G2 (drivers/video/geforce/gf_v2_display.c) — hook ごとの層 4 と手本**
面 → `gf3_surface` は `{off = gpu_off, bytes, pitch, width = w, height = h, fmt (INDEX8・RGB565・XRGB8888 だけ、ほかは INVAL), reservation_id = priv[0], generation = priv[1], dma = 0}` (G9 の priv)。

| hook | 層 4 (OS32) | nouveau | G2 の決め |
|---|---|---|---|
| mode_set | `ops->set_mode` = gf4_set_mode (display/modeset.c:327-375) | N/dispnv04/crtc.c:643-669 (mode_set)・:708-751 (prepare・commit)・:609-640 (swap_fbs の pin = OS32 は G9 の予約) | conn_mask は 1 bit (clone_mask = 0 なので C12 が先に断る)、route = `c->routes[conn]` の写しに `head` を入れる (route_equal は head を見ない、display/output.c:59-66。crtc は modeset で選ぶ、N/dispnv04/dac.c:556 の possible_crtcs)。`head_mask` に無い head は INVAL。`g->lost` なら最初に `gf_v2_start`。別名なら上の「抜ける時」。成功で `head[h] = {s->sid, 0, 0}`、INDEX8 で `pal_set` なら `gf4_palette_set(c, h, 0, 256, g->pal)` (失敗は kprintf だけで 0。mode は確定済み) |
| flip | `ops->flip` = gf4_flip (display/flip.c:159-217、sync = flags & GFX2_FLIP_VBLANK) | N/dispnv04/crtc.c:1136-1217 (page_flip)・:1074-1087 (flip_complete) | 別名は INVAL、`pend_sid` があれば BUSY。成功の後 `c->hold[h].pending` なら `pend_sid = s->sid`、でなければ (同期の flip は確定済み) `retired = scan; scan = s->sid` |
| flip_done | `gf4_flip_complete` (flip.c:113-141) を直に (窓口を通った後) | 同上 | `pend_sid` が無ければ retired を返して 0 に。hold が pending で `base_verified` かつ vblank の数 (`gf_irq_snapshot` の `vblank[h]`、flip.c:80-85 と同じ数) が `hold.after` のままなら BUSY。進んでいれば `gf4_flip_complete(c, h, now + GF3_TIMEOUT_US)` (待たずに確定)、失敗は `pend_sid = 0` で rc。hold が pending でなければ (vblank_wait が先に確定した) そのまま入れ替え |
| vblank_wait | `ops->vblank_wait` = gf4_vblank_wait (flip.c:143-157。保留の flip を先に確定) | N/dispnv04/crtc.c:1277-1284 (vblank の割込み) | そのまま |
| palette | `gf4_palette_set` (display/gamma.c:80-92) を直に | N/dispnv04/crtc.c:777-799 (8bpp の palette と 16/32bpp の gamma は同じ LUT) | xrgb を `pal[]` に RGB 8 bit で写し `pal_set = 1`、enabled で INDEX8 の head 全部に送る (層 4 は INDEX8 でない head の palette を受けない、gamma.c:87)、最初の失敗の rc |
| gamma | `ops->gamma` = gf4_gamma (gamma.c:94-107、565・X888 だけ) | N/dispnv04/crtc.c:816-836 | `rgb768` を `const u16 (*)[256]` で渡す |
| cursor_image | `ops->cursor_image` = gf4_cursor_image (display/cursor.c:184-) | N/dispnv04/crtc.c:938-1020 (nv11_cursor_upload の premultiplied・cursor_set) | 面は ARGB8888 (§3-3 の premultiplied) で `kva` が非 NULL (C13 が cpu_begin 済み)、`{kva, bytes, pitch, w, h, 0, 0}`。32・64 の正方形の外は層 4 が ERANGE |
| cursor_move・cursor_show | `ops->cursor_move`・`ops->cursor_show` (cursor.c:117-137) | N/dispnv04/crtc.c:1022-1037・cursor.c | show は 0・1 |
| dpms | `ops->dpms_set` = gf4_dpms_set (display/dpms.c:68-91) | N/dispnv04/crtc.c:175-233 | state は 0〜3 (GFX2_DPMS_* と gf4_dpms は同じ値) |

**G10 (drivers/video/geforce/gf_v2_enum.c) — 列挙**
- GFX2 の connector = 層 4 の route (`c->routes[i]`、DCB の出力 1 本) の番号 i。G1 の caps の `connectors` は `c->nroutes`、`heads` は 2。`gf_v2_route_index(c, r)` は connector・type・or_mask が一致する最初の i、無ければ nroutes。
- enum_head: head < 2。`state->head[h]` が enabled なら attached 1・connector_mask = 1 << route の番号・w/h = mode の hd/vd・mode_id。fmt_scanout = INDEX8・565・X888 の 3 bit。max_w = max_h = 4096 (N6/nouveau_display.c:682-685、CELSIUS〜CURIE)。scanout_sid は中核が上書き。
- enum_conn: type は N6/nouveau_connector.c:1327-1350 (DCB の connector 表が無い時の決め方) と同じ: 同じ connector に ANALOG と TMDS があれば両方 DVI_I、TMDS (LVDS も) だけは DVI_D、ANALOG だけは VGA。TV は `c->tv_subconnector == 2` (tv.c:155 の TV_SVIDEO) なら TV_SVIDEO、ほかは TV_COMPOSITE。possible_heads = head_mask & 3、clone_mask = 0 (N/dispnv04/dac.c:557・dfp.c:716・tvnv17.c:827 の possible_clones = 0)。
- 状態: TV は `tv_sense_valid` が偽なら UNKNOWN、`tv_pin_mask` が 0 なら OFF、ほかは ON (tv.c:310-322)。ほかは `gf4_ddc_probe(c, i2c_index, dl)` → `ops->ddc_read(.., 0, blk, dl)` (modes.c:229-237 の detect と同じ順、N6/nouveau_connector.c:579-588)。EDID の頭 (00 FF FF FF FF FF FF 00) が合えば、DVI_I の相方がいる時は byte 20 の bit7 (digital) が TMDS 側なら TMDS が ON・ANALOG が OFF、逆なら逆 (N6/nouveau_connector.c:603-618)、相方が無ければ ON。ON の時だけ edid_blocks = 1 + byte 126。EDID が無ければ TMDS は OFF、ANALOG は UNKNOWN (OS32 の層 4 に analog の負荷の検出が無い。N6 の :629-645 は force の時だけ)。monitor_name・mm_w・mm_h は 0 (欄だけ)。preferred_mode_id は `mode_connector` がこの connector の最初の mode の id (modes.c:247-251、最初に通った候補が native)。
- enum_mode(head, fmt, i): fmt は 1〜10 (1〜3 以外は 0 件)。`state->modes[]` の順に、fmt が合い、その connector の route のどれかの head_mask に head がある物を数え、i 番目を返す。i が件数以上は INVAL。`id = mode.id`、`w = t.hd`、`h = t.vd` (DBLSCAN は層 4 が倍にした値で、面の高さもこれ、modes.c:252-258)、`pixclk_khz = t.khz`、`htotal`・`vtotal`、`hsync_khz10 = khz·10 / ht`、`refresh_mhz` は u64 を使わず `q = khz·1000 / total; r = khz·1000 % total; q·1000 + (r·1000 + total/2) / total` (modes.c:252-258 の ×1000)、`flags = t.flags & 15` (下の GFX2_MODE_*)、source・scaling は 0 (欄だけ)。
- edid(conn, block): TV は NOSYS、block は 0〜255、`ops->ddc_read(c, i2c_index, block, out, dl)` (ddc.c:243-246)。

**D1 (sync.cfg の置き場・配備・既定・インストーラ)**
- 置き場: ホストは `assets/gfx/sync.cfg` (新規)、ゲストは `/etc/gfx/sync.cfg` (= include/config.h:42 の `SYS_GFXSYNC_CFG`)。読むのはカーネルなので配備の定義は build/core.yaml (カーネル層) に 1 項目 `host: assets/gfx/sync.cfg`・`guest: /etc/gfx/sync.cfg`・`tags: [gui]`。build/packages.yaml は変えない (tag で GUI.PKG に入り、`/etc/gfx` は mkpkg の entry_dirs (tools/mkpkg.py:490) が足す)。親の `/etc/gfx` は配備の道具が作る (`/usr/share/fonts` と同じ)。
- tag を gui にした理由: 同期層を使うのは PEGC・Trident の GUI だけ (trident.mod も gui)。起動 FD (BOOT + MINIMAL) に入らないので 2HD の空きを使わない。無い時は Y1 が組み込みの既定 (SHADOW・unit=1・PLAIN・flush=NONE) で動き、Trident は byte の書き (11-08 の掃引で欠け 0、遅い) になる。
- 中身はリポジトリが持つ (機器ごとの実測値、`TRI_WRITE_GAP_DEFAULT_NS` と同じ扱い)。hsync・make deploy は上書きする。hsync_protect の保護の名前には足さない。値を変えるのは assets/gfx/sync.cfg の commit。
- インストーラ: install.c (FD → HDD) は `/etc` を写すが FD に無いので何もしない。cdinst は Normal・Full で GUI.PKG から入る。FD で入れた後は実機の更新 (hsync) で入る。
- 旧値の引き継ぎ (PM 決定 #2): カーネルは system.cfg の `TRI_WRITE_GAP` を読まない (T1 が trident_module.c:198-199 を消す)。T1 を含む版を Ra266 に入れる前に、ノートのセッションで `/etc/system.cfg` の `TRI_WRITE_GAP` を見る。80 以外の値があれば PM が assets/gfx/sync.cfg の `[pci:1023:*]` の gap_ns をその値にして入れる。無いか 80 なら何もしない。system.cfg の旧鍵は消さない (読まれないだけ、ファイルを書き換える操作を増やさない)。
- 既定の中身 (全文。書式は I2 の追記の「sync.cfg の読み」、値は §10 の例と同じ):
```
; /etc/gfx/sync.cfg — CPU 直描きの同期層 (HAL_V2_DESIGN §10)
; 鍵は pci:VVVV:DDDD → pci:VVVV:* → 機種名の順に照らし、無ければ組み込みの既定
; (SHADOW unit=1 PLAIN flush=NONE)。; から行末は注釈。
[pegc]
mode=SHADOW  unit=4  method=PLAIN  flush=NONE  read_ns=0  read=SHADOW
[pci:1023:*]
; Trident (Ra266 内蔵)。gap_ns は 2026-11-08 の掃引 (70ns で欠け 0、+10ns)
mode=SHADOW  unit=4  method=GAP  gap_ns=80  flush=NONE  read_ns=1500  read=SHADOW
```
- 試験は §13-1 の「sync.cfg の既定の中身 (D1)」(Y1 の段が入った後に D1 が足す)。

**A0 の追補 (ヘッダの全文。G11 の段の最初の commit。os32_gfx2.h は G10 の段の最初の commit)**
- drivers/video/geforce/gf_v2.h: `struct gf_v2 {` の前に 2 行、`struct gf_v2` の `struct gf_v2_gart gart;` の行の後に 4 行、`gf_v2_dpms` の宣言の後に 10 行を足す。`g3`・`ch` の行の注釈を「G11」に替える。
```c
struct gf_v2_head { u32 scan_sid, pend_sid, retired_sid; };   /* G2 だけが書く */
struct gf_v2_home { u32 valid, conn, mode_id; struct gf3_surface front; u8 pal[768]; }; /* G2 だけが書く。v2 が head 0 を取る前の v1 の走査 */
```
```c
    u32 epoch, lost;                             /* G11 だけが書く */
    struct gf_v2_head head[2];                   /* G2 */
    struct gf_v2_home home;                      /* G2 */
    u32 pal_set; u8 pal[768];                    /* G2 (RGB 各 8 bit) */
```
```c
extern struct gf_v2 gf_v2_inst;                  /* G1 */
struct gf4_test_ops; struct gf4_ctx; struct gf4_route;
int gf_v2_start(struct gf_v2 *, u32 dl);         /* G11。0 / GF2・GF3 の rc */
int gf_v2_stop(void *ctx);                       /* G11。GfxBackendV2.stop */
const struct gf4_test_ops *gf_v2_disp(void);     /* G11 */
int gf_v2_deadline(struct gf2_chan *, u32 us, u32 *dl);  /* G11 */
int gf_v2_rc(int gf_rc);                         /* G11 */
void gf_v2_fence_reset(struct gf_v2 *);          /* G4 の追補 (G11 の段) */
u32 gf_v2_route_index(const struct gf4_ctx *, const struct gf4_route *);  /* G10 */
int gf_v2_owns(const struct gf3_surface *);      /* G2 */
```
- drivers/video/geforce/gf_v2_weak.c の末尾 (G1 が G11 より先に入っても組め、止める側に倒れる値):
```c
__attribute__((weak)) int gf_v2_start(struct gf_v2 *a0, u32 dl) { (void)a0; (void)dl; return OS32_ERR_NOSYS; }
__attribute__((weak)) int gf_v2_stop(void *a0) { (void)a0; return OS32_ERR_NOSYS; }
__attribute__((weak)) const struct gf4_test_ops *gf_v2_disp(void) { return NULL; }
__attribute__((weak)) int gf_v2_deadline(struct gf2_chan *a0, u32 us, u32 *dl) { (void)a0; (void)us; (void)dl; return OS32_ERR_NOSYS; }
__attribute__((weak)) int gf_v2_rc(int rc) { (void)rc; return OS32_ERR_IO; }
__attribute__((weak)) void gf_v2_fence_reset(struct gf_v2 *a0) { (void)a0; }
__attribute__((weak)) u32 gf_v2_route_index(const struct gf4_ctx *a0, const struct gf4_route *a1) { (void)a0; (void)a1; return 0; }
__attribute__((weak)) int gf_v2_owns(const struct gf3_surface *a0) { (void)a0; return 0; }
```
- tools/checks/gfx2_wiring.txt に 8 行: `I2 geforce gf_v2_deadline`・`I2 geforce gf_v2_disp`・`I2 geforce gf_v2_fence_reset`・`I2 geforce gf_v2_rc`・`I2 geforce gf_v2_start`・`I2 geforce gf_v2_stop`・`I3 geforce gf_v2_owns`・`I3 geforce gf_v2_route_index` (今の並びの位置に)。
- sdk/include/os32/os32_gfx2.h: `GFX2_DPMS_OFF` の行 (C11 の追補が先に入っていればその後) の後に 4 行。ModeInfo の `flags` の bit で、層 4 の GF4_NEG_H・NEG_V・INTERLACE・DBLSCAN (display/data.h:8-11) と同じ値。v1 から作る列挙 (C11) は 0。
```c
#define GFX2_MODE_NHSYNC 1
#define GFX2_MODE_NVSYNC 2
#define GFX2_MODE_INTERLACE 4
#define GFX2_MODE_DBLSCAN 8
```

**実機でしか確かめられない点** (NP21/W に GeForce は無い): v2 の面への mode_set が 640x480 以外・565・X888 で映ること、全画面を抜けた時に v1 の front と palette が戻ること (カメラ)、非同期の flip の vblank の数での確定、565 → INDEX8 の palette の入れ直し、DVI-I の EDID の digital の bit での振り分けと DDC の無い ANALOG が UNKNOWN になること、TV の sense の値、固い LOST (drain の失敗) で表示が止まり PC-98 へ切り替わること (故障の注入が要る)。D1: Ra266 の `/etc/system.cfg` に `TRI_WRITE_GAP` があるか、Trident の装置の番号が `pci:1023:*` に落ちること、gap_ns=80 で欠けが 0 (§13-3)。geforce.mod の大きさの枠 (build/geforce-module.mk の mkmod の 160/160/208 KiB) に G2・G10・G11 の約 630 行が収まるかは make で見る。

**ユーザーの判断が要る分岐**: 固い LOST (drain も通らない) の後、GF は再起動まで戻らない (v1 の `failed` と同じ)。ユーザー判断 2026-11-10 の「復帰は次の mode_set か再起動」のうち、mode_set で戻るのは柔らかい LOST だけになる。mode_set で層 4 の recover (`gf4_service_recover` の停止 → 再開、recover.c:314-337) まで行う案もあるが、v1 は既に PC-98 へ落ちているので v2 だけが GF を取り直す形になり、別の設計が要る。推奨は今の形 (再起動)。
- **固い LOST の後 (ユーザー決定 2026-11-11)**: 推奨の「固い LOST は再起動で戻す」。柔らかい LOST (drain が通った) は次の mode_set で戻る。固い LOST は v1 の failed と同じく PC-98 の表示に落ち、GF は再起動まで戻らない。
## 確定の後の追記 (2026-11-11、T1 の診断の上書き、PM 決定)
- Y4 の追補: `gfxsync_override(ctx, method, unit, gap)` はその ctx の方式・単位・間隔を一時変更し 0/INVAL、`gfxsync_override_clear(ctx)` は bind 時の値に戻す。§10 の範囲と hook の検査を適用し、失敗時は変更しない。方式定数は include/gfx_sync.h に置く。
- 担当は include/gfx_sync.h・gfx/sync/sync_int.h・sync_bind.c・a0_weak.c と tools/checks/gfx2_wiring.txt (I3 kernel)、使い手は T1 の診断。host 試験で切替・復元・無効値を検査する。

- **I3 の容量 (PM 決定 2026-11-11)**: GeForce module の mem_max は実測 305,616 B に余裕を持たせ 393,216 B (384 KiB)、load_span_max は従来の prefix の余裕 64 KiB を加え 458,752 B (448 KiB)、image_max は 262,144 B を維持する。kernel は固定帯を持たず pgalloc から確保する。値の正・生成の表・manifest・固定値の試験・[容量の台帳](../../08_build.md#起動時に読む部品の大きさの上限と由来) を追従する。

## 確定の後の追記 (2026-11-11、実機の G8 の GART への 2D の書き、PM 決定)
- **実機の結果**: GeForce 6200 で gfx2_rh の G8 が落ちた。VRAM から GART の面 (256x64 INDEX8、4 頁) への 2D の COPY の後に CPU で読むと、48〜63 行目 (4 頁目) の全部が元の値のままだった。0.5 秒待って読み直しても変わらず、fence は進み、failed は 0 だった。逆向き (GART が元で VRAM が書き先) と M2MF の読み戻しは通る。§7-2 の追記の「G8 の DMA の結び」は、SF2D の書き先を GART にする組み合わせに nouveau の手本が無いことを §13-3 の比べで確かめる前提だったが、その比べが落ちた。
- **決定 (手本に寄せる)**: SF2D の書き先 (0x0188) は VRAM だけにする。GPU から GART への書きは、xf86-video-nouveau (nv04_exa.c:412-426) と nouveau (nouveau_bo0039.c:38-63) と同じく M2MF だけで行う。元 (0x0184) の GART は今のまま。
  - `check` (drivers/video/geforce/gf_v2_ops.c): 書き先が GART の op は、次の 1 つだけを HW にする。COPY・rop 0xCC・F_PAT と色鍵の flag なし・元が VRAM・元と書き先の fmt が同じ。ほかは NOSYS を返す (GART は CPU から見えるので core が SW で描く)。STRETCH・FILL・MONO・HOSTDATA の書き先が GART なら NOSYS。
  - `emit` の上の COPY: `gf3_m2mf(VRAM_DMA → GART_DMA)` で矩形を写す。in_off = 元の off + sy*pitch + sx*cpp、out_off = 書き先の off + dy*pitch + dx*cpp、rowbytes = w*cpp、rows = h。gf3_m2mf は中で fence を待つ (読み戻しと同じ)。
  - `gf3_m2mf` (hwdev/gf/accel_m2mf.c): handle に `GF3_HANDLE_GART_DMA` を許す。範囲の検査は `m2mf_span` で、`gf3_surface_check` の GART の分岐と同じく `c->range_q` (G5 の `gf_v2_gart_range`) を引く。range_q が無ければ EINVAL。VRAM の重なりの検査は VRAM 同士のときだけ (今のまま)。
- **試験**: host 試験 gf_v2_ops (check の表で GART の書き先の行)・gf_g8 (SF2D の 0x0188 が GART にならない)・M2MF の host 試験 (GART の handle の範囲内・外)。実機の受入は gfx2_rh の G8 (HALV2-RH-I2)。
