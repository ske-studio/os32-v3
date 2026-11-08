> 出所: `~/os32-tmp/run/gfx-accel-hal/DESIGN.final.md` (Claude 起草、codex astra のレビュー 3 回で確定、2026-11-08)。段の状態は票 TASK_GFX_ACCEL_HAL が持つ。
> 状態: **現行 (2026-11-08)** — HAL に織り込む描画の高速化 (DIRECT/SHADOW) の確定設計。

# GFX の描画を PCI を渡らない形にする — HAL に織り込む詳細設計 (確定版)

> r3 に、第 3 回レビュー (REVIEW.r3) の条件 4 件を反映した (末尾の「確定」の節)。

> 前提: シングルタスク・協調切替、9801 互換は見ない、整数演算だけ、[C1]〜[D3]・[HW1]、KAPI は [ABI] (末尾追加・版上げ)。
> ユーザー決定 (2026-11-08、原文): 「すべて要ります。重要度別に分けて機能実装。あ、でもHALに織り込んだほうが良いのかも？」
> ユーザー決定 (2026-11-08、原文): 「HALが実装された場合はネイティブやPEGCなど機能的にバッファリングした方が良いものが利用する方にします。つまり、十分に描画が早い場合はHALからのグラフィックへの書き込みのみに出来るようにする方を正にします。」
> ユーザー決定 (2026-11-08、原文): 「cirrusは物理デバイスがないのでそのままでいいです。」→ Cirrus は今の経路のまま。
> ユーザー決定 (2026-11-08、原文): 「GeforceはHALからVRAM」→ GeForce の既定は DIRECT。計測は効果を記録するためだけに行い、切り替えの条件にはしない。

## r1→r2 の変更 (19 件、要約)
台帳は RAM の CLIENT を残して DISPLAY に書く。写像の入口を断る。DIRECT 用の attach。世代。XOR の ROP。同期の GET。dirty と flush。検査の式。部分失敗。透過は CPU に残す。移動の複写元。計測の区間。見積もり。非 INDEX8 は GUI 不可。スクロールのプロトコル。モジュールの検査。試験と変異。実寸。Trident の 80ns。中身は r2 と同じなので、本文に取り込んである。

## r2→r3 の変更 (REVIEW.r2 の指摘ごと)
1. P1 回復: 方式も世代も `libos32gfx_check` の 1 か所で見る。CPL=0 と CPL=3 で同じ扱い。NOSYS・STALE・gfx_lost のどれでも、束を捨てて attach し直し、描き直しの旗を立てる。SHADOW 側では `gfx_direct=0` に戻す → §3-3a
2. P1 flush の入口: C の `gfx_present`・`getchar`・`trygetchar`・`yield`・`halt` も `libos32gfx_check` を通るので、そこで flush する。64 本に満たない束を試験に入れた → §3-3a、§8
3. P1 データの持ち主: PUT・MONO のデータは積む時点で束の区画 (4KiB) に写す。大きいものは行で分けて写す。呼び出し側の VA は持たない → §3-3b
4. P1 移動の順序: 旧の vis を控える → 位置を変える → `recompute_and_expose` (ほかの窓の露出) → COPY → 合成 → 移動した窓の Paint。Paint の重なりはそのまま許す → §3-5
5. P1 スクロールの範囲: `cp = translate(valid∩rect, d) ∩ rect ∩ vis`、`Paint = (rect∩vis) − cp`。rect の外が変わらないことを試験する → §3-6
6. P1 SCROLL の前の flush: flush が成功したときだけ SCROLL を送る。失敗したら送らず、rect を INVALIDATE する → §3-6
7. P1 常設の写像の外: その面では DIRECT にしない (`direct_ok` に条件を足す)。page_flip で写像の外へ出たら SHADOW に、戻ったら DIRECT に戻す。往復を試験する → §3-1、§5
8. P2 query: 断るのは lease・bundle・bridge の側だけ。共通の認可は変えない。DIRECT 中も query が通ることを受入に入れた → §3-4
9. P2 done: CPL=3 なら、何かを実行する前に done の 4B が書けるか確かめる。確かめた後は 0 から数える → §3-2
10. P2 見積もり: submit の数・命令の数・backend の呼び出しの数・GPU の同期の数・PCI のバイトを分けて、D1・D2・D4 ごとに出した → §4
11. P2 試験: 先に取った lease の旧 VA と token が失効すること、CPL=3 の attach、Trident の SHADOW の回帰、残った指摘ごとの変異を足した → §8
12. ユーザー決定: §9-1 を閉じた。GeForce の既定は DIRECT。P0 は効果を記録するためだけに回す → §4、§9

## 1. 実物の確認 (r2 と同じ。r3 で足した行だけ)
- r2 の §1-1〜§1-4 はそのまま有効。食い違いは 4 件 (GeForce の fill/blit の書き先、M2MF では CPU が空かない、gfx_hal.h:73、Trident の間隔: main は待ち無し・wt/tri-gap は 80ns)。
- userland/lib/gfx/libos32gfx_core.c:174-181 `libos32gfx_check`: CPL=0 で ready なら何も見ずに 0 を返す。:204-208 `gfx_present` は dirty を積むだけ。:210-234 の getchar・trygetchar・yield・halt は呼んだ後に `libos32gfx_check` を通る。
- gfx/gfx_core_surface.inc:323-353 `gfx_reinit_surface_roles`: (0) で `lease_revoke_surface` (写像を外す)、(1) で `ledger_surface_regen` (世代を上げる)、最後に `gfx_bind_client`。
- exec/surface_query.c:205-212: `surface_query` も `surface_query_authorize` (:95-106) を通る。lease は :251、bundle は :259。
- userland/gshell/src/visible.rs:186-: `recompute_and_expose` が全部の窓の vis を計算し直し、露出した所を dirty に足す。vis と dirty はクライアントのローカル座標 (wm.rs:11)。

## 2. 方式 (決定済み)
| backend | 正 | 書く経路 |
|---|---|---|
| GeForce (INDEX8・640×480・常設の写像の中) | **VRAM (DIRECT、既定)** | HAL の口 → DISPLAY |
| GeForce (常設の写像の外・全画面の間) | RAM (SHADOW) | 今の CLIENT と present (backend_geforce.c:560-591) |
| Trident・Cirrus・PEGC・PC-98 | RAM (SHADOW) | 今のまま |
- 契約 G4: DIRECT の間、CPL=3 は CLIENT も DISPLAY も写像しない。RAM の CLIENT は登録したまま残し、SHADOW に戻るときと全画面アプリが使う。DIRECT の間は中身が古い。
- 読むのは GET だけ。XOR は ROP、透過は key 付きの PUT か透過の MONO で行う。

## 3. 契約の変更

