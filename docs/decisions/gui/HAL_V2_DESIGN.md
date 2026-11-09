> 出所: `~/os32-tmp/run/hal-v2/DESIGN.final.md` (Claude 起草、codex astra のレビュー 4 回 (第 4 回はユーザー承認) で確定、2026-11-09)。段の状態は票 TASK_HAL_V2 が持つ。
> 状態: **現行 (2026-11-09)** — 表示 HAL v2 (形式 u32×2・32bpp、DX9c 程度の能力、2D の口、fence、面、GPU から見える RAM、adapter/head の列挙、CPU 直描きの同期バックエンド層) の確定設計。

# 表示 HAL v2 の詳細設計 (確定)

前提: シングルタスク・協調切替、9801 互換は見ない、カーネルは整数だけ。ユーザー決定は DECISIONS.md (11-08・11-09) が正。3D の口とオーバーレイは範囲外 (能力の欄だけ置く)。今の版 (GFX_ACCEL_HAL_DESIGN の H1〜H6・G1〜G3) は壊さず上に積む。

## r3→r4 の変更 (REVIEW.r3 の指摘ごと、現物は 4f971604)
- P1-1 fence の順: `gfx2_fence_issue` を廃し、`gfx2_fence_reserve` (番号だけ) → `be->emit` (描画) → `gfx2_fence_commit` (印 + note、失敗は喪失) の 3 段に。命令ごとに 1 番号 (§6-1・§11-2・§11-3)。
- P1-2 sync の ctx: adapter の `sync[2]` を廃し、C1 の `gfx2_sync_publish(kind, head, ctx)`・`gfx2_sync_get(a, head, &ctx, &mode)` と Y4 の `gfxsync_mode()` に。DIRECT/SHADOW の判定はこの 1 口だけ (§10・§11-2・§11-5)。
- P1-3 gart_range: `struct gf3_ctx` の末尾に範囲照会の callback と opaque を A0 が足し、設定 G1・実体 G5・利用 G8 (§11-6)。
- P1-4 別名: C4 の `gfx2_surf_alias`・`gfx2_surf_alias_update` (backing を借りるだけ、解放しない、更新で gen+1) を A0 に。C14 はこれだけを呼ぶ (§6-5・§11-2)。
- P1-5 unmap 失敗: C7 の `gfx2_map_detach`・`gfx2_vaspace_drop` を足し、unmap 失敗の面は backing を返さない (KEEP)。destroy・quarantine の失敗でも AS への参照は 0 (§7-3・§11-2)。
- P1-6 .text の条件: 組込み backend は kernel の .text (A0 が build/os32.ld に境界の記号)、module は `GfxBackend.v2_mod` の module の .text。GF の v2 表は geforce_module.c が kernel 側へ写す (§11-7)。
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
| GF の登録 | backend_geforce.c:261-262・719-734 | |
| Trident | trident.h:351-352・581、write_gap.c:4-21、regs.c:117-138、present.c:26-56、trident_module.c:198-200、backend_trident.c:96-98・144 | 同期層へ移す元 |
| mm | mm/dma_pool.h (64KB 固定)、pgalloc.h:227-235・191-197・116、pgalloc_owner.inc:216-231 (`ledger_transfer`)・233- (`ledger_reclaim_owner`)、pgalloc_resource.inc:37-38 (`ledger_device_map_prepare` は DEVICE owner だけ)、paging_map.inc:169-171 (supervisor の恒等 UC の写像) | 写像はユーザー向けでない |
| ユーザーの写像 | mm/paging.h:286-317 (`map_user`・`map_user_range_phys`・`free_user_range` は頁を pgalloc へ返す)、paging.h:206 `lease_mapping`、exec/lease.c:40-60 (ledger_surfaces に縛られる)、memmap.h:465-468 (lease の帯 F0000000〜FE000000・8 本) | |
| 写像の帯 | mm/paging_addrspace.inc:202-215 (`map_user_range_phys` は app_pde の外を拒む)、paging.h:216 (`lease_pt_phys[MEM_LEASE_MAX_PDES]`)、paging_lease.inc:5-11 (root の文脈)・135- (`paging_lease_unmap` は帯の中なら ledger に縛られない)、paging_addrspace.inc:115-120 (破棄で lease の PT を返す)、exec/lease.c:69 (確保の上限は `MEM_LEASE_END`) | 新しい帯は lease の PT を共用する |
| AS の後片付け | exec/exec_finish.inc:33-34 (`lease_revoke_all`)・36 (poison なら私物を返さない)・55 (`paging_addrspace_destroy`)・62-64 (reclaim・retire)・69 (`kfree(a->as)`) | |
| GF の同期 | hwdev/gf/accel_fill.c:44-46・accel_copy.c:61-62 (中で fence を待つ) | emit だけの口を足す |
| g_backend | include/gfx_hal.h:181 は `GfxBackend *` (H4 が hook を書き換える) | `GfxBackendV2` は const、v1 は毎回 `a->v1->` から読む (D2) |
| module | tools/module_schema.py、build/out/trident/trident_imports.h:19 (`TRIDENT_MODULE_ABI_GEN`)、geforce_module.c:299 | |

食い違い 2 件 (r1 から): (2) SURVEY §3-1 の GF の透過色は実物では未使用。(3) SURVEY §6 #8 に対し GF の中には `pci_dma` と 1 頁の staging がある。(1) H2 以降の未着地は解消 (D1)。

r4 で直した食い違い (現物 4f971604。上の表の該当の行もこの値で読む):
| ID | r3 の前提 | 現物 | 直した所 |
|---|---|---|---|
| D1 | gfx_hw_submit.c:1-14 は NOSYS | gfx/gfx_hw_submit.c は実装済み (111 行、`submit_one`:20・`gfx_hw_submit`:70・`gfx_hw_get`:95)。H1〜H6・G1〜G3 は main に入った | §11 の着地の前提は満たした。C14 の呼ぶ行は `gfx_hw_submit`:70・`gfx_hw_get`:95 の入口 |
| D2 | gfx_hal.h:181 は `const GfxBackend *` | `extern GfxBackend *g_backend` (gfx_hal.h:181、実体 gfx_core.c:56)。`gfx_policy_reeval` が `g_backend->fill_rect`・`blit` を書き換える (gfx_policy.c:47-48) | `struct gfx2_adapter.v1` は `GfxBackend *`。v1 の hook は呼ぶたびに `a->v1->` から読み、関数ポインタを写して持たない (§11-2) |
| D3 | `gfx_scanout_gen_bump()` | 無い。走査の変化は `gfx_policy_scan_changed()` (gfx/gfx_policy.c:67-76、宣言 gfx_policy.h:14-16「Module-safe scan notification」。中身は `gfx_scan_changed = 1;` → `gfx_policy_reeval()`) | §9-3 |
| D4 | gfx_screen_owner は gfx_core.c:457 | gfx_core.c:461 | §6-5 |
| D5 | accel_copy.c:60-61 | accel_copy.c:61-62 (accel_fill.c:44・46 は同じ) | §1・§11-6 |
| D6 | GfxBackend の末尾に足すだけ | 末尾は `shadow_fill_rect`・`shadow_blit` (gfx_hal.h:142-143)。tools/hwdev_layout.json:661 に `struct GfxBackend`、:1033 に `struct gf3_ctx` の配置 | A0 は 143 の後に足し、hwdev_layout.json の 2 表も直す (§11-6・§11-7) |
| D7 | GF・Trident の表は module の物を指す | module の init が kernel 側の `candidate` を埋め、`geforce_start` (geforce_module.c:193-241) が module の init で kernel 側の `candidate` を埋めて hook ごとに `module_text_contains` を見る (G3 で `direct_ok` の text の検査と、`direct_ops` の表が module の ro の中にあるかの検査 :216-241 が加わった)。公開 (`gfx_backend_geforce = candidate`) は load の成功の後の :334 で別に行う (既定の表は :144)。trident_module.c:22・31 も同形 | v2 の表も kernel 側へ写し、`v2_mod` を付ける (§11-7) |
| D8 | 「module の .text の中」の検査を組込みにも使える | kernel の .text の境界の記号が無い (build/os32.ld:36-41 は `*(.text)` だけ、`_etext` を使うのは host 試験だけ) | A0 が `__os32_text_start`・`__os32_text_end` を足す (§11-7) |
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
| INDEX8 → RGB | 今のパレット (`gfx_get_palette`) | 0 |
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
| 32 | `caps2d_hw, caps2d_sw, exact` (bit は §4-2 の op の番号。hw/sw は「どれかの形式で使える」の要約だけ) |
| 44 | `fmt_surface, fmt_render2d, fmt_scanout, fmt_src_stretch` |
| 60 | `max_w, max_h, max_pitch, pitch_align, offset_align, scanout_align, stretch_min_q16, stretch_max_q16, max_cmds` |
| 96 | `vram_bytes, vram_cpu_bytes, gart_bytes, gart_page, fence_hw, async` |
| 120 | `heads, connectors, palette_entries, gamma_bits, cursor_w, cursor_h, cursor_fmt, flip_vblank, vblank_irq, thermal` |
| 160 | `cpu_direct, sync_mode` + `char sync_profile[16]` |
| 184 | 3D の段 (今は全部 0): `vs_version, ps_version, max_tex_w, max_tex_h, max_tex_aspect, max_tex_stages, max_streams, max_prim, max_index, max_rt, tex_caps, raster_caps, zcmp_caps, blend_caps` |
| 240 | `reserved[4]` (0) |

