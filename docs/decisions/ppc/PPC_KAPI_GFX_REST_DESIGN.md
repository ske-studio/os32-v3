> 出所: ~/os32-tmp/run/kfill-5c/DESIGN.final.md (設計ラリー 3 回、起草 Fable・レビュー sol、2026-11-20 確定)
> 状態: **現行 (2026-11-20)** — 段 5c の確定設計。末尾の「確定」の節は本文に優先する

# PPC_KAPI_FILL 段 5c の確定設計 (gfx の残り 12 本と gfx2 の 4 本)

> 状態: **確定 (2026-11-20)** — Fable 起草、sol レビュー 2 回、PM 決定を反映 (末尾)。出所は本線の機械の ~/os32-tmp/run/kfill-5c/。公開 8 本・不在 8 本。

対象: PPC_KAPI_FILL_DESIGN.md §3 の段 5c (詳細設計 r2 に第 2 回の直しを入れた物)。**基点は 5b の実装 (wt/ppc-gfx-5b-c2 3ad37cd56) の取り込みの後の木**。r1 の行番号は origin/main 9d99c1b64 で採り、r2 で足した所だけ 3ad37cd56 で読み直した。ビルド・試験は回していない ([V4]、§6)。

## r1→r2 の変更 (REVIEW.r1 の指摘ごと)

| 指摘 | 直し |
|---|---|
| P1 門が早期 return より後 | §2-3: 門を共通の公開の入口の先頭 (context の直後、alias 探し・同期の ctx・dirty の交差より前) へ。x86 は定数 1 で消える。公開の gfx_init / gfx_init_200 が前提と明記 |
| P1 通常の SYSMEM 面と端末の再描画 | §2-3: gfx2_mode_set・gfx2_flip も同じ門を公開の入口に置く (gfx2_surf_get はアダプタ世代を見ない、gfx/v2/surface.c の gfx2_surf_get)。surface_copy と口には門を置かない (端末の gfx/gfx_console.c の gfx_console_open・gfx_console_present は be を直に呼ぶので止まらない)。A9 を分けて端末の再描画を公開の前に確かめる |
| P2 部分転送の後の dirty | §2-3: PowerMac の道だけ、転送した範囲が dirty の全体を覆う時に消す。x86 の道は今の字面のまま。A2b・A7 に試験 |
| P2 一致の条件 | §2-4: op ごとに有効なコマンドを 1 個以上用意し、SW は効果 (描画・読み出し・状態) で、NOSUPPORT は NOSYS で確かめる |
| P2 公開とゲストの循環 | §4: 受入の台本が公開表の写しで対象のスロットを仮に結線してゲストと変異を通し、その後に本番の表を直す。gfx2prob.c を一覧に |
| P2 変異の挿入点・固定モード | §4-1・A8: 'gfx2' の組は空の集合で残す (ppc_gfx2.py の 'absent' の当て先)。固定モードの比較は OF の host (test_of_fb.py) で直に。A11 に通常ラッパーと実ターゲットを明記 (確定で 8 本になり、'gfx2' の組は gfx2_check_op が残る形に変わった) |
| P2 検査の一覧 | §4-1 を 3ad37cd56 の木で作り直し (I3 host の covers、5b の既存の変異、ppc-kapi-binding・ppc-kapi-table) |
| P3 計数の規則 | §2-5: 端末・mode_set・flip は present_bytes だけ、保護行だけの present は bytes +0 / commits +1。A3 に試験 |

**PM 決定**: (1) パレットの 3 本は 5c では不在。ATI の DAC の装置は作らない。(2) check_op の規則の変更・gfx2_present の共通の所有者照合・256 色の読み出しの 3 案は採らない (x86 の答えを変えない)。(3) 部分転送の後の dirty は PowerMac だけで直し、x86 は今どおり。(4) **gfx2_check_op は 5c では公開せず不在のまま** (INDEX8 同士の ALPHA で check_op の答えと submit の結果が合わない、§2-4。規則は据え置き)。

## 1. 実物の確認

### 1-1. 依頼・計画の前提と実物の食い違い (6 件)