### 3-1. GfxBackend (末尾に足す。hwdev_layout.json から作り直す)
```c
#define GFX_POLICY_SHADOW 0
#define GFX_POLICY_DIRECT 1
#define GFX_ROP_COPY 0
#define GFX_ROP_XOR  1                    /* dst ^= color (GXxor) */
    u8   policy;                          /* gfx_policy_reeval だけが書く */
    int  (*direct_ok)(void);
    int  (*fill_rop)(int x, int y, int w, int h, u8 color, u8 rop);
    int  (*put)(int x, int y, int w, int h, const u8 *src, u32 pitch, int key); /* 終わりに WC を吐き出す */
    int  (*mono)(int x, int y, int w, int h, const u8 *bits, u32 pitch, u8 fg, int bg,
                 int cx, int cy, int cw, int ch);  /* 不透明 (bg>=0) だけ。透過は呼ばない */
    int  (*get)(int x, int y, int w, int h, u8 *dst, u32 pitch);
/* drivers/video/geforce/gf_direct.c */
int gf_direct_ok(void)
{
    return selected_front.fmt == GF3_INDEX8 && selected_front.width == GF_WIDTH &&
           selected_front.height == GF_HEIGHT &&
           gf_front_in_cpu_map(&selected_front);  /* off+bytes が常設の写像 (起動時の framebuffer) の中 */
}
```
- DIRECT に必須の口は fill_rect・blit・fill_rop・put・get・direct_ok。**能力の検査は、backend が登録時に保存した DIRECT 用の hook の組 (`direct_ops`) に対して行う**。今の表の fill_rect・blit は見ない (SHADOW の間は NULL のため)。一つでも欠けると `gfx_policy_reeval` は SHADOW にし、kprintf を出す。表の hook は、切り替えが確定したときに gfx_policy.c だけが方式に合わせて書き換える (§3-4)。
```c
/* include/gfx_hal.h、末尾追加。backend は prepare/init で 1 回だけ埋め、あとは書き換えない */
typedef struct GfxDirectOps {
    int (*fill_rect)(int, int, int, int, u8);
    int (*blit)(int, int, int, int, int, int);
    int (*fill_rop)(int, int, int, int, u8, u8);
    int (*put)(int, int, int, int, const u8 *, u32, int);
    int (*mono)(int, int, int, int, const u8 *, u32, u8, int, int, int, int, int); /* 任意 */
    int (*get)(int, int, int, int, u8 *, u32);
} GfxDirectOps;
    const GfxDirectOps *direct_ops;                   /* GfxBackend の末尾。NULL = DIRECT にならない */
    int (*shadow_fill_rect)(int, int, int, int, u8);  /* SHADOW のときの表の値 (CLIENT が RAM なら NULL) */
    int (*shadow_blit)(int, int, int, int, int, int);
```
- GeForce は backend_geforce.c:260-262 で表を直接書くのをやめる。代わりに `direct_ops` を埋め、`shadow_*` は NULL にする。走査面の確定は `gfx_policy_reeval` に任せる。
- fill_rop・mono・put・get の色・鍵・画素は **INDEX8 専用** (§3-7)。
- `mono` は任意。透過、または `mono==NULL` のときは、カーネルの gfx_hw_mono_cpu.c が展開する。1 は fg、0 は `fg^1` にして、行を束ねた小片 (4KiB) ごとに `put(key = bg<0 ? fg^1 : -1)` を呼ぶ。不透明なら 0 を bg にする。
- `fill_rect`・`blit` の書き先: DIRECT なら DISPLAY。SHADOW で CLIENT が VRAM の backend なら CLIENT。SHADOW で CLIENT が RAM なら NULL (GeForce の SHADOW も NULL)。

### 3-2. KAPI (os32-kapi-add、version 82→83)
```c
#define GFX_CAP_DIRECT 0x0010   /* GFX_ScreenInfo.reserved[0] = 方式の世代 (u16、方式の切り替えと走査面の変化のたびに上げる、0 は使わない) */
#define GFX_CMD_FILL 1
#define GFX_CMD_COPY 2
#define GFX_CMD_PUT  3
#define GFX_CMD_MONO 4
#define GFX_CMD_MAX  64
typedef struct { u16 op, rop; i16 x, y, w, h, sx, sy; u32 color; i32 key; u32 data, pitch; } GFX_Cmd; /* 32B */
int gfx_hw_submit(const GFX_Cmd *cmds, u32 n, u32 gen, u32 *done);   /* kapi.json 末尾、done は出力 */
int gfx_hw_get(int x, int y, int w, int h, u8 *dst, u32 pitch, u32 gen);
```
```c
/* gfx/gfx_hw_submit.c の入口 (この順) */
int user = ring3_call_from_user();
if (user && !ring3_user_range_writable((u32)done, sizeof(u32))) return OS32_ERR_INVAL; /* 何も実行しない */
*done = 0;
if (g_backend->policy != GFX_POLICY_DIRECT) return OS32_ERR_NOSYS;
if (gen != gfx_policy_gen) return OS32_ERR_STALE;
if (n > GFX_CMD_MAX || (user && !ring3_user_range_readable((u32)cmds, n * sizeof(GFX_Cmd))))
    return OS32_ERR_INVAL;
for (u32 i = 0; i < n; i++) { int rc = submit_one(&cmds[i], user); if (rc) return rc; *done = i + 1; }
return 0;
```
- 命令ごとの検査は r2 の表のとおり。COPY は 2 つの矩形を一緒に削る。PUT は `w ≤ pitch` で範囲が `(h-1)*pitch+w`、MONO は `(w+7)/8 ≤ pitch`、GET は書けること。どれも u32 のあふれを見る。`ring3_user_range_readable` が無ければ、H2 で exec.c:454 の writable と対の口を足す。
- `*done` は全部終わった先頭の数。`cmds[*done]` は途中まで書いたかもしれないので、再送しない。

### 3-3. libos32gfx・libos32gui・WM の境目