装置ごとの埋め方は r1 §4 の表のとおり。ただし GF の caps2d_hw の ROP3・PAT は G7 まで「パターンを使わない GX 相当の 16 種、32bpp は COPY だけ」、ALPHA は G6 で確かめるまで 0。

### 4-2. 操作ごとの可否
```c
#define GFX2_HOW_NOSUPPORT 0
#define GFX2_HOW_SW        1
#define GFX2_HOW_HW        2
int gfx2_check_op(u32 adapter, const GFX2_Op *op_shape, u32 src_fmt, u32 dst_fmt,
                  u32 src_pool, u32 dst_pool, u32 *how);  /* op・rop・flags と形式と pool で判定 */
```
- 中核の判定の順: (1) 形式の組が規則で禁止 (RGB→INDEX8、PLANAR4、3D の形式) → NOSUPPORT、(2) 書き先が SHADOW の面 (§6-4) → SW、(3) backend の `check` が HW を返す → HW、(4) それ以外 → SW。submit は同じ関数で命令ごとに振り分ける (判定と実行が食い違わない)。

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
| COPY | 2 | dst・src・d*・sx/sy (sw・sh は dw・dh と同じ)・rop・skey/dkey | ROP が SRCCOPY 以外と鍵の同時 |
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
 * シングルタスクなので入れ子は無いが、入れ子なら OS32_ERR_BUSY (in_submit の旗) */