| # | 前提 | 実物 | 設計への効き |
|---|---|---|---|
| 1 | 5b が済んでいる | origin/main 9d99c1b64 には無いが、**基点の 3ad37cd56 にはある**: gfx/gfx_present.c、門 display_provider_present_ok (platform/powermac/display_provider.c の display_provider_present_ok、`present_owner == appslot_cur() && present_generation == a->gen`)、付与は公開の init だけ (gfx/gfx_core.c の gfx_kapi_init・gfx_kapi_init_200 の DISPLAY_V1_PUBLIC_INIT_OK)、失効は display_provider_raster_reset と shutdown の a->gen++。不在は gfx 12 本・gfx2 4 本 | 5c は 5b の取り込みの後に直列。門はこの実物をそのまま使う (名前も足さない) |
| 2 | 段名「exec/surface_query.c → PPC 公開表」 | surface_query.c は 16 本のどれの本体でもない。本体は gfx/gfx_core_api.inc (screen_info、set_palette_hal、hw_fill_rect、hw_blit、stats、lease_palette)、gfx/gfx_core_framebuffer.inc の gfx_get_framebuffer、gfx/gfx_core_bridge.inc の gfx_framebuffer_bridge、gfx/gfx_scroll.c の gfx_hardware_scroll、gfx/palette.c の palette_get、gfx/gfx_hw_submit.c の gfx_hw_submit・gfx_hw_get、kapi/kapi_osri.c の kapi_osri_show、gfx/v2/display.c の gfx2_flip・gfx2_present、gfx/v2/modeset.c の gfx2_mode_set、gfx/v2/check_op.c の gfx2_check_op | **surface_query.c は変えない**。効くのは surface_query.c の perm_max 検査 (perm_max が RO/RW でない面は INVAL): PowerMac の DISPLAY は perm_max = NONE (platform/powermac/display_provider.c の display_provider_boot_reserve) なので、USER に渡る面は CLIENT (影) だけ |
| 3 | gfx2_present の同期の先が影 | 元も先も同じ影。DISPLAY の alias は SHADOW の時 CLIENT の RAM を指し (gfx/v2/v1_bridge.c の v1_alias_desc)、同期の先も saved.shadow (platform/powermac/display_provider.c の init)。gfx/v2/display.c の gfx2_present は**影を影へ写すだけ**。しかも gfxsync_row_write (gfx/sync/sync_write.c の gfxsync_row_write・gfxsync_row_read) は unit=4 で LE の語に組むので、BE の PowerPC で unit=4 の設定が当たると影の語が逆順に壊れる (今は既定 unit=1、assets/gfx/sync.cfg に of_fb の節は無い) | PowerMac では同期層の書きを**飛ばして**後端の写しへ行く形にする (§2-3)。足すだけの形 (書き → 写し) は採らない |
| 4 | check_op: 共通の規則は SW | 条件つき。check() は gfx/v2/check_op.c の check。OF FB は be->check が NOSYS (drivers/video/of_fb/of_fb_v2.c の shim_check)、caps.vram_cpu_bytes = 0 (gfx/v2/caps.c の gfx2_caps_from_v1 は埋めない) なので、形の問い (メモリプールの判定) はプール SYSMEM・GART で SW、VRAM で NOSUPPORT。alias への実行は shadow() (check_op.c) で SW | 規則は変えない。一致の定義を「実在できる面の組」に限る (§2-4) |
| 5 | パレットは OF の set-colors か ATI の DAC | OF の呼び出しは起動の間だけ (platform/powermac/rh_observe.c の capture_colors「while firmware calls are still available」、使うのは color@・color! で黒と白の 2 個)。後端の set_palette は空 (drivers/video/of_fb/backend_of_fb.c の palette)、gfx2 は shim_palette が NOSYS (drivers/video/of_fb/of_fb_v2.c の shim_palette)、caps.palette_entries = 0 (gfx2_caps_from_v1)。**gfx2_palette は既に公開済みで NOSYS** | 実行時に CLUT を書ける道が今は無い。v1 の 3 本は型ごとの不在 (§2-2、PM 決定) |
| 6 | 「計画 §7: check_op … ユーザー判断」 | 計画に §7 は無い (§6 ユーザー判断は 0 件)。根拠は 5b 確定設計 §5 の末尾「check_op の公開の判定の規則の変更を採るならユーザーに戻す」 | §5 はその文に従う |

### 1-2. 食い違いでない確認