#### 3-3a. 確かめる所を 1 つにする (libos32gfx_core.c `libos32gfx_check` を置き換える)
```c
int libos32gfx_check(void)
{
    GFX_ScreenInfo si;
    if (gfx_stopped) return OS32_ERR_INVAL;
    if (gfx_direct && gfx_flush() == 0) {}            /* 溜まった束を出す (失敗は下で拾う) */
    gfx_api->gfx_screen_info(&si);
    int want = !!(si.flags & GFX_CAP_DIRECT);
    if (gfx_ready && !gfx_lost && want == gfx_direct &&
        (!want || si.reserved[0] == gfx_gen)) {
        if (gfx_cpl() == 0 || !gfx_attach_port || want) return 0;
        return gfx_refresh();                          /* SHADOW の CPL=3 は今どおり lease の世代を見る */
    }
    gfx_cmd_drop(); libos32gfx_detach(); gfx_direct = 0; gfx_lost = 0;  /* SHADOW 側の旗も消す */
    int rc = libos32gfx_attach_checked();             /* DIRECT か SHADOW かはここで決め直す (r2 §3-3 の分岐) */
    gfx_regen = 1;                                     /* 呼んだ側が描き直す */
    return rc;
}
int libos32gfx_take_regen(void) { int r = gfx_regen; gfx_regen = 0; return r; }
```
- `gfx_flush()`: 区画を submit する。成功なら 0。NOSYS・STALE・そのほかの誤りでは、束を捨てて `gfx_lost=1` にし、負の値を返す (次の check で attach し直す)。
- check (= flush) を通る所: C の `gfx_present` (:204)・`libos32gfx_getchar`・`trygetchar` (キーがあったとき)・`yield`・`halt` (:210-234、**呼ぶ前**に flush、呼んだ後に check)。Rust の client.rs の COMMIT・WAIT・POLL の前。wm.rs:906 の前。filer_core.c:277・tilemap_compose.c:877 の前。libos32gfx の detach・終了。束が 64 本を超えるとき、または区画があふれるとき。
- 描き直しの順は**固定**: check → (成功したら) take_regen → 描き直し。flush だけでは回復しない (flush は gfx_lost を立てるだけで、attach し直すのは check)。
  - gshell: 各周の頭 (wm.rs の周の入口、:1180 付近の wm_cycle の先頭) と flush_present (:898) で `libos32gfx_check()` を呼ぶ。0 が返ってから `take_regen` を見て、立っていれば `composite_full` と `invalidate_all_clients` を呼ぶ。check が負なら、その周は描かずに次の周で check をやり直す。
  - libos32gui: client.rs の WAIT・POLL (:331 付近) から戻ったら、`libos32gfx_check()` → 0 なら `take_regen` → 立っていれば自分の窓を全面 INVALIDATE する。COMMIT の前は flush だけを行う。
```rust
// userland/rust/libos32gui/src/client.rs: WAIT/POLL の戻りの共通の後処理
fn after_return() {
    if unsafe { ffi::libos32gfx_check() } == 0 && unsafe { ffi::libos32gfx_take_regen() } != 0 {
        invalidate_all_own_windows();
    }
}
```
- 書く命令は今どおり `gfx_add_dirty_rect` も積む。GeForce の DIRECT の present_rect は commits を数えるだけ。

#### 3-3b. 束のデータ (userland/lib/gfx/draw/gfx_cmd.c)
```c
static GFX_Cmd q[GFX_CMD_MAX]; static u32 nq;
static u8 arena[4096]; static u32 used;                /* 積んだ時点で写す。呼び出し側の VA は持たない */
static void push_rows(u16 op, ..., const u8 *src, u32 pitch, u32 rowbytes, int h)
{
    while (h > 0) {
        u32 rows = (sizeof arena - used) / rowbytes;
        if (!rows || nq == GFX_CMD_MAX) { (void)gfx_flush(); continue; }  /* 空けてから */
        if (rows > (u32)h) rows = (u32)h;
        for (u32 r = 0; r < rows; r++) kmemcpy_u(arena + used + r * rowbytes, src + r * pitch, rowbytes);
        q[nq++] = (GFX_Cmd){op, ..., (i16)rows, ..., .data = used /* 区画の中の位置 */, .pitch = rowbytes};
        used += rows * rowbytes; src += rows * pitch; y += rows; h -= (int)rows;
    }
}
/* gfx_flush: data を (u32)(arena + data) に直して submit し、終わったら nq = used = 0 */
```
- rowbytes の最大は PUT で 640、MONO で 80 なので、4096 に必ず 1 行は入る。
- 一時の小片 (文字列の 1bpp、回転の RAM の小片) は push が返ったらすぐ使い回してよい。

### 3-4. 方式の切り替え (gfx/gfx_policy.c の `gfx_policy_reeval`、入口はこれだけ)
| いつ | 向き |
|---|---|
| GUI に入る・全画面から GUI に戻る・page_flip/set_mode で写像の中に戻る | 共通の条件 (下) を満たせば DIRECT |
| 全画面に入る (gfx_core.c:30 `appslot_gfx_claim`)・GUI を出る・`gfx_shutdown` (:466-)・写像の外への page_flip/set_mode (backend_geforce.c:239-263)・backend の誤り | SHADOW |
- 呼ぶ側は向きを決めない。どの契機でも `gfx_policy_reeval()` を呼び、向きは下の共通の条件だけで決める (走査面の確定が「全画面中は SHADOW」より優先されることは無い)。
```c
/* DIRECT へ移る共通の条件: GUI が画面を所有し、全画面の所有者がいない (surface_query.c:96-103 と同じ判定の材料) */
static int gfx_direct_wanted(void)
{
    const GfxDirectOps *d = g_backend ? g_backend->direct_ops : 0;
    return con_sink_is_enabled() && !gfx_fullscreen_owner() &&    /* appslot_gfx_owner() が GUI 以外を指していない */
           d && d->fill_rect && d->blit && d->fill_rop && d->put && d->get &&  /* 保存した DIRECT 用の hook を見る */
           g_backend->direct_ok && g_backend->direct_ok();
}
void gfx_policy_reeval(void)
{
    u8 want = gfx_direct_wanted() ? GFX_POLICY_DIRECT : GFX_POLICY_SHADOW;
    if (want == g_backend->policy) { if (gfx_scan_changed) gfx_policy_gen_bump(); return; }
    if (want == GFX_POLICY_DIRECT) {
        gfx_reinit_surface_roles(0, 0);   /* lease_revoke_surface: CLIENT・DISPLAY の旧 VA の PTE を外す */
        gfx_reinit_surface_roles(1, 0);   /* ledger_surface_regen: 旧 token を失効させる */
    }
    /* 切り替えが確定してから、表の hook を方式に合わせて書く (この順) */
    const GfxDirectOps *d = g_backend->direct_ops;
    g_backend->fill_rect = want ? d->fill_rect : g_backend->shadow_fill_rect;
    g_backend->blit      = want ? d->blit      : g_backend->shadow_blit;
    g_backend->fill_rop  = want ? d->fill_rop  : 0;
    g_backend->put       = want ? d->put       : 0;
    g_backend->mono      = want ? d->mono      : 0;
    g_backend->get       = want ? d->get       : 0;
    g_backend->policy = want;
    gfx_policy_gen_bump();                /* 0 を飛ばす */
}
/* 写像を断る所 (query と共通の認可には入れない) */
static int gfx_policy_blocks_map(u32 role)
{
    return g_backend && g_backend->policy == GFX_POLICY_DIRECT &&
           (role == LEDGER_ROLE_CLIENT || role == LEDGER_ROLE_DISPLAY);
}
/* exec/surface_query.c:251 surface_api_lease・:259 surface_api_bundle の先頭、
   gfx_core_bridge.inc:9 と CPL=3 からの gfx_get_framebuffer: */
if (gfx_policy_blocks_map(role)) return OS32_ERR_NOSYS;   /* bridge・get_framebuffer は 0 で埋める */
```
- 全画面アプリの DISPLAY の lease (surface_query.c:102) は SHADOW の中で今どおり。契約は縮めない。