if (user && (!writable(done, 4) || !writable(fence, 4))) return OS32_ERR_INVAL;   /* 何もしない */
a = gfx2_adapter_get(adapter); if (!a) return OS32_ERR_INVAL;
if (gen != a->gen) return OS32_ERR_STALE;
if (n > GFX2_MAX_CMDS || (user && !readable(ops, n * sizeof(GFX2_Op)))) return OS32_ERR_INVAL;
copy_from(k_ops, ops, n * sizeof(GFX2_Op));                 /* 以後 ops は読まない */
u32 k_done = 0, k_fence = a->fence.issued; int rc = 0;     /* n == 0 は 0・done=0・fence=発行済みの最新 */
static struct gfx2_exec ex[GFX2_MAX_CMDS];
for (i = 0; i < n; i++) { rc = check_one(a, &k_ops[i], &ex[i]); if (rc) goto out; }  /* 走査 1: 何も実行しない */
for (i = 0; i < n; i++) { rc = run_one(a, &ex[i]); if (rc) break; k_done = i + 1; }  /* 走査 2 */
if (k_done) { a->be->kick(a->ctx); }
k_fence = a->fence.issued;
out:
*done = k_done; *fence = k_fence;      /* 最後に書く。READBACK の先と重なっていてもこの値が残る */
return rc;   /* 走査 2 の rc は装置の失敗だけ (§6-3)。k_ops[k_done] は途中まで行ったかもしれず、再送しない */
```
- 検査 (走査 1): 面の sid の世代・持ち主 (呼んだ AS)・状態 (§7-1)、欄の表 (§5-2)、矩形とクリップのあふれ、data の範囲 (`(h−1)·pitch + 行のバイト` を u32 のあふれ込みで)、`gfx2_check_op` が NOSUPPORT でないこと。
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
- 1 か所 `gfx2_cpu_begin(surf, access)`: 読む → `wait(last_write)`。書く → `wait(last_write)`・`wait(last_read)`。`gfx2_cpu_end(surf, access)`: 書いた VRAM は同期層または backend の吐き出しを 1 回、SHADOW の面は dirty を足す。
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

### 6-4. 正本と SHADOW
| 面 | 正本 | HW の操作 | CPU の書き |
|---|---|---|---|
| v2 の VRAM・GART の面 | 自分 (影は持たない) | 可 | lock の間に直接 |
| 同期層の SHADOW の表示面 (PEGC・Trident の SHADOW) | RAM の影 (sw の `view.base` = RAM の正本、`view.sync = NULL`。VRAM への転送は C13 の present だけ) | **しない** (`check_op` が SW を返す。Trident の GE も使わない) | 影に書き dirty に足す。present が `gfxsync_write_rows` で写す |
| 同期層の DIRECT の表示面 | VRAM | 可 (GE の後は `tri_sync` が fence の代わり) | lock は RAM の写し (staging) を渡す: lock で `gfxsync_read_rows` (DISCARD なら読まない)、unlock で lock した矩形を `gfxsync_write_rows`。生の VRAM の VA は渡さない |
- 同期の対象は「同期の対象の別名面」(C14 が `gfx2_surf_alias` で作る表示面) だけ。通常の面 (SYSMEM・VRAM・GART の create の面) は ctx を拾わない (`gfx2_sync_get` を呼ばず 1 行目の扱い)。別名面のどの行を使うかは `gfx2_sync_get(a, head, &ctx, &mode)` (C1、§11-2) の 1 口だけで決める: `NOTFOUND` = 同期層の面でない (1 行目)、`mode == GFXSYNC_SHADOW` = 2 行目、`GFXSYNC_DIRECT` = 3 行目。C7 (lock の staging)・C8 (SW へ回す)・C9 (`gfx2_view.sync`)・C13 (present) はこの値だけを見る。面と head の対応は「別名の面の `role` の head」(C14 が作成の時に `gfx2_surf_alias` の `head` に入れる)。
| v1 の GeForce の DISPLAY (H2 の DIRECT) | VRAM | v1 の INDEX8 の口だけ | v1 の規則 (H2) |

### 6-5. v1 との境目 (gfx/v2/v1_bridge.c)
- 起動時に v1 の DISPLAY と CLIENT を v2 の表に**別名の面** (`pool=VRAM か SYSMEM`、`usage=SCANOUT|V1`、`owner=GFX2_OWNER_SCREEN`、free と gfx2_lock は INVAL) として載せる。
- 使える者: `gfx2_surf_get` は `owner == GFX2_OWNER_SCREEN` の面を、呼んだ者が `gfx_screen_owner()` (gfx_core.c:461) の持ち主の時だけ返す (持ち主の照合の唯一の例外)。sid の取得は `int gfx2_screen_surface(u32 adapter, u32 head, u32 role, u32 *sid)` (role = DISPLAY・CLIENT、持ち主でなければ BUSY)。
- 作り直し: v1 の mode の変更・`ledger_surface_regen`・H4 の方式の切り替えの後に gfx_core が `v1_bridge_rebind()` を呼ぶ (C14 の担当、呼ぶ行は gfx_core.c の 3 か所も C14 が足す)。C14 は v1 の今の値から `struct gfx2_alias_desc` を作って C4 の `gfx2_surf_alias_update` を呼ぶだけで、sid・gen・形状・kva を自分では書かない (P1-4)。
```c
/* gfx/v2/v1_bridge.c (C14)。起動時は gfx2_surf_alias で作り、以後は update だけ */
void v1_bridge_rebind(void)
{
    for (role = DISPLAY; role <= CLIENT; role++) {
        struct gfx2_alias_desc d; struct gfx2_surf *s = alias[role];
        if (!s) continue;                              /* 起動時の gfx2_surf_alias が失敗した役 */
        if (v1_alias_desc(role, &d)) {                 /* v1 に面が無い (テキストの画面など) */
            if (s) gfx2_surf_alias_update(s, NULL);    /* 0: 旧 sid は STALE、面は形を持たない (w=h=0) */
            continue;
        }
        if (gfx2_surf_quiesce(s, GFX2_REBIND_DL))      /* TIMEOUT/STALE: 装置の失敗は C3 が LOST にした */
            continue;                                  /* 別名は旧のまま (LOST なら free と desc だけ通る) */
        (void)gfx2_surf_alias_update(s, &d);           /* 0 / BUSY (lock か lease 中、C14 が先に閉じる決まり) */
    }
}
```
- v1 の入口 (present_rect・fill_rect・blit・fill_rop・put・mono・get・gfx_hw_submit:70・gfx_hw_get:95・osri_show・set_mode・page_flip・DISPLAY の lease の開始と終わり) は全部 `v1_bridge_begin(alias, access)`/`end` を通す: CPU も v1 の HW も触る前に §6-2 の待ち (v2 の束が別名に書いた分を待つ)。v1 の GF の HW は同期なので (D9) `v1_bridge_end` の fence は 0 で、別名の面に番号を記録しない。v1 が非同期の HW を持つ時に初めて C3 に口を足す (A0 の版上げ)。
=====END

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
- sid: 下位 10 bit = 表の位置 (1,024 面)、上位 22 bit = 世代 (解放で +1)。呼んだ AS の owner と面の持ち主が違えば INVAL。
- lock の flags: READONLY・DISCARD・DONOTWAIT。**DISCARD でも待ちは同じ** (`wait(last_write)`・`wait(last_read)`)。DISCARD の効果は「中身を読まない」だけ: staging の面は lock の時の読みを省く。DONOTWAIT で未完了なら `OS32_ERR_BUSY`。

### 7-2. 確保と GF の予約
- pool: VRAM・GART・SYSMEM (MANAGED は libos32gfx)。usage: RENDER2D・SCANOUT・CURSOR・STAGING・TEXTURE (欄だけ)。tiling は backend が選び、CPU から線形に見える物だけ。
- VRAM の heap (gfx/v2/vram_heap.c) が置き場を決め、backend の `alloc` が装置の予約を作る。GF は `gf_vram_reserve(ch, surf, off, bytes, node)` を呼び、返った予約の id・世代を面の私物に持つ (`gf3_surface_check` がそのまま通る)。解放は GPU の使用の終わりと写像の解除の後に `gf_vram_release`。
- GART の面を GF で描くため `gf3_surface` の末尾に `u32 dma` (VRAM_DMA か GART_DMA の handle) を足し、`gf3_surface_set`・`gf3_surface_check` を領域ごとに分ける (段 G8)。GART の面の検査は予約ではなく GART の表 (§8) の範囲で行う。

### 7-3. 走査面の解放
- 面は `scan_ref` を持つ: head の今の走査面で +1、保留中の flip の先で +1。flip の反映 (vblank の後に backend が報告) か head の停止で −1。
- `free` は `scan_ref > 0` か fence が未完了なら CLOSING にして戻る。vblank の処理と `fence_query`・submit の入口で CLOSING を見回り、両方 0 になったら実際に返す (写像の解除 → 予約の解放 → heap/GART)。写像の解除が IO なら backing を返さず `GFX2_SF_KEEP` を立てて隔離へ (下の (1))。
- AS の終了 (`gfx2_owner_release`、呼ぶ所は exec/exec_finish.inc:33-34 の `lease_revoke_all` の直後、36 行の poison の判定より前。X1 の担当):
```c
/* gfx/v2/owner_release.c (C15)。0 = この AS から完全に切り離した、
 * OS32_ERR_IO = CPU の写像を外せなかった (呼び手は as を poison する) */