- PowerPC の像に既に入っている本体: gfx_core.c (各 inc)・gfx_scroll.c・palette.c・gfx/v2/*.c・exec/surface_query.c (build/sources/common.mk の C_COMMON_EXEC)。**入っていない** (C_COMMON_PC98_PENDING、コンパイルだけ): gfx_hw_submit.c・gfx_hw_mono_cpu.c・gfx_osri.c・gfx_osri_view.c・kapi/kapi_osri.c。
- 表示へ届く道は of_fb_copy (drivers/video/of_fb/backend_of_fb.c の of_fb_copy) だけ。呼ぶのは v1 の present (backend_of_fb.c) と shim の surface_copy (drivers/video/of_fb/of_fb_v2.c の surface_copy、mode_set の surface_copy 呼出し・flip の surface_copy 呼出し)。**端末も同じ shim を通る** (gfx/gfx_console.c の gfx_console_open の be->mode_set、gfx_console.c の be->flip。共通の gfx2_mode_set・gfx2_flip は通らない)。保護行 (protected_rows) と 4 px の語の丸めは of_fb_copy が持つ。
- gfx2_present は所有者を照合しない (gfx/v2/display.c の gfx2_present `context(id, head, 0, …)`)。mode_set (gfx/v2/modeset.c の gfx2_mode_set)・flip (gfx/v2/display.c の gfx2_flip) は所有者でなければ BUSY。
- gfx_get_framebuffer の USER の道は gfx_framebuffer_bridge (gfx/gfx_core_bridge.inc の gfx_framebuffer_bridge) で CLIENT の lease。gfx_policy_blocks_map (gfx/gfx_policy.c の gfx_policy_blocks_map) は DIRECT の時だけ断る。OF FB は SHADOW (policy = 0)。
- 計数: x86 は後端が数える (drivers/video/pc98/backend_pegc_present.inc の pegc_present_rect、drivers/video/geforce/gf_present.c の gf_present_rect)。OF FB は 1 か所も数えない (backend_of_fb.c に gfx_counters が無い)。
- kapi/kapi_osri.c に x86 の命令・番地の依存は無い (caller_access・kmalloc・copy_caller_bytes・ring3_fault_kill)。依存は先の gfx_osri_view.c: PLANAR4 640x400 の固定 (表示モードの検査)、後端の osri_show (OF FB は NULL)、pcm_state・serial_gate_active (表示可否の検査、PowerPC は pcm の組が不在)。

### 1-3. 統合の判断 3 点 (POLICY_DEV §1)

| 点 | 判断 |
|---|---|
| 同じ責務の実装が既にあるか | ある。影 → 表示は of_fb_copy、所有者と世代の門は 5b の display_provider_present_ok (マクロ DISPLAY_V1_PRESENT_OK をそのまま 3 入口で使う)、情報・面・計数の本体は共通の gfx_core。2 個目は書かない。足すのは「gfx2_present から後端の写しを呼ぶ口」1 本と OF FB の計数だけ |
| 口は既存の物で足りるか | GfxBackendV2 に present の欄は無く、GfxSyncTarget に欄を足すと x86 の 4 後端とモジュールの構造体の形が変わる。**機種ヘッダのマクロ 1 個** (5b の DISPLAY_V1_WIDTH・DISPLAY_V1_PRESENT_OK と同じ platform_display_fallback.h、include/display_provider.h の platform_display_fallback.h の取り込み 越し) にする。x86 は定数 |
| 既存の計画・確定設計と合うか | 計画 §2 の gfx「B は可視面・palette・raster・HW 能力。OF FB の実能力を検査」、gfx2「B は of_fb_v2 ops。各機能は caps・check_op 後に使用」、AC-KAPI-1 (型別の欠如は通常・欠如の両方を試験)。DISPLAY_HOOKS §3「shutdown の後の描画は端末の画素を変えない」は gfx2 の 3 本にも掛ける (A9)。置き場: 共通 = gfx/v2/display.c (既存の 1 機能)、装置 = drivers/video/of_fb、機種 = platform/powermac |

## 2. 16 本の分け方と (a)〜(c) の決め

### 2-1. 分類

| 関数 | 分類 | PowerMac の答え | 理由 |
|---|---|---|---|
| gfx_screen_info | PowerPC だけ (公開表) | 実寸・bpp 8・PACKED8・flags 0・lease 0 | 本体は像にあり、後端の query (drivers/video/of_fb/backend_of_fb.c の query) が答える |
| gfx_get_framebuffer | PowerPC だけ (公開表) | planes[0] = **影** (CLIENT の lease の VA)、planes[1..3] = 0、pitch = stride | 台帳が決めている (食い違い 2、契約 G4「USER に見せるのは CLIENT だけ」)。OF FB の直の番地は渡さない (語の整列・保護行・端末との取り合いを of_fb_copy が持つため)。表示は gfx_present_* で出す |
| gfx_stats | PowerPC だけ (後端の計数 + 公開表) | §2-5 | 本体は像にある。数える側が無い |
| gfx_hw_fill_rect・gfx_hw_blit | PowerPC だけ (公開表) | NOSYS (後端に fill_rect・blit が無い、gfx/gfx_core_api.inc の gfx_hw_fill_rect・gfx_hw_blit) | 本体が像にあり能力で答える。x86 の加速なしの後端と同じ規則。v1 の hw 系に SW の代わりは無い (呼ぶ側が CPU へ落ちる約束、gfx_core_api.inc の加速の契約) |
| gfx_hw_submit・gfx_hw_get | **E** | 欠如の答え (NOSYS) | OF FB は direct_ops が無く DIRECT にならない (gfx/gfx_hw_submit.c の gfx_hw_submit・gfx_hw_get で必ず NOSYS)。TU は像に無く、入れるには gfx_hw_mono_cpu.c も要る。x86 との差: 欠如の答えは *done に 0 を書かない (移植するアプリは戻り値で見る) |
| gfx_hardware_scroll | **E** | 何もしない | display_scroll は空 (platform/powermac/display.c の display_scroll)。結線すると vram_scroll_y だけ動く見かけの成功になる |
| gfx_set_palette・gfx_get_palette・gfx_lease_palette | **E** | 欠如の答え | 食い違い 5。結線すると set は黙って何もせず、get は PC-98 の 16 色の 4 ビット値 (gfx/palette.c の palette_get) を返し、lease は必ず INVAL。gfx2_palette の NOSYS・palette_entries = 0 と揃える |
| gfx_osri_show | **E** | 欠如の答え (NOSYS) | §1-2 の末尾。x86 の依存でなく、PLANAR4 固定と pcm・serial の依存 |
| gfx2_mode_set・gfx2_flip | **本線の共通の口が要る** (門 1 行ずつ) | 固定モード 1 個・INDEX8・SYSMEM の scanout を全面で写す。VBLANK 付きの flip は NOSYS。公開のセッションの外は BUSY | 本体と shim は既にある。面の取得がアダプタ世代を見ないので門が要る (§2-3) |
| gfx2_present | **本線の共通の口が要る** | §2-3 | 食い違い 3 |
| gfx2_check_op | **E (5c では不在、PM 決定)** | 欠如の答え (NOSYS) | §2-4: INDEX8 同士の ALPHA で答えと実行が合わない。規則は変えないので公開を保留 |

**公開 8 本 (gfx 5・gfx2 3)、不在 8 本 (gfx 7・gfx2 1)**。パレットの 3 本と gfx2_check_op の不在は **PM 決定**。PROVIDER_GROUPS['gfx'] のヘッダ除外 (gfx_hal.h・palette.h) は残す (使うのは E の物だけ)。

### 2-2. パレットの分担 (PM 決定: 5c では作らない。後で DAC の票が立った時の形)

- 本線の共通の口: 既存の GfxBackend.set_palette・query の lease_first/lease_count、GfxBackendV2.palette、gfx2_pal (palette_state.c) で足りる。足す物は無い。gfx_get_palette を 256 色で読ませる直しは x86 の PEGC の答えに触れるので別の判断。
- PowerPC の後端: backend_of_fb.c の palette() と of_fb_v2.c の shim_palette を、platform が of_fb_desc に入れる書き手 (DAC の装置) へつなぐ。OF は使えない。

### 2-3. (a) gfx2 の表示の 3 入口: 門の位置、影 → 表示の呼び手、dirty

**門は共通の公開の入口の先頭に置く (5b のマクロを使い回す)。影 → 表示は gfx2_present が機種ヘッダのマクロ越しに後端の写しを呼ぶ。口と surface_copy には門を置かない。**

```c
/* platform/pc98/platform_display_fallback.h     (DISPLAY_V1_PRESENT_OK() は 5b の定数 1 のまま) */
#define DISPLAY_V2_SHADOW_PUSH(x, y, w, h) OS32_ERR_NOSYS      /* x86: 影が正本の後端は無い */
/* platform/powermac/platform_display_fallback.h */
int display_provider_shadow_push(int x, int y, int w, int h);
#define DISPLAY_V2_SHADOW_PUSH(x, y, w, h) display_provider_shadow_push(x, y, w, h)