### 3-5. 窓の移動 (userland/gshell/src/movecopy.rs、input.rs:896-926 と wm.rs:1497-1512 から呼ぶ)
```
old_outer = w.outer(); old_org = w.client_origin(); old_vis = w.vis      // ローカル座標
cursor::hide; drop_drag_frame                                            // 枠の消去で生じた dirty も下で引く
if gfx_flush() < 0 { 今の手順 (set_dirty_full) で終わる }
flush_present()
ok = old_vis − w.dirty − w.issued − topmost_local(old_org)               // 有効な旧の画素
w.x, w.y = 新しい位置
visible::recompute_and_expose(st)          // 全部の窓の vis を作り直し、ほかの窓の露出を dirty に
ok = ok ∩ w.vis;  if ok.capped { ok = ∅ }  // ローカル座標なので位置が変わっても対応する
for r in sort(ok, d=新−旧):                // dy>0 は下の行から、dy<0 は上から。dx も同じ
    gfx_copy_rect(dst = r + new_org, src = r + old_org)
composite_rect(old_outer); composite_rect(w.outer())   // デスクトップとクロームだけ。クライアントの中は描かない (wm.rs:921-)
add_dirty(w, w.vis − ok)                   // recompute が足した分と重なってもよい (add_dirty で結合される)
st.dirty_screen(old_outer); st.dirty_screen(w.outer()); configure_pending; cursor::show
```
- COPY を合成より先にするのは、旧の外形の合成が複写元を塗り潰すから。旧の位置で露出したほかの窓は、recompute が足した dirty から次の周で Paint される。

### 3-6. GUI_OP_SCROLL (= 82、`GuiReqScroll {u32 window; GuiRect16 rect; i16 dx, dy;}` 16B、ローカル座標)
- クライアント側 (libos32gui/src/scroll.rs):
```rust
pub fn scroll(win: u32, rect: Rect, dx: i16, dy: i16) -> Result<(), i32> {
    if unsafe { ffi::gfx_flush() } < 0 { invalidate(win, rect); return Err(OS32_ERR_STALE); } // 送らない
    call_req(GUI_OP_SCROLL, &GuiReqScroll { window: win, rect: rect.into(), dx, dy })
}
```
- WM (gshell/src/scroll_op.rs): その窓の issued を COMMIT と同じく出してから、次を行う。
```
rv    = rect ∩ w.vis
valid = rv − w.dirty − w.issued − topmost_local
cp    = translate(valid, d) ∩ rect ∩ w.vis          // rect の外へは書かない
if |dx| ≥ rect.w || |dy| ≥ rect.h || capped { cp = ∅ }
for r in sort(cp, d): gfx_copy_rect(r + org, r − d + org)
add_dirty(w, rv − cp)                               // Paint で返す
```
- 戻り値: 0、INVAL、STALE。呼び出し元は U3・U4 (widget.rs:498-501、edit_gui/src/lib.rs:256、filer/src/lib.rs:358・1401)。

### 3-7. 色の形式の境目 (今回は INDEX8 専用、将来の形式付きの口は別の版で)
- 今回足す口 (GfxBackend の fill_rop・put・mono・get、GfxDirectOps、KAPI の GFX_Cmd の FILL・PUT・MONO、gfx_hw_get) は **INDEX8 専用**。色は u8 の index。鍵と bg は -1〜255 で、負の値が「透過」を表す。`direct_ok` は INDEX8 でなければ偽なので、ほかの形式の面にこれらの口が届くことは無い。
- 高色深度は将来、**形式付きの別の口**として足す。形はこう決めておく: 画素の値は u32、透過は画素の値とは別の旗 (`GFX_CMD_F_TRANSPARENT`)、形式は書き先の面から決まる。名前は `GFX_CMD_FILL32` 等、または KAPI の別の入口とし、末尾追加・版上げで足す。今回の口の意味は変えない (u8 と負の値を広げて使い回さない)。
- 今回は高色深度を実装しない。

## 4. 重要度別の段と効果の見積もり
記号: t_k は KAPI 1 回、t_f は GPU の同期 1 回、t_d は WC の吐き出し 1 回 (BAR0 の read)。PCI の書きは 24µs/KB。P0 で測るのは**効果の記録のため**で、切り替えの条件ではない。
| 段 | 操作 | 今 (SHADOW) | DIRECT: submit / 命令 / backend の呼び出し / GPU の同期 / PCI |
|---|---|---|---|
| D1 | 背景の全面 | 300KB = 7.37ms | 1 / 1 / fill 1 / 1 / 40B |
| D1 | 1 文字 (32×32 に丸め) | 1KB = 24µs | 1 / 1 / put 1 (CPU の展開) / 0 + t_d / 約 0.13KB |
| D2 | 320×240 の窓の移動 | 約 170KB = 4.1ms + アプリの全面の描き直し | WM 2〜3 + アプリ 1 / 15〜35 / blit 1〜4・fill と put 10〜30 / blit と fill の数 / 2〜6KB |
| D2 (D1 の文字) | 600×400 を 16 行スクロール | 240KB = 5.9ms | 2 (アプリ 1 + WM の SCROLL) / 3 / blit 1・fill 1・put 3 (16 行 × 600B を 4KiB の小片で) / 2 + 3 t_d / 約 9.7KB = 0.23ms |
| D4 | 同じスクロール | — | 2 / 3 / blit 1・fill 1・mono 1 / 3 / 約 1.3KB |
| D5 | 64KB の不透明の画像 | 1.5ms | 16 (区画ごと) / 16 / M2MF 16 / 16 / 64KB (CPU は空かない) |
- D3 (変わった所だけ、SHADOW の側) は Trident・PEGC・GeForce の SHADOW に効く。対象は P0 で「塗った面積に対する送った面積の比」が 1.5 を超えた操作。
- P0 は r2 のとおり (区間は入力 → GPU の完了、warmup 10・本番 50、Trident は 80ns の版)。D1 の後に回し直して t_f が大きければ (命令数 × t_f が SHADOW の present を超える操作があれば)、「束の中では待たない」口を層 3 の別票にする。これは性能の改善の票で、既定を DIRECT にすることは変えない。

## 5. backend ごとの対応
| backend | 方式 | 中身 |
|---|---|---|
| GeForce | **DIRECT が既定**。`direct_ok` が偽 (写像の外・非 INDEX8・寸法違い) または全画面の間は SHADOW。非 INDEX8 と寸法違いは今どおり GUI 不可 (backend_geforce.c:553-556) | fill・blit・fill_rop は gf3 (GXcopy/GXxor)、put は CPU の dword + WC で key は画素を飛ばし最後に t_d、get は UC の読み (写像の中だけなので常に可)、mono は D4 で不透明だけ gf3_mono、hw_ops を数える |
| Trident | SHADOW (ram_fallback・GE 無効・80ns) | 変えない (方式は常に SHADOW、lease も今どおり) |
| Cirrus・PEGC・PC-98 | SHADOW | 変えない |

## 6. 部品の大きさ (r2 から変わらず)
vmkernel +3KB (FD に入る。FD_MIN_FREE_KB=64 を check-packages-host で確かめる)。geforce.mod +5KB (FD の外)。libos32gfx +3KB・libos32gui.shlib +2KB・gshell +5KB (CD)。kdebug.bin・trident.mod・cirrus.mod は 0。