int gfx2_owner_release(struct addrspace *as)
{
    int poison = 0;
    for each surf s with s->owner == as->owner:
        if (s->map_as == as) {
            if (gfx2_unmap(s)) { poison = 1; s->flags |= GFX2_SF_KEEP; }  /* (1) 外せない: PTE が残る。backing は返さない */
            gfx2_map_detach(s);                              /* 必ず: map_as = NULL・map_va = map_npages = 0 (PTE には触らない) */
        }
        gfx2_staging_drop(s);                                /* (2) C7: staging を返す (無ければ何もしない) */
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
- 帯: lease の帯の上の方を使う。`MEM_GFX2_MAP_BASE 0xF8000000`・`MEM_GFX2_MAP_END 0xFE000000` (96MB) と、lease の確保の上限 `MEM_LEASE_ALLOC_END = MEM_GFX2_MAP_BASE` を memmap.h に足し、exec/lease.c:69 の上限を `MEM_LEASE_ALLOC_END` にする (M3)。帯の検査 (lease.c:176・paging_lease_unmap) は `MEM_LEASE_END` のままで新しい帯を含む。
- PT: 新しい帯の PDE は今の `as->lease_pt_phys[k]` (k = (va − MEM_LEASE_BASE) >> 22) をそのまま使う。無ければ `paging_gfx2_map` が `pgalloc_alloc_phys(as->owner, 1)` で取り、0 で埋め、`pd[LEASE_PDE + k] = phys | PAGE_RW | PTE_USER` にする (lease_context の検査と同じ形)。AS の破棄 (paging_addrspace.inc:115-120) が今どおり返す。
- 張る: `paging_gfx2_map(as, va, pa, npages, pte_flags)` (M2) を連続の区間ごとに。文脈は `paging_lease_root_context(as)` (CR3 が kernel かその AS、IRQ・例外の外) を要する。KAPI の中 (CR3 = その AS) から呼べる。PTE は `pa | PAGE_RW | PTE_USER | cache`、VRAM は exec/lease.c:26 `alias_cache` と同じ属性、GART は WB。区間の途中で失敗したら gfx2 が張った区間を `paging_lease_unmap` で外して INVAL (全部か無し)。新しく取った PT は空なら `paging_lease_unmap` が返す。TLB は 1 頁ずつ `invlpg` (CR3 がその AS の時)、kernel の CR3 の時は不要 (その AS の TLB は CR3 の切り替えで消える)。
- 外す: 今の `paging_lease_unmap(as, va, npages)` (帯の中なら ledger に縛られず、頁を返さない)。free・AS の終了・LOST で外す。VA は gfx/v2/lock.c の AS ごとの bitmap (§11-2 `struct gfx2_vaspace`) が配る。
- 写像は lock をまたいで残すが、触ってよいのは lock の間だけ (シングルタスクなので PTE の付け外しで縛らない)。libos32gfx の wrapper は必ず lock を通す。

## 8. GPU から見える RAM

| 項目 | 決め |
|---|---|
| 取り方 | 必要な時に `pgalloc_alloc_n_owner` で 4KB 頁ごと (連続でなくてよい)。持ち主は面の AS の owner。dma_pool は使わない |
| 上限 (PM 決定 #1) | **遅延確保の上限 8MB** (`GFX_GART_KB=8192`、system.cfg)。予約はしない。越えれば `OS32_ERR_NOSPC` (`OS32_ERR_NOMEM` は無い) |
| pin | pgalloc に頁ごとの pin の数 (`u8 pin_map[PHYSMEM_MAX_PFN]`、pgalloc_owner.inc、M1) を足し、`pgalloc_free_n_owner`・`ledger_transfer`・`ledger_reclaim_owner` の**全部**で pin の頁があれば全件不変で拒否し `ledger_bad_free` に数える (reclaim は pin の頁を飛ばして返さず、戻り値 0)。pin の頁を動かせるのは `ledger_transfer_pinned` (隔離へ移すため) だけ |
| 物理の表 | 中核が `u32 pfn[]` を持ち、backend の `gart_bind(pfn, n, *gpu_off)`・`gart_unbind`。GF6200 は NV44 の GART の頁表 (nouveau `nvkm/subdev/mmu/nv44.c`、MIT)、ATI は PCI GART (`r100_pci_gart_*`、MIT)。GF は GART の DMA の物 (新 handle) を作る |
| cache | WB (bus master は snoop)、DMA_PHYS_LIMIT は `dma_range_ok` |
| 解放 | fence を待つ → `gart_unbind` → 写像を外す → pin を外す → `pgalloc_free_n_owner` |
| 回収の失敗 | GPU の待ちが終わらない GART の頁は §7-3 の (5) で隔離の owner (`GFX2_QUAR_OWNER`、gfx2 の初期化で `ledger_owner_new(LEDGER_KIND_PERSIST, 0, "gfx2-quar", &GFX2_QUAR_OWNER)`) へ移し、AS の回収と retire は今どおり進む。隔離の頁は adapter の停止が確かめられた時に `gfx2_quarantine_drain` が unpin → `pgalloc_free_n_owner(GFX2_QUAR_OWNER, pfn, n)` で返す。LOST_HELD の間は返さない |
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
- C (ACTIVE でない側の TVRAM にコンソールを出し続ける) は HAL v2 の中で設計しない。別の票 **CONSOLE_DUAL** (仮名、「GUI の間も ACTIVE でない側の TVRAM にコンソールを出し続ける」) で設計する。HAL v2 の段 (C1 など) は C の仕事を持たない。PC-98 の adapter は `kind = GFX2_KIND_PC98`・`flags = 0` で登録するだけ (A の列挙)。
- CONSOLE_DUAL が受け持つ経路 (第 4 回のレビューの指摘): gfx/gfx_core_init.inc:9-14 の GF/Trident の早い戻り / kernel/console/console.c:53-56・195-204 の GUI 中の TVRAM の出力の停止 / console.c:434-443 の復帰の時の消去 / init/kernel.c:473 などの切り替えの時の消去 (gfx_core.c:267-270 の `_gfx_common_init` の消去を含む)。

### 9-2. 表示の口 (公開) と v1 への変換 (完全な署名は §11-1)
| gfx2_* | GfxBackendV2 | v2 の無い backend (PEGC・Trident) の v1 の hook |
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
rc = a->be->mode_set(a->ctx, head, conn_mask, m, s);            /* 失敗時は backend が退避した状態へ戻す */
if (rc) return rc;                                              /* 世代も scan_ref も変えない */
gfx2_scan_ref_swap(a, head, s);  gfx_policy_scan_changed();   /* H4 の唯一の遷移の口 (gfx_policy.h:14-16、D3)。module の側の G* から呼ぶ所も同じ口 */
=====END

if (fullscreen_app(caller)) a->restore_on_leave = 1;            /* 全画面を抜ける時に gshell の mode と走査面へ戻す */
return 0;
```

## 10. CPU 直描きの同期バックエンド層

置き場: `/etc/gfx/sync.cfg` (`SYS_GFXSYNC_CFG`、[C4])。r1 §10 の書式・項目 (mode・unit・method・gap_ns・flush・read_ns・read) のまま。PM 決定 #2: `TRI_WRITE_GAP` はここへ移す (既定 80ns)。旧値の引き継ぎと「sync.cfg が system.cfg より優先」は配備の手順で保証し、カーネルは system.cfg の旧鍵を読まない。
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
| flush=SERIALIZE | 中核の `cpu_serialize()` (cpuid) を使う、hook 不要 |
- 端数: 行の先頭と末尾の unit に満たないバイト・未整列の部分は 1 バイトずつ (今の present.c:57 と同じ)、method の待ちは unit の書きのあとごと。境界の検査 (present.c:18-25 の相当) は bind の `bytes`・`pitch` で中で行う。
- Trident の移し方は r1 §10 のとおり (write_gap.c → sync_gap.c、regs.c:117-138 と present.c:26-56 → sync_write.c、present.c は検査と呼び出しだけ、trident_module.c:198-200 を削除)。

## 11. 今の版との関係と A0 (宣言だけを先に置く段)

- 着地の前提: GFX_ACCEL_HAL の H1〜H6 と G1〜G3 が main に入っていること。v1 の INDEX8 の口・`GFX_ScreenInfo`・`GFX_CAP_*` は変えない。
- 着地の前提は満たした (D1、4f971604)。
- **A0 は宣言と仮の実体だけを置く段** (1 人、ほかの全段より先)。中身: §11-1〜11-7 の全部のヘッダ、kapi.json の末尾の gfx2_* (83→84、全部 NOSYS)、内部の全関数の **weak の仮の実体** (gfx/v2/a0_weak.c・gfx/sync/a0_weak.c・drivers/video/geforce/gf_v2_weak.c、全部 `return OS32_ERR_NOSYS;` か 0)、大きさと offset の検査、module_schema・hwdev_layout・build の登録。A0 の後は各段が単独で `make kernel` を通せる。**A0 以外の段はヘッダを編集しない** (足りなければ止めて PM へ)。
- 仮の実体の掃除 (P2-2): weak の仮の実体は本来の関数名 (`gfx2_surf_new` 等) で置くので、記号名では見分けない。A0 が `tools/checks/gfx2_wiring.txt` (行 = `I1 kernel gfx2_surf_new` の形、I ごとの対象関数を §11-2〜11-7 の表から全部) と `tools/checks/gfx2_no_weak.sh <I>` を作る。検査は最終の build/out/os32.elf と geforce module の ELF に対し、対象の各記号が (a) `nm` の型が `T`/`t` (`W`・`V`・`U` は不合格)、(b) 番地が a0_weak.o の .text の区間 (build/out/os32.map の a0_weak.o の行から) の外、の両方。各 I の合格は「割り当てた §13 の試験の合格の記録 + `gfx2_no_weak.sh <I>` の 0」の両方 (§12)。

### 11-1. 公開 (sdk/include/os32/os32_gfx2.h、C89、A0)
```c
#define GFX2_MAX_CMDS 64
#define GFX2_OP_FILL 1  #define GFX2_OP_COPY 2  #define GFX2_OP_STRETCH 3  #define GFX2_OP_MONO 4
#define GFX2_OP_HOSTDATA 5  #define GFX2_OP_READBACK 6  #define GFX2_OP_ROP4 7  #define GFX2_OP_ALPHA 8
#define GFX2_OP_LINE 9  #define GFX2_OP_SET_PATTERN 10  #define GFX2_OP_MAX 10
#define GFX2_HOW_NOSUPPORT 0  #define GFX2_HOW_SW 1  #define GFX2_HOW_HW 2
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
struct gfx2_adapter {
    u32 index, gen, state;               /* C1 (登録)・C3 (喪失で gen+1・state) */
    GfxBackend *v1;                      /* C1。hook は呼ぶたびに a->v1-> から読む (H4 が書き換える、D2) */
    const struct GfxBackendV2 *be; void *ctx;   /* C1 (be は kernel の中の表、§11-7) */
    u32 kind;                            /* C1。GFX2_KIND_PC98 1・PEGC 2・TRIDENT 3・GEFORCE 4・CIRRUS 5 */
    GFX2_Caps caps;                      /* C10 */
    struct gfx2_fence_st fence;          /* C3 だけが書く (reserved は reserve、issued は commit) */
    struct gfx2_heap heap;               /* C5 だけが書く */
    u32 heads; struct gfx2_head head[2]; /* C12・C13 だけが書く */
    u32 restore_on_leave;                /* C12 */
};   /* 同期層の ctx は持たない。gfx2_sync_get で毎回引く (P1-2) */
struct gfx2_alias_desc {                 /* C14 が v1 の今の値から作り、C4 に渡す */
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
    u8 *staging; u32 staging_pitch;      /* C7 (同期の面の lock の写し) */
    u8 *kva;                             /* C4 (作成の時に be->kva か SYSMEM の VA) */
    u32 heap_off;                        /* C5 (gfx2_heap_alloc の結果、C4 が格納) */
    u32 gart_first, gart_n, gart_moved; /* C6 だけ (gart_moved は隔離へ移せた頁数) */
    u32 priv[4];                         /* backend の私物 (GF: 予約の id・世代・node の番号、G9) */
};
struct gfx2_vaspace { u32 as_gen; u32 bits[(MEM_GFX2_MAP_END - MEM_GFX2_MAP_BASE) / 4096 / 32]; }; /* C7、4 本 */
extern struct gfx2_adapter gfx2_adapters[GFX2_MAX_ADAPTERS /* 4 */];  /* 実体は C1 */
extern struct gfx2_surf gfx2_surfs[GFX2_MAX_SURFACES /* 1024 */];     /* 実体は C4 */
```
| 関数 (全部 gfx2_int.h に宣言) | 戻り値 | 実装の段 | 呼ぶ段 |
|---|---|---|---|
| `struct gfx2_adapter *gfx2_adapter_get(u32 a)` | NULL = 無い | C1 | 全部の公開の入口 |
| `int gfx2_adapter_register(GfxBackend *v1, u32 kind, u32 flags)` | 0/FULL/INVAL (abi 不一致・hook が .text の外・ACTIVE が 2 つ目) | C1 (§11-7 の検査) | gfx_core (C1 が呼ぶ行を足す) |
| `void gfx2_adapter_unregister(GfxBackend *v1)` | — (adapter を LOST・held=0 に、be = NULL) | C1 | geforce_module.c・trident_module.c の停止の道 (G1・T1 が 1 行) |
| `int gfx2_sync_publish(u32 kind, u32 head, GfxSyncCtx *ctx)` | 0/INVAL (kind・head の範囲外) | C1 (static `GfxSyncCtx *sync_tab[GFX2_KIND_MAX + 1][2]` を書くのはここだけ) | P1 (直接)・T1 (module の import)。bind の後・unbind の**前** (NULL を publish してから unbind) |
| `int gfx2_sync_get(const struct gfx2_adapter *, u32 head, GfxSyncCtx **ctx, u32 *mode)` | 0 (mode = `gfxsync_mode(ctx)`) / OS32_ERR_NOTFOUND (同期層でない、*ctx = NULL) | C1 | C7・C8・C9・C13 (§6-4 の判定の唯一の口) |
| `struct gfx2_surf *gfx2_surf_get(u32 sid, u32 caller_owner, int *rc)` | NULL + rc (INVAL/STALE) | C4 | C7・C9・C12・C13・C14 |
| `int gfx2_surf_new(struct gfx2_adapter *, u16 w, u16 h, u8 fmt, u8 pool, u8 usage, u32 owner, struct gfx2_surf **out)` | 0/INVAL/NOSPC | C4 (heap は C5、GART は C6、予約は `be->alloc`) | 公開 create だけ |
| `int gfx2_surf_alias(struct gfx2_adapter *, const struct gfx2_alias_desc *, struct gfx2_surf **out)` | 0/FULL/INVAL | C4: 表の空きに `owner = GFX2_OWNER_SCREEN`・`usage = SCANOUT\|V1`・`flags = ALIAS`、heap・GART・`be->alloc` は呼ばない | C14 (起動時) |
| `int gfx2_surf_alias_update(struct gfx2_surf *, const struct gfx2_alias_desc * /* NULL = 形無し */)` | 0/BUSY (state が IDLE でない・`last_write`/`last_read` が未完了)/INVAL (ALIAS でない) | C4: 失敗時は何も変えない。成功時は gen+1 (旧 sid は STALE) → 形状・pitch・gpu_off・kva を写す。backing は解放も確保もしない | C14 (`v1_bridge_rebind`) |
| `int gfx2_surf_destroy(struct gfx2_surf *)` | 0/BUSY/INVAL (ALIAS) | C4。順: `gfx2_unmap` (IO なら KEEP を立てて BUSY、backing は返さない) → `be->release` → C5/C6 → 表を空き (gen+1) | 公開 free・C15・見回り |
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
| `int gfx2_cpu_begin(struct gfx2_surf *, u32 access, u32 dl_us)`・`void gfx2_cpu_end(struct gfx2_surf *, u32 access, const struct gfx2_rect *)` | 0/TIMEOUT/STALE | C3 | C7・C9 (sw)・C13・C14 |
| `int gfx2_surf_quiesce(struct gfx2_surf *, u32 dl_us)` | 0/TIMEOUT/STALE、何も変えない | C3 | C12・C15 |
| `void gfx2_adapter_lost(struct gfx2_adapter *, int held)`・`void gfx2_adapter_recovered(struct gfx2_adapter *)` | — | C3 | C9・G4 経由の rc |
| `int gfx2_heap_init(struct gfx2_heap *, u32 base, u32 size)`・`int gfx2_heap_alloc(struct gfx2_heap *, u32 bytes, u32 align, u32 *off)`・`int gfx2_heap_free(struct gfx2_heap *, u32 off)` | 0/NOSPC/INVAL | C5 | C1 (init)・C4 |
| `int gfx2_gart_alloc(struct gfx2_adapter *, struct gfx2_surf *, u32 owner)`・`int gfx2_gart_free(struct gfx2_adapter *, struct gfx2_surf *)`・`int gfx2_gart_pfns(const struct gfx2_surf *, const u32 **pfn, u32 *n)`・`int gfx2_gart_quarantine(struct gfx2_surf *)` | 0/NOSPC/BUSY | C6 (pin は M1、`be->gart_bind`) | C4・C7・C15 |
| `int gfx2_map(struct gfx2_surf *, struct addrspace *, u32 *va)`・`int gfx2_unmap(struct gfx2_surf *)` | 0/NOSPC/IO。unmap の IO は map_* を変えない | C7 (`paging_gfx2_map`・`paging_lease_unmap`) | C7 (lock)・C4・C15 |
| `void gfx2_map_detach(struct gfx2_surf *)` | — (map_as = NULL・map_va = map_npages = 0。PTE と VA の bit には触らない) | C7 | C15・C4 (destroy の IO の後) |
| `void gfx2_staging_drop(struct gfx2_surf *)`・`void gfx2_vaspace_drop(struct addrspace *)` | — (staging を kfree / as の枠の as_gen を 0、bit を全部 0) | C7 | C15 |
| `int gfx2_head_restore_v1(struct gfx2_adapter *, u32 head)` | 0/IO | C13 | C15・C12 (全画面を抜ける) |
| `int gfx2_surf_quarantine(struct gfx2_surf *)`・`void gfx2_quarantine_drain(struct gfx2_adapter *)` | 0/IO (§7-3 のコード。IO でも owner・state は隔離に済む) | C15 | C15・C3 (回復)。drain は KEEP の面を飛ばす |
=====END

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
- 束の終わり: C9 は `k_done` が 1 以上なら `be->kick`、`*fence = a->fence.issued`。sw の命令は `gfx2_cpu_begin` が前の HW の命令の commit 済みの番号を待つので、混在の束 (hw・sw・hw) で CPU が描画の前に触ることは無い。

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
    const u32 *pal;                   /* INDEX8 → ARGB (gfx_get_palette の写し) */
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
extern const gfx2_sw_fn gfx2_sw_table[GFX2_OP_MAX + 1];   /* 実体は C9 (submit.c)。CKEY 付きの COPY は sw_ckey */
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
/* include/memmap.h (M3): MEM_GFX2_MAP_BASE 0xF8000000UL・MEM_GFX2_MAP_END 0xFE000000UL・
   MEM_LEASE_ALLOC_END MEM_GFX2_MAP_BASE。exec/lease.c:69 を MEM_LEASE_ALLOC_END にするのも M3 */
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
struct gf_v2 {                                   /* 実体は G1 (gf_v2_ops.c の static 1 つ) */
    struct gf3_ctx *g3; struct gf2_chan *ch;
    struct gf_v2_ring ring;                      /* G4 だけが書く */
    u32 completed, failed_upto;                  /* G4 */
    struct gf_vram_reservation_node node[GFX2_MAX_SURFACES]; /* G9 (surf の priv[2] が番号) */
    u32 gart_handle, gart_base, gart_pages;      /* G5 */
};
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
/* G2 (gf_v2_display.c)。GfxBackendV2 の同名の hook の型そのもので、ctx は struct gf_v2 * */
int gf_v2_enum_head(void *, u32 head, GFX2_HeadInfo *);
int gf_v2_enum_conn(void *, u32 conn, GFX2_ConnectorInfo *);
int gf_v2_enum_mode(void *, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *);
int gf_v2_edid(void *, u32 conn, u32 block, u8 *out128);
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
void gf3_range_query_set(struct gf3_ctx *, gf3_range_fn, void *opaque);  /* G8 が実装 (accel_surface.c)。G1 が v2 の初期化で
                                                    gf3_range_query_set(g->g3, gf_v2_gart_range, &gf_v2_inst) を呼ぶ */
/* G8 の gf3_surface_check: s->dma == GF3_HANDLE_GART_DMA なら
 *   c->range_q ? c->range_q(c->range_opaque, s->dma, s->off, s->bytes) : -EINVAL
 * を予約の照合 (gf_vram_match) の代わりに使う。VRAM_DMA (dma == 0) は今のまま */
struct gf3_pattern { u32 kind; u32 mono[2]; u32 color[64]; u32 fg, bg; };
int gf3_ckey_set(struct gf3_ctx *, bool on, u32 key, enum gf3_fmt, u32 dl);   /* G3 */
int gf3_alpha_set(struct gf3_ctx *, u32 const_a, bool per_pixel, u32 dl);     /* G6 */
int gf3_rop3_set(struct gf3_ctx *, u8 subc, u8 rop3, const struct gf3_pattern *, enum gf3_fmt, u32 dl); /* G7 */
/* struct gf3_surface の末尾に u32 dma (0 = VRAM_DMA で今と同じ、GF3_HANDLE_GART_DMA) を A0 が足す。意味の実装は G8 */
```

### 11-7. 表の組み立て・登録・module・build
```c
typedef struct GfxBackendV2 {   /* include/gfx_hal2.h (A0)。戻り値は OS32_ERR_*、第 1 引数は ctx。全部の hook は必須
                                   (使わない物は NOSYS を返す関数を置く。NULL は登録で拒む) */
    u32 abi; void *ctx;
    int (*caps)(void *, GFX2_Caps *);                                   /* 副作用なし */
    int (*check)(void *, const struct gfx2_exec *);                     /* 0 = HW でできる、NOSYS = できない */
    int (*heap_range)(void *, u32 *base, u32 *size);                    /* heap に渡す VRAM の範囲 */
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
| kernel の .text の境界: build/os32.ld の `.text` に `__os32_text_start = .;` (kentry.o の前)・`__os32_text_end = .;` (`*(.text.*)` の後) | A0 | — |
| GF の 1 表 (G1〜G9 の関数を並べる) と backend_geforce.c:719-734 の `.v2 = &gf_backend_v2` | G1 | `const GfxBackendV2 gf_backend_v2` (gf_v2_ops.c、module の中) |
| GF の写し (D7): geforce_module.c の `geforce_start` (:241 の `direct_ops` の検査の後、公開 :334 の前) に、`candidate.v2` の表が module の ro の中にあること (`direct_ops` と同じ :221-227 の型) と各 hook の `module_text_contains` を確かめてから `if (candidate.v2) { gf_v2_kcopy = *candidate.v2; candidate.v2 = &gf_v2_kcopy; } candidate.v2_mod = r;` (module が v2_mod に何を書いても r で上書き)。停止の道に `gfx2_adapter_unregister(&gfx_backend_geforce)` | G1 (geforce_module.c の 2 か所) | `static GfxBackendV2 gf_v2_kcopy` (kernel) |
| PEGC の表 (caps・enum・heap_range=0・kva) と backend_pegc.c の `.v2` | P1 | `pegc_backend_v2` (pegc_v2.c) |
| Trident: 表は作らない (`v2 = NULL`、中核が v1 から写す)。同期の bind は T1 | T1 | — |
| 登録: gfx_core の backend の選択の直後に `gfx2_adapter_register(g_backend, kind, ACTIVE)`、他の probe が通った物と PC-98 の内蔵は `flags=0` (kind は gfx_core.c:146 と同じ形の pointer の比較) | C1 (gfx_core.c の呼ぶ行も C1) | — |
| module: trident の import に `gfxsync_bind`・`gfxsync_write_rows`・`gfxsync_read_rows`・`gfxsync_flush`・`gfxsync_unbind`・`gfx2_sync_publish` (module_schema.py と生成物 build/out/trident/trident_imports.h の両方)。geforce は import 無し・export は今の backend の表 (中の `v2`)。両方の ABI の世代を上げる | A0 (module_schema.py と生成物) | `TRIDENT_MODULE_ABI_GEN`・`GEFORCE_MODULE_ABI_GEN` |
| build: gfx/v2/*.c・gfx/sync/*.c を build/kernel.mk の C_KERNEL に、gf_v2_*.c を geforce module の一覧に、a0_weak.c を両方に | A0 (各段は自分のファイルが一覧にあることだけ前提) | — |
| 登録の検査 (P1-6): `abi == GFX_HAL2_ABI`、全 hook が非 NULL、全 hook が「`v2_mod == NULL` なら kernel の `[__os32_text_start, __os32_text_end)`、非 NULL なら `module_text_contains(v2_mod, p)` (module_runtime.h:56)」の中。`v2 == NULL` (Trident・PC-98) は検査しない | C1 | — |
=====END


## 12. 段と順 (1 機能 1 ファイル、1 行 = 1 段 = 1 実装者)

順は固定: **A0 → (実装の段、並列) → I1 → I2 → I3**。実装の段は単体の host 試験と `make kernel` が通れば「部品の実装済み」で、**機能の完了とは数えない**。完了は、その部品を含む I の段の合格だけ (weak の仮の実体が残らないことを含む)。

| 段 | ファイル | 呼ぶ (宣言の所) | 呼ばれる | 書く共有の状態 | 行 | I |
|---|---|---|---|---|---|---|
| A0 | §11 の全ヘッダ・kapi.json と生成物・a0_weak.c ×3・module_schema.py・hwdev_layout.json (GfxBackend・gf3_ctx)・build/kernel.mk・build/os32.ld (.text の境界)・tools/tests/gfx2_abi_host.c・tools/checks/gfx2_wiring.txt・gfx2_no_weak.sh | — | 全部 | — | 700 | — |
| C1 | gfx/v2/backend_reg.c (+gfx_core.c の登録の行) | `be->caps`・`gfx2_heap_init`・`module_text_contains`・`gfxsync_mode` | 全部の公開の入口・P1・T1 (publish) | `gfx2_adapters[]` の index・v1・be・ctx・kind、`sync_tab` | 220 | I1 |
| C2 | gfx/v2/format.c | — | C9・S*・U1 | — | 200 | I2 |
| C3 | gfx/v2/fence.c | `be->fence_emit/state/wait`・`gfx2_surf_state_set`・`gfx2_quarantine_drain` | C4・C7・C9・C12〜C15 | `adapter.fence` (reserved・issued)・`gen`・`state`、`surf.last_*` | 220 | I1 |
| C4 | gfx/v2/surface.c | C5・C6・C7 (`gfx2_unmap`・`gfx2_map_detach`)・`be->alloc/release/kva` | 公開・C7・C9・C12〜C15 | `gfx2_surfs[]` (§11-2 の C4 の欄、別名の作成と更新を含む) | 320 | I1 |
| C5 | gfx/v2/vram_heap.c | — | C1・C4 | `adapter.heap` | 150 | I1 |
| C6 | gfx/v2/gart.c | `pgalloc_alloc_n_owner`・`pgalloc_pin/unpin`・`ledger_transfer_pinned`・`be->gart_bind/unbind` | C4・C7・C15 | `surf.gart_*`・pfn の表 (gart.c の static) | 200 | I1 |
| C7 | gfx/v2/lock.c | `paging_gfx2_map`・`paging_lease_unmap`・C1 (`gfx2_sync_get`)・C3・C4・`gfxsync_read/write_rows` | 公開・C4・C15 | `surf.map_*`・`staging`、`gfx2_vaspace[4]` | 270 | I1 |
| C8 | gfx/v2/check_op.c | C2・`be->check` | 公開・C9 | — | 150 | I2 |
| C9 | gfx/v2/submit.c | C3・C4・C8・S*・`be->emit/readback/kick` | 公開 | `k_ops`・`ex`・パターン (static)、`gfx2_sw_table` | 260 | I2 |
| C10 | gfx/v2/caps.c | `be->caps` (無ければ v1 の GfxMode から) | 公開・C1 | `adapter.caps` | 120 | I2 |
| C11 | gfx/v2/enum.c | `be->enum_*`・`edid` (無ければ v1 から) | 公開・C12 | — | 200 | I3 |
| C12 | gfx/v2/modeset.c | C3・C4・C11・`be->mode_set`・`gfx_policy_reeval` (H4) | 公開 | `adapter.head[]`・`restore_on_leave` | 160 | I3 |
| C13 | gfx/v2/display.c | `be->flip`・`flip_done`・`vblank_wait`・`palette`・`gamma`・`cursor_*`・`dpms` か v1 の hook・C4・`gfxsync_write_rows` | 公開・C15 | `adapter.head[].pend_sid`、`surf.dirty` を 0 に | 240 | I3 |
| C14 | gfx/v2/v1_bridge.c (+gfx_core.c 3 か所・gfx_hw_submit.c:70・95・surface_query.c の呼ぶ行) | C3・C4 (`gfx2_surf_alias`・`alias_update`) | v1 の入口 | 別名の面 | 220 | I2 |
| C15 | gfx/v2/owner_release.c | C3・C4・C6・C7 (`map_detach`・`staging_drop`・`vaspace_drop`)・C13 | X1 | `surf.owner`・`flags` の KEEP (隔離) | 200 | I1 |
| S1〜S11 | gfx/v2/sw_fill.c・sw_copy.c・sw_rop.c・sw_rop4.c・sw_mono.c・sw_hostdata.c・sw_readback.c・sw_stretch.c・sw_ckey.c・sw_alpha.c・sw_line.c | C2・`gfxsync_row_*` (view.sync の時) | C9 | — | 1450 | I2 |
| M1 | mm/pgalloc_owner.inc | — | C6 | `pin_map[]` | 90 | I1 |
| M2 | mm/paging_gfx2.inc (paging.c に include) | `pgalloc_alloc_phys` | C7 | `as->lease_pt_phys[]` | 80 | I1 |
| M3 | include/memmap.h・exec/lease.c:69 | — | C7・lease | — | 10 | I1 |
| X1 | exec/exec_finish.inc:34 の後 | `gfx2_owner_release`・`paging_addrspace_poison` | — | `as->appmem_poisoned` (poison の口で) | 15 | I1 |
| Y1〜Y4 | gfx/sync/sync_cfg.c・sync_gap.c・sync_write.c・sync_bind.c | `sysconfig` の読み・`cpu_loops_per_tick` | C7・C13・S*・T1・P1 | `gfxsync_ctx_pool` (Y4) | 500 | I3 |
| T1 | drivers/video/trident/present.c・regs.c・trident_module.c (write_gap.c 削除、停止の道に `gfx2_adapter_unregister`) | `gfxsync_*`・`gfx2_sync_publish` (A0 が import の一覧と生成物に足した物) | v1 present | — | −50 | I3 |
| P1 | drivers/video/pc98/pegc_v2.c (+backend_pegc.c の `.v2`) | `gfxsync_bind`・`gfx2_sync_publish` | C1 | — | 150 | I3 |
| G1 | drivers/video/geforce/gf_v2_ops.c (+backend_geforce.c の `.v2`、geforce_module.c の写しと unregister の 2 か所) | gf3_*・G3〜G9・`gf3_range_query_set` | 中核 (表) | `struct gf_v2` の g3・ch、`gf_v2_kcopy` | 340 | I2 |
| G2 | drivers/video/geforce/gf_v2_display.c | gf4_* | G1 の表 | — | 250 | I3 |
| G3 | hwdev/gf/accel_ckey.c (+accel_objects.c:112 の結び) | gf2_emit | G1 | — | 80 | I2 |
| G4 | drivers/video/geforce/gf_v2_fence.c・hwdev/gf/accel_fill.c・accel_copy.c・accel_scale.c (*_emit の切り出し) | gf2_fence_* | G1・C3 経由 | `ring`・`completed`・`failed_upto` | 220 | I2 |
| G5 | drivers/video/geforce/gf_v2_gart.c | gf2_dma_new・NV44 の頁表 | C6 経由・G8 | `gart_*` | 180 | I1 |
| G6 | hwdev/gf/accel_alpha.c | gf2_emit | G1 | — | 100 | I2 |
| G7 | hwdev/gf/accel_rop3.c | gf2_emit | G1 | — | 180 | I2 |
| G8 | hwdev/gf/accel_surface.c (`gf3_surface_check` の GART の分岐と `gf3_range_query_set`) | `c->range_q` (G5 の `gf_v2_gart_range` を G1 が設定) | gf3_* | `gf3_ctx.range_q`・`range_opaque` | 110 | I2 |
| G9 | drivers/video/geforce/gf_v2_alloc.c | `gf_vram_reserve/release` | C4 経由 | `node[]`・`surf.priv` | 120 | I1 |
| U1 | userland/lib/gfx/v2/*.c (format.c を共有) | 公開の gfx2_* | アプリ | — | 400 | I2 |
| D1 | etc/gfx/sync.cfg・インストーラ | — | Y1 | — | 30 | I3 |
| I1 | 面の道: C1・C3〜C7・C15・M1〜M3・X1・G5・G9 | 確保 → 写像 → lock → 解放 → AS の回収 (隔離を含む)。合格は §13 の I 列が I1 の試験全部 (**新帯の往復 `gfx2_map_selftest` と AS の回収を必ず含む**) + `gfx2_no_weak.sh I1` | | | 結線 | |
| I2 | 描画の道: C2・C8〜C10・C14・S1〜S11・G1・G3・G4・G6・G7・G8・U1 | 混在の束・fence・v1 の橋・libos32gfx。合格は I 列が I2 の試験全部 + `gfx2_no_weak.sh I2` | | | 結線 | I1 の後 |
| I3 | 表示の道: C11〜C13・G2・P1・T1・Y1〜Y4・D1 | 列挙・mode・flip・全画面の出入り・同期層。合格は I 列が I3 の試験全部 + `gfx2_no_weak.sh I3` | | | 結線 | I2 の後 |
合計 カーネル約 6,800 行・ユーザーランド約 400 行。1,000 行を超えるファイルは無い。ATI の backend は別の設計。

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
| 同期の面の lock | DIRECT の面の lock の va が staging (VRAM の VA でない)、unlock で write_rows に lock の矩形 | lock.c の staging の分岐を外す | I3 |
| check_op と submit | 全 op × 形式 × pool で how が同じ | submit.c が check_op でなく caps2d を見る | I2 |
| 同期層 | 設定の欠け・範囲外・hook の欠けで既定、gap の回数が今の trident の値 (65・7・38 の場合) と同じ | sync_bind.c の組み合わせの拒否を 1 つずつ | I3 |
| module | 古い世代の module を拒む・`v2=NULL` で動く・.text の外のポインタを拒む | backend_reg.c の abi の比較を外す | I1 |
| H1/H2 の回帰 | tools/tests/gfx_accel_hal_host.c が無変更で通る | — | I2 |
| fence の順 (P1-1) | fake の backend が呼ばれた順を記録。FILL(hw) → 記録が `emit(f)` → `fence_emit(f)` の順、`issued` は fence_emit の 0 の後だけ f。続く SW の FILL の cpu_begin が f を待つ。emit が IO → `failed_upto = f`・書き先が LOST・`issued` 不変 | submit.c で commit を emit の前に動かす / fence.c の `issued = f` を fence_emit の前に | I2 |
| sync の判定 (P1-2) | fake の ctx を DIRECT で publish → lock が staging・check_op が HW 可・present が NOP。SHADOW で publish し直す → lock は影・check_op が SW。publish(NULL) の後 → NOTFOUND で VRAM の面の扱い。C7・C8・C9・C13 の 4 か所が同じ答え | backend_reg.c の `gfx2_sync_get` が mode を写しの値で返す (publish し直しを見ない) | I3 |
| GART の範囲 (P1-3) | `range_q` 未設定で GART の面の `gf3_surface_check` → EINVAL。設定後: bind 済みの [0,8KB) は 0、[4KB,12KB) は ERANGE、`off+bytes` のあふれ込みは ERANGE。VRAM の面は今の結果と同じ | accel_surface.c の GART の分岐を予約の照合へ戻す | I2 |
| 別名の更新 (P1-4) | `gfx2_surf_alias_update` で gen+1・旧 sid が STALE・kva は渡した値・heap/GART/`be->alloc`/`be->release` の呼び出しが 0 回。lock 中は BUSY で何も変わらない。`gfx2_surface_free(別名)` は INVAL | surface.c の update で gen+1 を外す / destroy の ALIAS の拒否を外す | I2 |
| unmap の失敗 (P1-5) | fake の `paging_lease_unmap` が失敗 + `be->release` が BUSY → owner_release は IO、全部の面で `map_as != as`・`owner != as->owner`・vaspace の枠が空、KEEP の面の backing が heap/GART に戻らない。その後 as を 0xCC で埋めて free し、CLOSING の見回りと drain を回しても as を読まない (host の番兵) | owner_release.c の `gfx2_map_detach` を外す / destroy の IO で KEEP を立てない | I1 |
| .text の検査 (P1-6) | 組込みの表 (hook が host の `__os32_text_start`〜`end` の中) は 0。1 つを .data の番地にすると INVAL。module の表は fake の `module_record` の text の中で 0、外で INVAL。`v2_mod` が NULL の module の表 (写し忘れ) は kernel の区間で見るので INVAL | backend_reg.c の `v2_mod == NULL` の分岐を「検査しない」に | I1 |
| weak の検査 (P2-2) | 対象の関数を weak だけの版でリンク → `gfx2_no_weak.sh I1` が 1、強い実体のある版で 0。weak の仮の実体と同じ名の強い実体が a0_weak.o の区間に来た版 (人工) も 1 | gfx2_no_weak.sh の型の検査を記号名の検査に戻す | I1 |

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

### 13-2. NP21/W (PEGC)
- 列挙・能力の出力 (I3)、v2 の sw の 2D の画面の CRC (I2)、GUI ↔ 全画面アプリの出入りで v1 の DISPLAY の lease 中の v2 の投入が BUSY、抜けた後に gshell の画面が戻る (I3)、アプリの終了で面・写像・GART が回収される (台帳の自己検査、I1 の必須)。
- 新しい帯の写像の往復: カーネルの自己検査 `gfx2_map_selftest` (paging_map_user_keep_selftest と同じ形) が F8000000 に張り、アプリの文脈から読めて、外した後に PTE が 0・PT が AS の破棄で返ることを見る (I1 の必須。I1 の合格の前に通す)。
- 実行時の RED: 橋 (C14) を外した版で v1 の present と v2 の sw の書きが混ざった時に画面の CRC が変わること (I2)。

### 13-3. 実機 (Ra266、GeForce 6200・Trident)
- GF: op ごとに hw と sw の READBACK を比べる (exact は一致、他は最大差 ≤ §3-3)、混在の束 (hw・sw・hw) (I2)、走査中の面の free の後に表示が乱れない (カメラ、I3)、fence の待ちの時間 (I2)。
- Trident: sync.cfg の gap_ns=80 で欠けが 0 (11-08 の掃引と同じ台本)、sync.cfg が無い時は既定で動く (I3)。

## 14. 未決
- 無し。PM 決定: #1 遅延確保の上限 8MB (`GFX_GART_KB`)、不足は NOSPC。#2 `TRI_WRITE_GAP` は /etc/gfx/sync.cfg へ (引き継ぎと優先順は配備の手順、既定 80ns)。#3 はユーザー決定「AとC」で確定 (§9-1)。
- 将来 #3 を同時出力へ広げる時に直す所 (今は直さない、記録だけ): §9-1 の ACTIVE の 1 個の決まり / C1 の `gfx2_adapter_register` の ACTIVE の 1 個の検査と `gfx2_sync_publish` の kind ごと 1 つの表 / head 系の口の NOSYS の分岐 (C11〜C13) (C は別票 CONSOLE_DUAL)。
=====END


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
- **PPC の選択集合 (C14・M1・M2)**: `gfx/gfx_hw_submit.c`・`mm/paging.c`・`mm/pgalloc.c` (と `gfx/gfx_vram.c`) は `tools/ppc/selected_success.json` の PPC の初回成功集合に入っている。C14・M1・M2 は PPC のクロスコンパイルが通ることを完了条件にする (x86 の asm・pc98 のヘッダを増やさない、増やすなら `tools/ppc/known_header_dependencies.json` に組を足す)。
- **公開ヘッダの型の契約 (A0)**: 第 4 段 B の `build/abi_contract.mk:4-13` が公開ヘッダ単独の gnu89 の probe と全 C の規則の char/enum の旗の検査を持ち、第 4 段 A の `include/type_contract.h` が ILP32・enum 4B・u64 の member 整列 (x86 4・ppc 8) を固定した。A0 で `sdk/include/os32/os32_gfx2.h` を abi_contract.mk の gnu89 の probe に足す。GFX2_Caps (256B)・GFX2_Op (80B) は u64 を使わず u32 だけで組み、ppc の整列 8 と offset を両立させる。KAPI 83→84 は USB の `usb_stat_get` と先に入った方が 84 (後の方が上げる)。