/* gfx/v2/display.c  (#include "display_provider.h" を足す) */
int gfx2_present(u32 id, u32 head, i16 x, i16 y, u16 w, u16 h)
{   ...
    int rc = context(id, head, 0, &a, &c);
    if (rc) return rc;
    if (!DISPLAY_V1_PRESENT_OK()) return OS32_ERR_BUSY;   /* 門: alias 探し・sync_get・dirty の交差より前 */
    ... (alias 探し、sync_get、切り詰め、空なら 0 — 今の字面のまま) ...
    rc = DISPLAY_V2_SHADOW_PUSH(l, t, r - l, b - t);
    if (rc == OS32_ERR_NOSYS) {                           /* x86 は必ずこちら (定数)。中は今の 3 行のまま */
        rc = gfxsync_write_rows(ctx, l, t, r - l, b - t, s->kva + t * s->pitch + l * cpp, s->pitch);
        if (rc) return OS32_ERR_IO;
        s->dirty = (struct gfx2_rect){0};
        return 0;
    }
    if (rc) return rc;                                    /* PowerMac: IO。dirty は残す */
    if (l == s->dirty.x && t == s->dirty.y &&             /* 転送が dirty の全体を覆った時だけ消す */
        r == s->dirty.x + s->dirty.w && b == s->dirty.y + s->dirty.h)
        s->dirty = (struct gfx2_rect){0};
    return 0;
}
/* gfx2_flip (display.c): context の `if (rc) return rc;` の直後、gfx2_flip_poll の前 */
    if (!DISPLAY_V1_PRESENT_OK()) return OS32_ERR_BUSY;
/* gfx2_mode_set (gfx/v2/modeset.c の gfx2_mode_set の所有者の BUSY の直後。#include "display_provider.h" を足す) */
    if (!DISPLAY_V1_PRESENT_OK()) return OS32_ERR_BUSY;