## 7. ファイルごとの変更一覧 (1 行 = 1 段 = 1 実装者、POLICY_DEV §2。全 31 段)
| 段 | ファイル | 中身 | 依存 | 行 |
|---|---|---|---|---|
| P0a | userland/gshell/src/typical_ops.rs (新、feature gf_accel_accept) | 6 操作の台本、区間は入力 → GPU の完了 (§4) | — | 180 |
| P0b | drivers/video/geforce/gf_accept_prim.c (新) | t_f・t_k・t_d、全面の fill/copy、mono、put を CPU と M2MF で | — | 150 |
| H1 | include/gfx_hal.h、sdk/kapi.json と生成物、os32_kapi_shared.h、tools/hwdev_layout.json、tools/module_schema.py の版 | §3-1・§3-2・§3-7 の宣言だけ (os32-kapi-add) | — | 90 |
| H2 | gfx/gfx_hw_submit.c (新) | §3-2 の入口の順と命令ごとの検査、gfx_hw_get | H1 | 220 |
| H2a | exec/exec.c | 要れば `ring3_user_range_readable` (:454 の writable と対) | — | 20 |
| H3 | gfx/gfx_hw_mono_cpu.c (新) | 透過も不透明も小片にまとめて put | H1 | 80 |
| H4 | gfx/gfx_policy.c (新) | §3-4 の `gfx_direct_wanted`・`gfx_policy_reeval`・hook の差し替え・世代 | H1 | 140 |
| H5 | exec/surface_query.c:251・259、gfx_core_bridge.inc:9、gfx_core_framebuffer.inc:49 | `gfx_policy_blocks_map` (共通の認可には入れない) | H4 | 30 |
| H6 | gfx/gfx_core.c | 全画面に入る・出る、GUI に入る・出る、shutdown から `gfx_policy_reeval` を呼ぶ | H4 | 20 |
| G1 | drivers/video/geforce/gf_direct.c (新) | `gf_direct_ok` (写像の中という条件を含む)・put・get・`direct_ops` | H1 | 200 |
| G2 | drivers/video/geforce/gf_hook.c | 書き先を DISPLAY に、fill_rop (GXcopy/GXxor)、hw_ops を数える | H1 | 50 |
| G3 | backend_geforce.c (:239-263・:260-262・:640・present)、geforce_module.c:210-214 | 走査面が確定したら `gfx_policy_reeval()` を呼ぶ (向きは決めない)。表への直書きを外して `direct_ops` を登録、能力ビット、present は数えるだけ、新しい口の `module_text_contains` | G1・G2・H4 | 60 |
| L0 | userland/lib/gfx/libos32gfx_core.c | §3-3a の check・take_regen、DIRECT の attach・detach、gfx_present・getchar・trygetchar・yield・halt の前に flush | H1 | 70 |
| L1 | userland/lib/gfx/draw/gfx_cmd.c (新) | §3-3b の束・区画への写し・行での分け・gfx_flush | H1 | 170 |
| L2 | userland/lib/gfx/draw/gfx_draw.c | 点・線・矩形・get_pixel の振り分け | L1 | 60 |
| L3 | userland/lib/gfx/geom/gfx_fill.c・gfx_circle.c・gfx_bezier.c | 水平の区間 → FILL | L1 | 50 |
| L4 | userland/lib/gfx/text/gfx_font.c・gfx_kcg.c | 文字列を 1bpp の小片に並べて MONO 1 本 | L1 | 70 |
| L5 | userland/lib/gfx/draw/gfx_blt.c・gfx_sprite.c・gfx_surface.c・gfx_rotate.c | 面 → PUT (色鍵) | L1 | 70 |
| L6 | userland/lib/gfx/draw/gfx_copy.c (新) | gfx_copy_rect (SHADOW / DIRECT) | L1 | 60 |
| L7 | userland/lib/filer/filer_core.c:277、userland/lib/tilemap/tilemap_compose.c:877 | 前に gfx_flush | L1 | 10 |
| U1 | userland/rust/libos32gui/src/draw.rs | fb_base の 12 か所を振り分け、XOR は fill_rop、失敗したら INVALIDATE | L1 | 90 |
| U2 | libos32gui/src/client.rs・scroll.rs (新) | COMMIT の前に flush、WAIT・POLL の戻りで check → take_regen → INVALIDATE、§3-6 のクライアント側 | L0・W3 | 80 |
| U3 | libos32gui/src/widget.rs:498-501 | リストのスクロールを U2 で | U2 | 30 |
| U4 | edit_gui/src/lib.rs:256、filer/src/lib.rs:358・1401 | U2 を使う | U2 | 40 |
| W1 | userland/gshell/src/movecopy.rs (新) | §3-5 の順 | L6 | 160 |
| W2 | gshell/src/input.rs:896-926、wm.rs:898・906・1180・1497-1512 | W1 を呼ぶ、flush_present の前と各周の頭で check → take_regen → 描き直し | W1・L0 | 50 |
| W3 | gshell/src/scroll_op.rs (新)・handler.rs の表・reqs.rs、os32_gui_shared.h・proto.rs | §3-6 の WM の式、GUI の proto の版を上げる | W1 | 140 |
| W4 | gshell/src/cursor.rs | DIRECT の下地は gfx_hw_get を 1 回 (160B) | L1 | 30 |
| S1 | gfx/gfx_vram.c・gshell/src/damage.rs | D3 (中身は P0 の結果で票に書く) | P0 | 未定 |
| G4m | drivers/video/geforce/gf_hook_mono.c (新) | D4: 不透明の mono を gf3_mono で | G3 | 60 |
| G5m | drivers/video/geforce/gf_put_m2mf.c (新) | D5: 不透明の大きい put・get を gf3_upload/download で。大きさは P0 から PM が決める | G3・P0 | 70 |
- 並列: P0a・P0b・H1・H2a は最初に同時に出せる。H1 の後に H2・H3・H4・G1・G2・L0・L1 を同時に。H4 の後に H5・H6。L1 の後に L2〜L7・U1・W4 を同時に。W1 の後に W2・W3。U2 は L0 と W3 の後で、U3・U4 はその後。G4m・G5m は G3 の後。

## 8. 受入 → 試験・変異 (r2 の表に足す。変異は製品の行を 1 つずつ変える)
| 指摘 | 試験 | 変異 |
|---|---|---|
| r2-1 回復 | host: DIRECT → SHADOW (submit は NOSYS) → DIRECT を CPL=0 と CPL=3 の両方で回し、毎回 attach し直し・gfx_direct の値・regen が 1 回立つことを見る。gfx_lost からも回復する | check の `want == gfx_direct` の比べを外す / detach の後の `gfx_direct = 0` を外す |
| r2-2 flush の入口 | host: FILL 3 本 (64 本未満) → `gfx_present` / `yield` / `getchar` / client の WAIT の後で、偽の VRAM に出ていること | gfx_present の前の flush を外す / yield の前の flush を外す |
| r2-3 データの持ち主 | host: PUT・MONO を積んだ後、flush の前に元の小片を書き換えても絵は元のまま。5KB の PUT が 2 本に分かれ、絵が一致する | push_rows の写しをやめて元を指す / 行の分けの `rows` から 1 を引く |
| r2-4 移動の順 | host (wm_composite_tests): 3 窓。旧の位置で露出した窓に dirty が立つ、COPY の後に合成している、移動した窓の Paint は `vis − ok` を含む | recompute の呼び出しを COPY の後へ動かす / composite を COPY の前にする |
| r2-5 スクロールの範囲 | host: 窓の中の部分 rect を右へ 8px 動かし、rect の外の画素が変わらない、Paint が `(rect∩vis) − cp` | 式の `∩ rect` を外す |
| r2-6 SCROLL の前の flush | host: FILL → scroll。flush の前に SCROLL が出ないこと。flush が失敗したら送らずに INVALIDATE | scroll.rs の flush の検査を外す |
| r2-7 写像の外 | host (gf_accel 偽物): page_flip で off≠0 → SHADOW・世代が上がる → off=0 に戻る → DIRECT、どちらも regen | `gf_front_in_cpu_map` を外す |
| r2-8 query | host: DIRECT 中も `surface_query` は 0、lease・bundle・bridge は NOSYS | blocks_map を authorize へ動かす (query が NOSYS になって落ちる) / lease の検査を外す |
| r2-9 done | host: done が読み取り専用・NULL なら INVAL で何も実行しない。検査に落ちたら done=0、3 本目で失敗したら 2 | done の検査を実行の後へ動かす |
| r2-11 失効 | host (gfx_reattach_host.c): SHADOW で CLIENT・DISPLAY・bridge を取得 → DIRECT → 旧 VA の PTE が無い・旧 token の lease_view が STALE・CPL=3 の attach が lease を取らずに ready | `gfx_reinit_surface_roles(0,0)` を外す |
| r2-11 Trident | host (backend_trident の偽物): 方式は常に SHADOW、lease は通る、fill/blit は ram_fallback で NULL。実機 Ra266: Trident を選んだ GUI の起動と操作 | Trident の query で GFX_CAP_DIRECT を立てる |
| r3-1 能力の検査と hook の順 | host: SHADOW (表の fill_rect・blit は NULL、`direct_ops` は揃っている) から、走査面を変えずに GUI に戻ったら DIRECT になり、そのとき初めて表の hook が `direct_ops` の値になる。DIRECT → SHADOW で表の hook が `shadow_*` (GeForce では NULL) に戻る。`direct_ops` に 1 つ欠けがあれば SHADOW のまま | `gfx_direct_wanted` で `direct_ops` ではなく表の `fill_rect` を見る / hook の差し替えを世代を上げた後や revoke の前へ動かす |
| r3-2 全画面の間 | host: 全画面アプリが CLIENT・DISPLAY を lease 中に、写像の中へ page_flip (走査面が確定) しても方式は SHADOW のまま、lease・PTE・token が生きていて、世代だけ上がる。全画面を出ると DIRECT になる | `gfx_direct_wanted` から `!gfx_fullscreen_owner()` を外す / G3 で `gfx_policy_reeval` ではなく向きを渡す |
| r3-3 回復の呼び順 | host (実際の経路): (a) gshell の周を偽の世代の変化の後で 1 回回すと、check → take_regen → composite_full・invalidate_all_clients がこの順に 1 回ずつ起き、flush だけでは終わらない。(b) libos32gui のアプリが POLL だけで回っていても、POLL の戻りで attach し直して自分の窓を INVALIDATE する。(c) check が負の周は描かない | wm.rs の周の頭の check を外す (flush だけ残す) / client.rs の after_return を WAIT だけにする / take_regen を check の前に置く |
| r3-4 INDEX8 の境目 | host: 非 INDEX8 の走査面では `direct_ok` が偽で、submit は NOSYS。KAPI の生成物に GFX_Cmd の key が i32・色が u32 のまま (下位 8 ビットだけ使う)、範囲外の値 (key 256・-2) は INVAL | submit の key の範囲検査を外す / `gf_direct_ok` の INDEX8 の条件を外す |
- 実機 Ra266 (ノート、シリアル + カメラ): GeForce の既定 DIRECT で GUI↔全画面↔CUI、page_flip の往復、6 操作 (効果を記録するため)。

## 9. 未決
- なし。GeForce の既定 DIRECT はユーザー決定で閉じた (「GeforceはHALからVRAM」)。M2MF を使う大きさは P0 の実測から PM が決める。Cirrus は今のまま。

## 将来への申し送り (今回の範囲外)
- 3D・浮動小数点の口について、ユーザー決定 (2026-11-08、原文): 「HALではCPUチェックを行いMMX Pentium以上から受け付けるのが現状良いと思います。それ以下は大きめのテーブルを用意して計算させるほうが実用的に思います。」— その口を設計するときに、CPU の検査 (MMX Pentium 以上) と、それより下の CPU に向けた表引きの経路を入れる。

## 反論
- なし (REVIEW.r2 の 11 件・REVIEW.r3 の 4 件はすべて採った)。

## 確定 (2026-11-08、codex astra 第 3 回、条件 4 件を反映)
1. P1 能力の検査: 保存した `direct_ops` に対して検査し、切り替えが確定してから表の hook を方式に合わせて書く (§3-1、§3-4)。走査面を変えない SHADOW→DIRECT を試験 r3-1 で見る。
2. P1 移る条件: DIRECT へ移る共通の条件に「GUI が画面を所有し、全画面の所有者がいない」を足した。呼ぶ側は向きを決めない (§3-4)。全画面中の写像の中への flip で SHADOW と lease が保たれることを試験 r3-2 で見る。
3. P1 回復の順: WM の各周と WAIT・POLL の戻りを「check → 成功なら take_regen → 描き直し」に固定した (§3-3a)。flush だけで回復を待たないことを、実際の経路の試験 r3-3 で見る。
4. P2 INDEX8: 今回の口は INDEX8 専用。高色深度は、u32 の画素値と独立した透過の旗を持つ形式付きの口を別の版で足す (§3-7)。今回は実装しない。

## 追記 1 (2026-11-08、P0a・P0b の欠けの決定と P0c の分離)