/* platform/powermac/display_provider.c — 門は入口にあるのでここには置かない */
int display_provider_shadow_push(int x, int y, int w, int h)
{ return of_fb_present(x, y, w, h) ? OS32_ERR_IO : 0; }      /* !live は STALE → IO */
/* drivers/video/of_fb/backend_of_fb.c — v1 の present() と共用。成功で commits を 1 増やす */
int of_fb_present(int x, int y, int w, int h);
```

- **前提 (明記)**: PowerMac でこの 3 本を使えるのは、**公開の gfx_init / gfx_init_200 に成功したアプリが、gfx_shutdown を始めるまで**。門は present_owner (公開の init が付与) と present_generation == a->gen (shutdown の a->gen++ で失効) を見る。gfx_init を通らず gfx2 だけを使うアプリは 3 本とも BUSY (列挙・面・lock・submit は今のまま使える)。SDK の説明に 1 行足す (PowerPC の担当、公開の時)。
- なぜ入口か: (1) shutdown・復帰で alias の dirty が消えるので、門が後ろだと present は 0 で戻る。(2) gfx2_surf_get (gfx/v2/surface.c の gfx2_surf_get) は sid・所有者だけを見てアダプタ世代を見ないので、shutdown の前に作った通常の SYSMEM 面は復帰の後も有効で、所有権も終了まで残る。共通の所有者の照合だけでは mode_set・flip が端末の上に全面を写す。(3) surface_copy・口に門を置くと、端末自身の再描画 (gfx/gfx_console.c の gfx_console_open・gfx_console_present → be->mode_set・be->flip → surface_copy) が、アプリの文脈 (appslot_cur がアプリ) や公開の init の無い時に止まる。入口なら端末・gfx2_head_restore_v1 (gfx/v2/display.c の gfx2_head_restore_v1) は通らない。
- dirty: PowerMac の道は、(BUSY) 門で断られた時・(IO) 写しの失敗の時は残す。成功でも要求が dirty の一部だけなら残す (外接矩形のまま。削らない — 次の present が残りを出せれば足り、重ねて写すのは害が無い)。x86 の道は `if (rc == NOSYS)` の中に今の 3 行をそのまま入れるので、部分でも全体を消す今の振る舞いのまま (**PM 決定**)。
- 空の dirty・DIRECT・同期の ctx 無しの 0 は門の後ろで今のまま。保護行だけの矩形は of_fb_copy が 0 を返す (写さず成功、v1 と同じ)。
- x86: 門が定数 1、口が定数 NOSYS。所有者を照合しないのも今のまま (PM 決定)。**x86 の戻り値・dirty・命令列を変えない形**。build_compare で gfx2_present・gfx2_flip・gfx2_mode_set に意味の違う差が出たら止めて未決に上げる。

### 2-4. (b) check_op と gfx2_submit の一致 — 不一致が 1 組あり、公開しない

- **不一致 (sol 第 2 回 P1)**: INDEX8 同士の ALPHA。check_op は SW と答える (gfx/v2/check_op.c の check の列に当たらず、shadow・visible の枝で SW) が、実行は gfx2_sw_alpha が呼ぶ gfx2_from_argb(INDEX8, …) が NOSYS を返す。有効な面とコマンドでも、描く範囲が空でなければ合わない。OF FB は INDEX8 だけなので、ALPHA は必ずこの組になる。
- **PM 決定**: check_op の規則は据え置く (x86 の答えを変えない)。**gfx2_check_op は 5c では公開せず不在のまま**。公開の順・ゲストの受入・公開表の変更から外す。
- **将来の公開の条件 (1 行)**: INDEX8 同士の ALPHA の不一致が解け (規則か SW のどちらを直すにも x86 に触れるのでユーザー判断)、下の host の一致の試験が既知の不一致なしで緑になり、仮の結線のゲストで同じ表が一致すること。
- host の一致の試験 (A4、本線) は 5c で入れる。今の状態を固定し、将来の公開の条件を機械で見るため。対象は OF FB で実在できる組 (INDEX8、非 alias の SYSMEM、DISPLAY の alias)、op ごとに有効なコマンドを 1 個以上 (同じ面の重なる STRETCH・ALPHA を避ける、寸法が 0 でない、範囲の中):

| check_op の答え | submit で確かめる結果 |
|---|---|
| SW (描く op: FILL・COPY・LINE・MONO・HOSTDATA・ROP・ROP4・STRETCH・CKEY のうち規則が SW と答える物) | rc = 0、宛先の画素が期待の値に変わる (alias なら dirty が付く) |
| SW (READBACK) | rc = 0、読み出した領域が面の画素と同じ。面は変わらない |
| SW (SET_PATTERN) | rc = 0、描かない。続く F_PAT 付きの op がその模様で描く |
| NOSUPPORT (INDEX8 への FILTER 付き STRETCH など) | `how == GFX2_HOW_NOSUPPORT` を**明示して確かめ**、rc = NOSYS (gfx/v2/submit.c の check_one)、宛先は変わらない |
| **SW (ALPHA、INDEX8 同士) — 既知の不一致** | rc = NOSYS を**期待として固定** (解けた時にこの行が赤になり、公開の条件の見直しに気づく) |
| HW | OF FB では出ない (出たら不合格) |

- 実在できない組 (INDEX8 以外の形式、VRAM・GART) への形の問いは caps (drivers/video/of_fb/of_fb_v2.c の shim_caps) が先に断る約束で、試験の対象にしない。

### 2-5. (c) gfx_stats の計数の責務 (OF FB)

| 経路 | present_bytes | commits |
|---|---|---|
| v1 の present_rect・列の掃き出し (of_fb_present) | 書いた語 × 4 × 行 (端の丸めを含み、保護行・切り落としを除く) | 成功 1 回で +1。列の掃き出しは共通が 1 回ぶんに揃える (5b) |
| gfx2_present の口 (of_fb_present) | 同じ | 成功で +1 |
| **保護行だけの present** (写す行が 0) | **+0** | **+1** (成功した commit。x86 の「空の矩形でも commit」と同じ数え方) |
| gfx2_mode_set・gfx2_flip (surface_copy) | 同じ式で足す | **足さない** |
| **端末** (gfx_console.c → be->mode_set・be->flip → of_fb_copy) | 同じ式で足す | **足さない** |
| !live (STALE)・不正な引数 | +0 | +0 |

present_bytes は of_fb_copy の中 (全部の呼び手)、commits は of_fb_present だけ。hw_ops・io_accesses は 0 のまま。端末の出力が bytes に混ざるので、アプリは端末が動かない区間の差で読む (A6 は gfx の中で前後を採る)。

## 3. 部品の大きさと x86 の差の見込み

| 部品 | 行 (見込み) |
|---|---|
| gfx/v2/display.c | +11 (include 1、門 2、口と dirty 8) |
| gfx/v2/modeset.c | +2 (include 1、門 1) |
| platform/pc98/platform_display_fallback.h | +1 |
| platform/powermac/platform_display_fallback.h | +2 |
| platform/powermac/display_provider.c | +2 |
| drivers/video/of_fb/backend_of_fb.c・.h | +9 / +1 |
| sdk/kapi_target_ppc.py | gfx の集合 12 → 7、gfx2 の集合 4 → 1 (gfx2_check_op が残る) |
| 新しい本体の TU | 無し |

x86 の build_compare: **関数差 0 を見込む** (門は `if (!1)`、口は `if (NOSYS == NOSYS)` に畳まれ、中は今の 3 行)。見込みで受け入れず実測を票に貼る。3 関数のどれかに差が出たら命令列を並べて報告し、意味が違えば止めて未決へ。ほかの関数に差が出たら止める。

## 4. ファイルごとの変更と段

| 段 | 担当 | ファイル (1 ファイル = 1 コミット) | 中身 |
|---|---|---|---|
| 5c-A1 | PowerPC | drivers/video/of_fb/backend_of_fb.c・.h | of_fb_present、計数 (§2-5) |
| 5c-A2 | PowerPC | platform/powermac/platform_display_fallback.h | マクロと宣言 |
| 5c-A3 | PowerPC | platform/powermac/display_provider.c | display_provider_shadow_push |
| 5c-A4 | PowerPC | tools/tests/of_fb_host.c・test_of_fb.py | A3、固定モードの変異 (A8) |
| 5c-B1 | 本線 | platform/pc98/platform_display_fallback.h | マクロ (定数) |
| 5c-B2 | 本線 | gfx/v2/display.c | §2-3 (present・flip) |
| 5c-B3 | 本線 | gfx/v2/modeset.c | §2-3 (mode_set の門) |
| 5c-B4 | 本線 | tools/tests/gfx2_display_host.c・test_gfx2_display.py・gfx2_modeset_host.c、tools/checks/gfx2-display-host.py・gfx2-i3-host.py の covers、新しい一致の試験 (gfx2_check_equiv_host.c + test + tools/checks の登録。公開とは切り離した固定) | A1・A2・A2b・A2c・A4 |
| 5c-C0 | PowerPC | userland/ppc/gfx2prob.c・dispprb.c・新しい失効のプローブ (leaseprb.c、A9d)、tools/accept/ppc_gfx2.py・ppc_display.py (**仮の結線**: 台本が sdk/kapi_target_ppc.py の写しから対象のスロットを不在の集合から外して組む。**通常像と各変異像に同じ仮の結線を当て**、欠如の変異だけ対象のスロットを戻す。ppc_display.py が組む変異像 (gate-open・target-gate-open・owner-unchecked) の RAM root に gfx2prob を入れて起こす) | A5〜A9d を本番の表を変えずに通す |
| 5c-C1 | PowerPC | sdk/kapi_target_ppc.py・tools/ppc/test_kapi_binding.py | gfx の 5 本 |
| 5c-C2 | PowerPC | 同上 | gfx2 の 3 本 (mode_set・flip・present)。gfx2_check_op は集合に残す。台本の仮の結線を外す |

- 取り込みの順: **5b の全部 → 5c-A → 5c-B (B1〜B4 を 1 単位) → 5c-C0 (仮の結線でゲストと変異が緑) → 5c-C1・C2 (本番の表)**。循環は無い: ゲストの試験は公開の前に仮の結線で回り、公開の後は同じ試験が本番の表で回る。
- 公開の順 (本番の表): gfx の情報 3 本 → hw 2 本 → gfx2_mode_set・flip・present (5c-B の後。門が入る前には出さない)。gfx2_check_op は出さない。gfx の 5 本は 5c-B を待たない。
- 並列: 5c-A (PowerPC) と 5c-B (本線) は別ファイル。5c-C0 のプローブは 5c-A・B の間に書ける。公開表は PowerPC が直列。
- NP21/W: 5c-B2・B3 はその場の変更。関数差 0 なら既存の gfx2 の受入 (gfx2_i3) を 1 回、差が出たら PM が範囲を決める。

### 4-1. 触る既存の検査と固定 (n-1b。3ad37cd56 の木で名前を grep)

| 変える名前 | 当たる検査・固定 | 直し |
|---|---|---|
| gfx2_present・gfx2_flip・gfx/v2/display.c | test_gfx2_display.py (-I の列に platform/pc98 が無い。変異 'dirty retained on failure' の当て先 `if (rc) return OS32_ERR_IO;\n    s->dirty`)、tools/tests/gfx2_display_host.c の run、tools/checks/gfx2-display-host.py の covers。**test_gfx2_i3.py は display.c を実際に組む** (-I は kernel_includes.py が PC-98 の道を出す) が、tools/checks/gfx2-i3-host.py の covers に機種ヘッダが無い。test_gfx2_i3_guest.py が組むのは v1_shim.c (触らない)。fixtures/build_profile_x86.json・_ppc.json、tools/checks/gfx2_wiring.txt | display の試験に -I と covers (platform/pc98・powermac の platform_display_fallback.h、include/display_provider.h)。I3 host は covers だけ。変異の当て先は、2 行が x86 の枝の中へ入り字下げが変わるので**新しい字面へ直し、一意に当たることと実行時の RED を確かめる**。PowerMac の形の観測を足す |
| gfx2_mode_set・gfx/v2/modeset.c | gfx2_modeset_host.c・その test と tools/checks の登録、gfx2_enum_host.c、gfx2_i3_host.c | modeset を組む試験に display_provider.h が通ること (-I・covers)。門が 0 を返す形の 1 本を足す |
| of_fb_copy・backend_of_fb.c・of_fb_v2.c・gfx_counters | of_fb_host.c (gfx_counters の実体が無い)、test_of_fb.py の既存の変異 **'pitch'・'aligned-last-word'・'bound'・'protected-band'・'surface-connect'・'lost'** (当て先の字面を動かさない。計数は行を足すだけにする)、fbcon_contract_host.c (gfx_counters の定義 に実体)、test_gfx_console.py、build/platform/powermac.mk | of_fb_host.c に実体と期待。新しい変異 'fixed-mode' (shim_mode_set の `m->id != 1 || cm != 1 || …` を外し、of_fb_v2.mode_set を違う id・寸法で直に呼んで NOSYS を見る)・'count-commit'・'count-bytes' |
| powermac/display_provider.c・platform_display_fallback.h (両機種) | test_of_fb.py の **'present-owner'・'present-generation'・'present-reset'** (5b)、tools/accept/ppc_display.py の **GATE の字面・'gate-open'・'owner-unchecked'・'target-gate-open'・KB_KEEP_RESET の -u の列** (GATE とリンク引数)、5b の gfx_present_host.c・test_gfx_present.py (両機種のヘッダで組む)、test_trident_module.py、test_gfx_policy_triggers.py、fbcon_host.c、test_sep5_provider.py・_display.py・_gfx_init.py、check-layers・layers-ppc | 門の本体の字面は変えない (当て先を保つ)。-u の列に display_provider_shadow_push を足す。ヘッダの写しを持つ試験は新しいマクロの行を足す。'gate-open' は gfx2 の 3 入口にも効くので、gfx2prob の shutdown の観測でも RED を見る |
| PROVIDER_GROUPS・EXPECTED_ABSENT_SLOTS | tools/ppc/test_kapi_binding.py (check ppc-kapi-binding)、**tools/ppc/test_kapi_table.py (check ppc-kapi-table、同じ表を引く)**、tools/tests/test_arch_compat.py、sdk/gen_kapi.py、tools/accept/ppc_*.py。**tools/accept/ppc_gfx2.py の absent_bindings の 'absent' の変異は `    'gfx2': (frozenset({\n` に差し込む** | 'gfx2' の組は gfx2_check_op が残るので消えない (当て先が 1 個のまま)。期待の集合を 8 本ぶん減らす。binding の検査に 8 本の通常ラッパーと実ターゲットを明記 (A11) |
| gfx2_check_op・check_op.c | test_gfx2_check_op.py・gfx2_check_op_host.c、test_gfx2_submit.py・gfx2_submit_host.c、その checks | check_op.c・submit.c は変えないので直し無し。一致の試験は新しいファイル。gfx2_check_op は公開表・binding の期待で不在のまま |
| gfx2prob・dispprb | tools/accept/ppc_gfx2.py (今は列挙の gfx2prob を起こすだけ。EXPECTED は 'normal'・'mode-width'・'absent')、ppc_display.py の PROBES、build/ppc-user.mk、tools/checks/ppc-gfx2-guest.py・ppc-display-guest.py | gfx2prob に描画・present・mode_set・flip・shutdown・owner の観測を引数で足す (引数なしの今の出力 `GFX2 OK` は変えない)。EXPECTED に場合を足す |
| gfx_screen_info・gfx_get_framebuffer・gfx_stats・gfx_hw_* の本体 | 変えないので x86 の host 試験は触らない | — |

### 4-2. 受入 → 試験と変異

| # | 受入 | 試験 | 変異 (1 行) |
|---|---|---|---|
| A1 | x86 の形: 書きの失敗は IO で dirty が残り、成功で (部分でも) 消え、口は呼ばれず、門は効かない | host: gfx2_display_host (今の期待のまま) | 既存 'dirty retained on failure' |
| A2 | PowerMac の形: 門が偽なら BUSY で、口を呼ばず dirty を触らない。**(i) alias があり dirty が空、(ii) alias があり同期の ctx が無い、を別々に**確かめる (どちらも門が無ければ 0 が返る場面)。門が真なら切り詰めた矩形で口を 1 回、gfxsync_write_rows は呼ばない。口が IO なら IO で dirty が残る | host: 同じ TU を platform/powermac のヘッダと偽の門・口で組む | gfx2_present の門の行を **`if (r <= l \|\| b <= t) return 0;` の早期 return の後ろ**へ移す ((i)・(ii) とも 0 が返り赤) / `rc == OS32_ERR_NOSYS` を `1` に |
| A2b | PowerMac の形: dirty (10,10,20,20) に要求 (10,10,20,5) → 口は上の 5 行、**dirty は残る** → 要求 (0,0,全面) → 口は dirty の全体、dirty が消える | 同上 | 全体を覆う時の条件を外して常に消す |
| A2c | gfx2_flip・gfx2_mode_set: 門が偽なら BUSY で be->flip・be->mode_set を呼ばない。x86 の形では今のまま | host: display・modeset の試験 | 各入口の門の行を消す |
| A3 | of_fb_present: 成功で commits +1、present_bytes = 書いた語 × 4 × 行。**保護行だけは bytes +0 / commits +1**。of_fb_copy を直に (flip・端末の道) 呼ぶと bytes だけ増える。!live は数えない | host: of_fb_host | 'count-commit' / 'count-bytes' |
| A4 | OF FB の形の後端 (check = NOSYS、alloc = NOSYS、vram_cpu_bytes = 0、INDEX8、SHADOW) で、§2-4 の表を全 op・2 種の宛先で確かめる (ALPHA は既知の不一致として固定)。**公開の関門ではなく今の状態の固定** | host: gfx2_check_equiv_host.c (check_op.c・submit.c と SW の列を直に組む) | check_op.c の shadow の枝の `GFX2_HOW_SW` を `GFX2_HOW_HW` に → **実の alias への submit の描画の結果** (SW が描いた画素・dirty) で赤にする (形だけの check_op には効かない) / FILTER の行を外す → **`how == GFX2_HOW_NOSUPPORT` の明示の確かめ**で赤にする (submit は変換で NOSYS のままなので戻り値では取れない) |
| A5 | PowerMac (800x600x8): screen_info が実寸・bpp 8・PACKED8・lease 0。get_framebuffer の planes[0] が非 0・[1..3] が 0・pitch が面の記述と同じ。planes[0] へ書いた画素が gfx_present_rect で表示に出る | QEMU: check-ppc-display-guest、dispprb、7450・7455 | gfx_get_framebuffer の結線を欠如へ戻す |
| A6 | gfx_stats: 既知の矩形の present_rect の前後で bytes の差が式どおり・commits の差が 1。gfx2_present でも同じ。保護行だけは +0 / +1 | 同上 | A3 の変異を像に当てる |
| A7 | gfx2_present: 画面の alias へ submit で塗る → 表示は変わらない → present → 出る。丸めの外の番兵は変わらない。影の同じ位置が変わらない。**部分の present の後に残りの present で残りが出る**。dirty が空の 2 回目は 0 | QEMU: check-ppc-gfx2-guest、gfx2prob、7450・7455 | PowerMac のマクロを `OS32_ERR_NOSYS` の定数に |
| A8 | mode_set (id 1・cm 1・INDEX8・実寸の SYSMEM scanout) と flip (flags 0) で面が出る。VBLANK 付きは NOSYS。違う id・寸法は断る (ゲストでは共通の modeset.c と surface_copy も弾くので、**shim の固定モードの比較そのものは host で**) | QEMU: 同上 + host: test_of_fb.py | host: 'fixed-mode' / ゲスト: 既存 'mode-width' |
| A9a | **shutdown の前に作った通常の SYSMEM の scanout 面**で、**同じ所有者が** gfx_shutdown の後に gfx2_mode_set・gfx2_flip → どちらも BUSY、端末の画素は変わらない (flip・mode_set の門はこの場面で取る。共通の所有者照合は通るので門だけが断る)。gfx2_present も (dirty が空でも) BUSY。正常の復帰とターゲットの取り直しの失敗の両方 | QEMU: **ppc_display.py が組む像 (通常・gate-open・target-gate-open) の RAM root に gfx2prob を入れて** `gfx2prob shutdown`、画面の読み、7450・7455。プローブは生きたまま端末の画素と再描画を見る (古い VA には書かない) | 既存 'gate-open'・'target-gate-open' の変異像で 3 入口とも端末の上に出て赤 / flip の門の行だけ消す / mode_set の門の行だけ消す |
| A9b | **端末の再描画が止まらない**: A9a の後、同じアプリが動いたまま端末へ文字を出す → 表示に出る (be->flip の道)。終了の後のシェルの行も出る | 同上 (5b の shutdown の観測の続き) | surface_copy の先頭に門を足す (端末が出ず赤 — 門を装置へ下ろす誤りの検出) |
| A9c | 所有者でないアプリ・公開の gfx_init を通っていないアプリの gfx2_present が BUSY (flip・mode_set は共通の所有者照合が先に BUSY を返すので、門の RED は present で取る) | QEMU: 'owner-unchecked' の変異像の RAM root に gfx2prob を入れて `gfx2prob owner` | 既存 'owner-unchecked' (present が端末・所有者の画面に出て赤) |
| A9d | **古い framebuffer の VA の失効**: gfx_get_framebuffer で得た planes[0] を持ったまま gfx_shutdown → その VA へ書く → **期待したフォルトでプローブが終わる** (終了の理由と番地をログで照合)。端末の画素は変わらず、シェルが続く | QEMU: 別のプローブ leaseprb (死ぬのが期待。A9a の生きたプローブとは分ける)、7450・7455 | shutdown の lease の revoke を外す (フォルトせず、書きが通って赤) |
| A11 | 像が組め、**通常のラッパーと実ターゲット 8 組**: gfx_screen_info・gfx_get_framebuffer・gfx_stats → 同名、gfx_hw_fill_rect・gfx_hw_blit → 同名 (束縛のうえ NOSYS)、gfx2_mode_set・gfx2_flip・gfx2_present → 同名。**不在の 8 本** (gfx の 7 本と gfx2_check_op) は欠如の答え | PPC のリンク、ppc-kapi-binding・ppc-kapi-table | 不在の集合に gfx2_present を戻す / gfx2_check_op を集合から外す (期待の不在と食い違い赤) / 既存 'absent' (ppc_gfx2.py) が当たること |
| A12 | x86: 関数差が §3 の見込みの中 (実測を貼る) | build_compare | — |
| A13 | x86: gfx2 の表示の振る舞いが変わらない | NP21/W: gfx2_i3、host: 既存の gfx2 の列 | 既存の変異が実行時に RED |
| A14 | display.c・modeset.c が機種・装置のヘッダを直に読まない | check-layers・layers-ppc | display.c に `#include "backend_of_fb.h"` を足す |

## 5. 未決 (ユーザー判断が要るもの)

**0 件。** r1 の未決 1 と脇の 3 案は PM 決定で閉じた (冒頭)。ユーザーに戻すのは次の時だけ:

- DAC の装置を 5c の範囲に足す時、または x86 の答えを変える 3 案 (check_op の規則、共通の所有者照合、256 色の読み出し) のどれかを採る時。gfx2_check_op の公開 (ALPHA の不一致を解く直し) もここに入る。
- build_compare で gfx2_present・gfx2_flip・gfx2_mode_set に意味の違う差が出た時 (x86 の振る舞いが変わる形。止めて未決に上げる)。

## 6. 確かめていない物 ([V4])

- ビルド・build_compare・変異・ゲストの試験は回していない (起草もレビュー 2 回も読み取りだけ)。§3 は見込みで、x86 の命令列は build_compare 待ち。
- 3ad37cd56 で読み直したのは門 (platform/powermac/display_provider.c の display_provider_prepare_failed・display_provider_present_ok、platform_display_fallback.h)、be->flip・be->mode_set の呼び手、gfx2_surf_get、ppc_gfx2.py・ppc_display.py・test_of_fb.py の変異の名前。ほかの参照の基点は 9d99c1b64。
- 端末の再描画の時の appslot_cur・present_owner の値 (A9b。設計は門が端末の道に無いことだけに依る)。
- test_gfx2_display.py が kernel_includes.py を使うか、gfx2_modeset の試験と checks の登録の名前は grep だけ。
- レビューの照合で解けた物 (設計の前提として採った): 層の検査は display_provider.h → 像ごとの platform_display_fallback.h の形を通す。3 個の型は生成 C が読む os32_kapi_shared.h にあり、実ターゲットの宣言は gfx.h。SYSMEM の scanout は後端の alloc を通らず、800 幅の INDEX8 は pitch とオフセットの 4 バイトの整列を満たす。shutdown は CLIENT・DISPLAY の lease を revoke し、端末は別に確保した SYSMEM 面を使う。

## 7. 反論

- (P2 部分転送、一部) dirty を「残りの矩形」に削る形は採らない。外接矩形のまま残す — 1 個の矩形で持つ今の型では L 字の残りを表せず、重ねて写しても害が無い。指摘の直し案 (全体を覆う時だけ消す・残りを出せる試験) は採った。

## 確定 (2026-11-20、sol 第 2 回「条件付きで確定してよい」、PM 決定を反映)

- **P1 (§2-4・A4・A10) ALPHA の不一致** → PM 決定: check_op の規則は据え置き (x86 の答えを変えない)。gfx2_check_op は 5c では公開せず不在のまま。公開 8 本・不在 8 本。A10 と公開の順から外し、A4 は不一致を固定する host の試験として残す。将来の公開の条件は §2-4 の 1 行。
- **P2 (A4) 変異の取り方** → SHADOW の変異は実の alias への submit の描画の結果で、FILTER の変異は `how == NOSUPPORT` の明示の確かめで赤にする。
- **P2 (A2) 門の変異の位置** → 門を `r <= l || b <= t` の早期 return の後ろへ移す変異にし、空の dirty と ctx 不在を別々に確かめる。
- **P2 (§4・A9a・A9c) 変異像** → ppc_display.py が組む gate-open 系・owner-unchecked の変異像の RAM root に gfx2prob を入れて起こす。owner-unchecked の RED は present で、flip・mode_set の門は同じ所有者の shutdown の後の呼び出しで取る。
- **P2 (§6・A9a) 旧 VA の失効** → 別のプローブ (A9d) で期待のフォルトまで観測し、端末の画素と再描画は生きたプローブ (A9a・A9b) で見る。
- 第 1 回の 8 件は r2 で反映済み (冒頭の表)。反論 1 件 (外接矩形のまま残す) はレビューが妥当と認めた。
- 取り込みの順: 5b の全部 → 5c-A → 5c-B → 5c-C0 (仮の結線) → 5c-C1・C2。