## P0a 典型操作の台本 (DESIGN.final §7 P0a を置き換える)
1. **操作は 6 本**。§4 の表は効果の例で、操作の一覧ではない。台本の名前と中身は次のとおり。`move` は最前面の窓を右へ 64px 動かして戻す (`wm::move_window`、wm.rs:1497)。`scroll` は焦点の edit_gui に PageDown。`menu` はスタートメニューを開くキー (kbdnav の割り当て) を押して開き、もう一度押して閉じる。`desktop` は `wm::composite_full` (wm.rs:1007)。`char` は焦点の edit_gui に `a` と BS を交互に入れる。`list` は焦点の filer に ↓。
2. **起動の入口は gserial の動詞** `ops <名前> <回数>` だけにする (gserial.rs:128-142 の parse に 1 行、:229-272 の dispatch に 1 行)。シェルの命令・起動引数・/api/key は使わない。実機 (ノートのシリアル) と NP21/W は同じ経路で動く。
3. **前提は呼ぶ側が整える**: edit_gui・filer の起動と焦点は、既存の `key`・`type` で台本の前に済ませておく。焦点の窓の持ち主が違えば `error "pre" 3`。gf_accept (wm.rs:1743) の計測中なら `error "busy" 4`。
4. **入力**: キーは `Cmd::Key` と同じ注入の経路 (gserial.rs:264-270) で入れる。move と desktop は WM の関数を直接呼ぶ。
5. **計測の状態の持ち主は `GuiState.ops`**。feature gf_accel_accept のときだけ、wm.rs の GuiState にフィールドを 1 つ足す。
```rust
// userland/gshell/src/typical_ops.rs (新)
pub struct Run { op: u8, warm: u8, left: u16, i: u16, t0: u64, deadline: u32,
                 base: GfxStats /* present_bytes, commits, hw_ops */, id: [u8; 8], active: bool }
pub fn start(st: &mut GuiState, id: &[u8], op: u8, n: u16) -> Result<(), u8>; // gserial から
pub fn on_cycle_end(st: &mut GuiState);   // wm.rs の周の関数 (:1180 付近) の最後、flush_present の後に 1 行
```
6. **始まりと終わり**: 始まりは注入の直前の `now()` と `gfx_stats` (wm.rs:1759 の now と同じ時計)。終わりは、注入の後の周で次の 4 つがそろい、その周の flush_present が済んだ時点。(a) 全部の窓の `dirty`・`issued` が空、(b) リングに配っていない物が無い、(c) `configure_pending` が無い、(d) その周で COMMIT が 1 回以上あった (desktop と move は WM の flush でよい)。1 回 2 秒で打ち切り、その回は `FAILED` として記録する。次の回は、終わった次の周で入れる。
7. **回数と出力**: warmup 10 回の後に本番 n 回 (既定 50)。1 回ごとに `GOP1 <名前> <i> <µs> <Δpresent_bytes> <Δcommits> <Δhw_ops>` を出す (`serial_puts`、GFW1 と同じ出し方、wm.rs:1836-1838)。全部終わったら `reply(id, "end 0")`。DIRECT の量 (PUT・MONO・GET のバイト) の列は G 段で数が揃ってから末尾に足す。今は出さない。
8. **触ってよいファイル**: typical_ops.rs (新)、gserial.rs (parse・dispatch の 2 行と `Cmd::Ops`)、wm.rs (GuiState のフィールド 1 つ・on_cycle_end の呼び出し 1 行)、lib.rs (`#[cfg(feature = "gf_accel_accept")] mod typical_ops;`)。ほかのファイルには触らない。host の試験は gshell/host/ に typical_ops_tests.rs を新しく足す (parse、終わりの 4 条件、打ち切り)。

## P0b HAL の素の時間 (DESIGN.final §7 P0b を置き換える)
1. **置き場**: `drivers/video/geforce/gf_accept_prim.c` をやめ、**kdebug/gf_prim_bench.c (新、hwdev.mod)** にする。既存の層 3 の受入 (kdebug/gf_accel_test.c) と同じく、module の境界は `geforce_accel_ops()` (gf3_test_ops) と `geforce_foundation_ops()` だけを通る。dispatch の検査は gf_accel_test.c:96-104 と同じ条件を自前で持つ (static は共有しない)。
2. **線 (wire)**: kdebug/gf_command_wire.h:11-14 の `enum gf2_case` の末尾に `GF6_PRIM` を足す。tools/hwdev_layout.json の enum の並びを作り直す。シェルは userland/shell/cmd_gf2.c:10-12 の `cases[]` の末尾に `"prim"` を足し、:33-35 の上限を `GF6_PRIM` にする。`gf2 prim <項目>` の項目の番号は session に入れる。
3. **振り分け** (kdebug/gf_command_test.c:272- に 1 つ足す):
```c
if (q.size == sizeof(q) && q.test == GF6_PRIM && !q.rc) {
    int rc = gf_prim_bench(q.session);   /* kdebug/gf_prim_bench.h: int gf_prim_bench(u32 item); */
    request->session = 0; request->rc = rc; return rc;
}
```
4. **ビルド**: build/hwdev.mk:5 の `HWDEV_GF` に `gf_prim_bench` を足す。tools/tests/test_kdebug_imports.py の許可の一覧に、新しく使う名前があれば足す。
5. **項目** (session、0 は全部): 1 `fence` (1×1 の fill = t_f)・2 `fill` (全面)・3 `copy` (全面、重なり無し)・4 `mono` (1KB の不透明)・5 `ifc` (64KB の upload、force_m2mf=false)・6 `m2mf` (64KB の upload、force_m2mf=true)・7 `bar0` (BAR0 0x000000 の 32 ビットの読み = t_d)。書き先は `o->test_surface` で、表示面には書かない。test_surface が 640×480 より小さいときは、面の寸法で測って寸法も出す。
6. **時計と回数**: `f->io.timing.clock_us` (gf_accel_test.c:123 と同じ)。1 本が短いので、100 本を 1 組として組の時間を測る。warmup 1 組の後に本番 50 組。
7. **CPU の WC の put は測らない**: test_surface の写像は WC の窓 (512KiB) の外なので、値が違ってしまう。DESIGN の 24µs/KB (TASK_GF_ACCEL 段 0、全面 7.37ms) をそのまま使う。
8. **出力**: `GFP1 <項目> <組> <組の µs> <1 本の ns> <rc>`。終わりに `GFP1 done rc=<rc>` (serial_puts_polled)。
9. **t_k は CPL=3 の別の段 P0c で測る** (kdebug はカーネルの中なので測れない)。userland/tests/kapi_cost.c (新、40 行): `gfx_screen_info(&si)` を 1000 回呼び、前後を `sys_time_now` で測って 1 回の ns を出す。副作用の無い今の KAPI を使い、新しい submit は待たない。DIRECT の submit の実際の t_k は H2 の後に P0a を回し直して得る。

## 追記 2 (2026-11-08、PEGC の実測と「書くだけの DIRECT」の方向)

- 実測 (Ra266、全画面 640x480x8 を 8 回): 書き込み rep movsd・dword 14.1ms、word 23.6ms、byte 46.9ms、間隔 0〜600ns でも欠け 0。読み出し rep movsd・dword 89.0ms、word 176.9ms、byte 352.7ms。
- ユーザー (原文):「測ってよいですが、RAMの状態を管理しない書き込み方法として用意するのは？」「ゲームなどの場合毎秒レンダリングし続けなければならない時があるからです」「２．は必要でしょうね」(vsync)。
- 方向: PEGC に書くだけの DIRECT を足す (blit は申告しない = 描き直し、XOR・半透明は避ける、カーソルの下の退避は読む、vsync を併用、アプリが方式を選ぶ問い合わせ)。詳細の追記は別に書く (票 TASK_GFX_ACCEL_HAL)。

## 追記 3 (宣言の段 A0)

2026-11-09。第 2 波の共有契約を以下に固定する。A0 は宣言・状態の置き場・リンクできる仮実装までで、方式切替・描画・束の実装は各所有者の段で置き換える。追記 2 の PEGC の「書くだけの DIRECT」は取り消し、PEGC は §2 の SHADOW のまま。

- `include/gfx_hal.h` と `gfx/gfx_core.c` の `g_backend` は `GfxBackend *` に揃える。backend 自体は元から可変であり、表のコピーや別の切替入口は作らない。policy と方式に応じた active hook を更新するのは H4 の `gfx_policy_reeval` だけ (§3-4)。既存の初期化・登録経路の振る舞いは A0 では変えない。
- `gfx/gfx_core_surface.inc` の既存 `void gfx_reinit_surface_roles(int finish, int display_only)` を非 static にし、`gfx/gfx_internal.h` に宣言する。所有者は core (H6 の接続、H4 から呼ぶ)。通常文脈で `finish=0` の revoke → `finish=1` の regen の順。`display_only=0` は CLIENT/DISPLAY、1 は DISPLAY だけ。既存の実体を使い、revoke/regen の処理は変更しない。
- H4 の置き場は `gfx/gfx_policy.h` / `.c`。`extern u16 gfx_policy_gen` (初期値 1、世代は u16 で 0 を飛ばす)、`int gfx_direct_wanted(void)`、`void gfx_policy_reeval(void)`、`int gfx_policy_blocks_map(u32 role)`。wanted は真偽、blocks_map は LEDGER_ROLE_* に対する真偽で query の認可には使わない。A0 では wanted/blocks_map は 0、reeval は何もしない。
- H4 の同じヘッダに `int gfx_fullscreen_owner(void)` (GUI 以外の全画面所有者があれば非0、H4 が appslot の所有者から判断) と `extern int gfx_scan_changed` (H4 が実体を持つ、初期値0) も置く。H6/G3 は確定した走査面変更後に旗を 1 にして `gfx_policy_reeval()` を呼ぶ。H4 は同方式でも旗が立てば世代を上げ、処理後に旗を消す。向きを指定する別入口は作らない。A0 の fullscreen_owner は 0、通知・旗の消費は H6/G3・H4 の実装に残す。
- H3 は `gfx/gfx_hw_mono_cpu.h` / `.c` の `int gfx_hw_mono_cpu(int x, int y, int w, int h, const u8 *bits, u32 pitch, u8 fg, int bg, int cx, int cy, int cw, int ch)` を実装し、H2 が検査後に呼ぶ。bits は MSB-first、pitch はバイト、bg=-1 は透過、他は INDEX8。clip は画面座標。選択 backend の put を同期的に使い、bits を返却後に保持しない。0 成功、負値失敗 (部分描画はありうる)。A0 は NOSYS。policy と mono の `.c` はカーネルの sources/common.mk・order.mk に登録済みで、H2 は H3/H4 の実装前でもリンクできる。

libos32gfx の内部共有ヘッダは `userland/lib/gfx/gfx_direct_internal.h`。L0 は `libos32gfx_core.c` の `int gfx_direct`、`u16 gfx_gen`、`int gfx_lost` の実体と attach/recovery を所有する (A0 は全て 0)。gfx_gen は screen_info.reserved[0] の非0世代で、KAPI の u32 gen に渡す際に拡張する。L1 は送信/GET の失敗時に gfx_lost を立て、回復と旗の消去は L0 に任せる。再描画旗は L0 の非公開状態とし、`int libos32gfx_take_regen(void)` で一度だけ取得・消去する (A0 は 0)。check → 成功 → take_regen → 再描画の順は §3-3a のまま。

次の関数は全て `int` で、0 成功・負値失敗。L1 の置き場は `userland/lib/gfx/draw/gfx_cmd.c`、宣言は内部共有ヘッダ (flush は公開 `libos32gfx.h` からも見える)。L2〜L6 はこの口を使う。

```c
int gfx_cmd_enqueue_fill(int x, int y, int w, int h, u8 color, u8 rop);
int gfx_cmd_enqueue_copy(int dx, int dy, int sx, int sy, int w, int h);
int gfx_cmd_enqueue_put(int x, int y, int w, int h, const u8 *src, u32 pitch, int key);
int gfx_cmd_enqueue_mono(int x, int y, int w, int h, const u8 *bits, u32 pitch,
                         u8 fg, int bg, int cx, int cy, int cw, int ch);
int gfx_cmd_get(int x, int y, int w, int h, u8 *dst, u32 pitch);
int gfx_flush(void);
void gfx_cmd_drop(void);
```

- FILL の rop は COPY=0/XOR=1。COPY の座標は書き先→読み元。PUT の key=-1 は鍵なし、MONO の bg=-1 は透過、それ以外は INDEX8。MONO のビット順・clip は H3 の口と同じ。clip は L1 で適用し、必要なビット列を詰め直してから MONO 命令にする。MONO の fg は color、bg は key に入れる。画面内の寸法で PUT は最大640B/行、MONO は最大80B/行とする。
- PUT/MONO は enqueue 中に 4KiB の区画へコピーし、呼出側のポインタを保持しない。行の分割と自動 flush は L1 が所有し、順序を保つ。検査に失敗する値や GFX_Cmd の i16 に表せない値を暗黙に切り詰めない。失敗前に先頭の命令が送信された可能性があるため、呼出側で再送しない。
- GET は同期で、flush 成功後だけ KAPI の gfx_hw_get を呼び、dst を返却後に保持しない。drop は区画と命令を捨てるだけで送信しない。flush の失敗時は捨てて gfx_lost を立てる。A0 は空の flush が0、drop が何もしない、enqueue/get は NOSYS。
- L6 の `int gfx_copy_rect(int dx, int dy, int sx, int sy, int w, int h)` は `draw/gfx_copy.c`、宣言は `libos32gfx.h`。同じ画面内の COPY を SHADOW/DIRECT に振り分ける口で、0 成功・負値失敗。A0 は NOSYS。`gfx_flush`・`libos32gfx_take_regen` と併せて後続の C/Rust/WM が呼ぶ入口で、共有ライブラリや Rust への結線は各後続段で行う。既存の check/attach/detach の宣言と動作は維持する。
- G2 の `int geforce_fill_rop(int x, int y, int w, int h, u8 color, u8 rop)` は既存の fill/blit と同じ共有ヘッダ `hwdev/gf/accel.h`。G1 の `int gf_direct_ok(void)` と `extern const GfxDirectOps gf_direct_ops` は `drivers/video/geforce/geforce_module.h`。G1/G2 の実体をここに複製しない。`gf_direct.o` の build/geforce-module.mk への追加・登録・module text の検査は G3 の所有のまま。
