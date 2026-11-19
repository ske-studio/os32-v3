# PPC_KAPI_FILL 段 5b の確定設計 (共通の present の口)

> 状態: **確定 (2026-11-20)** — 本線 PM。Fable 起草、sol レビュー 2 回、確定の節を PM が反映。出所は本線の機械の ~/os32-tmp/run/kfill-5b/。

# 段 5b 詳細設計 r2 — v1 の present の共通の口と PowerMac の後端

対象: PPC_KAPI_FILL_DESIGN.md §3 の段 5b。読む木 origin/main 0683e23ab (r1 の行番号は 73d98b9b1 で採った。r2 で足した所だけ読み直した)。ビルド・試験は回していない。

## r1→r2 の変更 (REVIEW.r1 の指摘ごと)

| 指摘 | 直し |
|---|---|
| P1 PPC の公開条件 | §2-6 を新設。PowerMac だけ 6 本の入口に門 (所有者 + 端末が戻っていない) を置く。x86 は定数 1 で命令列を変えない。受入 A11・A12 を追加 |
| P1 取り込み順 | §4 の段を組み直し。口の宣言 + PowerMac の空実装を先 (5b-A)、共通の TU と gfx_vram.c の削りと build-selection の fixture を 1 単位 (5b-B) |
| P2 7 件の照合 | 食い違い 3 を「幅を使うのは add_dirty_rect と全画面の present の 2 入口、present_rect は引数のまま」に直し、§2-3 の断片を gfx_v1_present_rect の直呼びと明記 |
| P2 境界と x86 の差 | 口のヘッダを include/gfx_present_contract.h に替え、型の依存と呼ぶ条件を書いた。ラスタの移す範囲を行番号でなく「commits++ から pc98_splash_raster_table の呼び出しまで」に。§3 に実測と既存トレースの照合を追加 |
| P2 転送カウンタ | §2-5 に計数の責務の行を追加。5b では OF FB の計数を変えず、5c (gfx_stats の公開) へ申し送り |
| P2 既存試験の追従 | 5b-B に test_sep5_gfx_init.py の抽出名と test_boot_splash_native.py の fixture の追従、既存変異の実行時 RED の確認を追加 |
| P2 ゲスト受入と変異 | A4〜A6 を dispprb の画素の観測に書き直し (800x600x8、x=799、640 より右、番兵、7450・7455)。A5 の「構成が無ければ未確認」を削除 |
| P2 部分公開 | A6 の実寸と画素は公開済みの面・lock (gfx2_screen_surface・gfx2_surface_desc・gfx2_lock) で採る。A7 に通常 wrap・ヘッダ除外・残りの不在 12 本の検査を追加 |
| P2 統合と 5c | §1-3 の 3 点目を既存の計画・確定設計との整合に書き直し。5c への申し送りを §4-2 にまとめた |

PM 決定 (r1 の未決 2 件は確定): gfx_init_200 は PowerMac で実寸で公開。v1 の present の幅は x86 を 640 のまま、PowerPC は選択モードの幅。**200 行の保証・x86 の幅の変更・check_op の公開の判定の規則の変更を採るならユーザーに戻す。**

## 1. 実物の確認

### 1-1. 計画の前提と実物の食い違い (7 件)

| # | 計画の前提 | 実物 | 設計への効き |
|---|---|---|---|
| 1 | 5b の本線の対象は gfx/gfx_core.c | present の 6 本は全部 drivers/video/pc98/gfx_vram.c (:84 add_dirty_rect、:183 present_dirty、:197 present_nosync、:207 present、:219 present_rect、:239 present_raster)。この TU は build/features/pc98-devices.mk:3 で PC-98 だけが組む。gfx_core.c に present は無く、持つのは列の実体 (gfx_core.c:42-43 dirty_queue・prev_dirty) だけ | 「gfx_core が渡す」でなく、**gfx_vram.c の機種に依らない半分を新しい共通 TU へ移す**段になる |
| 2 | INDEX8 の変換は既存 OF FB を使う | 変換は無い。OF FB は深さ 8 だけ受ける (platform/powermac/display_provider.c:61 `fb->depth != 8` で不採用)。影 (PACKED8) → 表示は同じ形式の 32 ビット語の写し (backend_of_fb.c の of_fb_copy)。pitch は影 = 表示 = fb->stride | 5b で変換の部品は作らない。pitch は既存の of_fb_copy に任せる |
| 3 | (暗黙) 変更矩形の切り詰めは機種に依らない | 幅を使う入口は 2 個: gfx_add_dirty_rect (gfx_vram.c:86) と全画面の gfx_present (:213) が GFX_WIDTH = 640 (gfx/gfx.h:15) に固定。gfx_present_rect は引数をそのまま後端へ渡す (切り詰めは後端)。OF FB の幅は 4 の倍数なら何でも通る (display_provider.c:61-66) | そのまま移すと 640 を超える FB で右側が出ない。幅の出所を口にする (§2-3) |
| 4 | 200 行指定の実現範囲を照会 | gfx_mode_pick (include/gfx_hal.h:148-158) が 200 行を選ぶのは PLANAR4 の 200 行のモードがある時だけ。OF FB のモードは PACKED8 の 1 個 (backend_of_fb.c の of_fb_bind) なので legacy200 は既定のモードに落ちる。gfx_core_init.inc:1「packed 系はどちらの入口でも native 解像度」 | PowerMac の gfx_init_200 は今のコードのまま gfx_init と同じ結果 (x86 の PEGC・Cirrus と同じ規則)。足すコードは 0 行 (§2-5) |
| 5 | PPC の側は display_provider.c に後端を作る | 後端の present_rect は既にある (backend_of_fb.c:82、表 :93)。PPC は gfx_core.c・gfx_dirty.c を既に組む (build/sources/common.mk)。flip・VSYNC・プレーンの空の実装も既にある (platform/powermac/display.c:32-36) | PowerPC の仕事は公開表と受入が中心。display_provider.c に足すのは planar の口の空実装だけ |
| 6 | present は共通へ素直に移せる | gfx_present・_rect・_dirty は末尾で gfx_diag_px (gfx_vram.c:25) を呼ぶ。中身は PEGC の窓を読む診断 (pegc.h・serial_diag.h) | 診断は装置に残し、共通から口 1 本で呼ぶ |
| 7 | gfx2 の present は of_fb_v2 の ops | gfx2_present (gfx/v2/display.c:142-172) は ops を通らず gfxsync_write_rows。PowerMac の同期の先は**影** (display_provider.c:35 `.va = saved.shadow`、注記「generic sync writes never target this device」)。表示へ届くのは of_fb_copy (v1 の present_rect・shim_flip・shim_mode_set) だけ | 5b では触らない。**5c への申し送り**: gfx2_present を公開する前に、影 → 表示の写しを誰が呼ぶかを決める (ここでは display.c と provider の init しか読んでいない) |

### 1-2. 食い違いでない確認

- 変更矩形の列の処理は 5a-ii で gfx/gfx_dirty.c に入っている (gfx_dirty.h: gfx_dirty_add・gfx_dirty_merge・gfx_dirty_at)。
- PowerPC の「無いはず」は gfx 19 本・gfx2 4 本 (sdk/kapi_target_ppc.py:21-40、tools/ppc/test_kapi_binding.py:24-31)。
- check_op: 規則は共通の check() (gfx/v2/check_op.c:18-45)、装置の答えは be->check。OF FB の shim_check (of_fb_v2.c:131、表 :243) は NOSYS を返すが、共通の規則は条件により SW と答える (レビューの照合)。**5b は check_op.c に触らない。x86 の答えは変わらない**。

### 1-3. 統合の判断 3 点 (POLICY_DEV §1)

| 点 | 判断 |
|---|---|
| 同じ責務の実装が既にあるか | ある (gfx_vram.c の packed の道 :61-78 と present・present_rect)。PowerMac 用に 2 個目を書かず、これを共通へ移して両機種で使う |
| 口は既存の物で足りるか | 後端への渡しは GfxBackend.present_rect (include/gfx_hal.h:78) で足りる。GfxBackend に欄は足さない (Cirrus・GeForce・Trident のモジュールと構造体の形を共有するため)。planar と診断はリンク時の口 3 本 |
| 既存の計画・確定設計と合うか | 計画 §2 の gfx の行「C は present 系と dirty の共通方針を先に移設」と段 5a の続き。AC-KAPI-1 (型別の欠如: 残り 12 本は不在のまま、通常・欠如の両方を試験)、AC-DISPLAY-2 (能力差: ラスタ・VSYNC・200 行は PowerMac で能力どおり) を両像の同じ host 試験 (A1〜A3) に掛ける。DISPLAY_HOOKS §3「shutdown の後の present は端末の画素を変えない」は §2-6 の門で満たす。置き場: 共通 = gfx/gfx_present.c (新、1 機能 1 ファイル)、装置 = gfx_vram.c、機種 = platform/powermac |

## 2. 口の形

### 2-1. 分け方

| 物 | 持ち主 | 置き場 |
|---|---|---|
| 列の実体 dirty_queue・prev_dirty | 共通 | gfx/gfx_core.c:42-43 (動かさない) |
| 列の足し・併合 | 共通 | gfx/gfx_dirty.c (5a-ii、動かさない) |
| KAPI の入口 6 本、v1 の橋 (gfx_v1_present_rect)、packed の列の掃き出し | 共通 | **gfx/gfx_present.c (新)** |
| planar の転送・ステイルページの併合・flip・VSYNC 待ち・ラスタの再生・PEGC の診断 | 装置 | drivers/video/pc98/gfx_vram.c (残す) |
| 影 → 表示の写し、保護行、語の端の扱い | 装置 | drivers/video/of_fb/backend_of_fb.c (変えない) |
| planar の口の空実装、v1 の幅、公開の門 | 機種 | platform/powermac/display_provider.c・platform_display_fallback.h |

### 2-2. 共通と装置の境目 (新しいヘッダ include/gfx_present_contract.h)

```c
#include "os32_kapi_shared.h"   /* GFX_RasterPalTable, GFX_RASTER_MAX_ENTRIES。型の依存はこれだけ */
/* planar (PLANAR4) の後端だけが実装する。packed の後端しか無い機種は空で定義する。
 * 呼ぶ条件 (呼ぶ側 gfx_present.c が保証): 通常の文脈、bb_b != 0、g_backend が packed でない、
 * raster は table != 0 かつ 0 < count <= GFX_RASTER_MAX_ENTRIES。
 * 口の中の責務: commits・present_bytes・io_accesses の計数、列を空にする、prev_dirty の更新、
 * VSYNC 待ち (sync != 0 かつ非 flip の時だけ)・flip・ラスタの再生。 */
void gfx_planar_present_dirty(int sync);              /* 旧 _present_dirty の flip / 非 flip の枝 */
void gfx_planar_present_raster(GFX_RasterPalTable *t);/* 旧 gfx_present_raster の commits++ 以降 */
void gfx_present_diag(void);                          /* 旧 gfx_diag_px。無い機種は空 */
```

### 2-3. 共通の TU (gfx/gfx_present.c) — 中身は gfx_vram.c から字面のまま移す

```c
#include "gfx_dirty.h"            /* gfx_internal.h: bb_b, dirty_queue, prev_dirty */
#include "gfx_hal.h"              /* g_backend, gfx_counters */
#include "v2/gfx2_int.h"          /* v1_bridge_begin / end */
#include "display_provider.h"     /* DISPLAY_V1_WIDTH (下) */
#include "gfx_present_contract.h"

static void gfx_v1_present_rect(int x, int y, int w, int h);   /* :15-22 を移す */
static int  present_dirty_packed(void);                        /* :61-78 を移す */

void __cdecl gfx_add_dirty_rect(int x, int y, int w, int h)
{ if (bb_b) gfx_dirty_add(&dirty_queue, DISPLAY_V1_WIDTH, gfx_current_height, x, y, w, h); }

static void present_dirty(int sync)
{ if (!bb_b) return; if (present_dirty_packed()) return; gfx_planar_present_dirty(sync); }

void __cdecl gfx_present_dirty(void)   /* before; present_dirty(1); gfx_diag_first_present; gfx_present_diag */
void __cdecl gfx_present_nosync(void)  /* before; present_dirty(0); gfx_diag_first_present */
void __cdecl gfx_present(void)         /* gfx_v1_present_rect(0, 0, DISPLAY_V1_WIDTH, gfx_current_height) を直に呼ぶ (KAPI の gfx_present_rect は通らない) */
void __cdecl gfx_present_rect(int rx, int ry, int rw, int rh)  /* gfx_v1_present_rect(rx, ry, rw, rh)。幅は使わない */
void __cdecl gfx_present_raster(GFX_RasterPalTable *table)
{   /* 順を保つ: bb_b → table の検査 → packed なら列の掃き出しで終わり → planar の口 */
    u32 before = gfx_counters.present_bytes;
    if (bb_b && table && table->count > 0 && table->count <= GFX_RASTER_MAX_ENTRIES &&
        !present_dirty_packed()) gfx_planar_present_raster(table);
    gfx_diag_first_present(before);
}
```

幅の口 (機種のヘッダ platform_display_fallback.h。display_provider.h:10 の「-Iplatform/$(PLATFORM) で選ぶ」既存の形に 1 行ずつ足す):

```c
/* platform/pc98:     */ #define DISPLAY_V1_WIDTH GFX_WIDTH      /* 定数のまま = x86 の命令列は変わらない */
/* platform/powermac: */ #define DISPLAY_V1_WIDTH ((int)gfx_backend_mode(g_backend)->width)
```

- PowerMac の g_backend は of_fb か "none" (display_provider.c:11)。"none" はモードを持たず gfx_backend_mode が NULL を返すが、その時は bb_b == 0 なので式に届かない (入口 6 本は全部 bb_b を先に見る)。実装者はこの順を崩さない。
- GFX_RASTER_MAX_ENTRIES と GFX_RasterPalTable は os32_kapi_shared.h (共通)。splash_raster.h は共通へ持ち込まない。

### 2-4. 装置の側 (gfx_vram.c に残る形)

- `_present_dirty` の flip / 非 flip の枝 (:158-177) を `gfx_planar_present_dirty(int sync)` に改名して外部へ。先頭の `if (!bb_b)`・packed の判定は共通へ移った分だけ消す。
- `gfx_present_raster` の `gfx_counters.commits++` から `pc98_splash_raster_table(pc98_splash_raster_state(), table)` の呼び出しまで (呼び出しを含む。転送 → 再生の順ごと) を `gfx_planar_present_raster` に。
- `gfx_diag_px` を `gfx_present_diag` に改名して外部へ (中身は同じ)。
- `_flush_dirty_queue`・`_merge_prev_dirty`・`_flip_page` は static のまま。display_wait_vsync・display_flip_pages はここからだけ呼ぶ (**flip・VSYNC 待ちは装置側**、[HW1] のとおり GDC は platform の display_* 越し)。
- 変異の目印 (tools/tests/test_boot_splash_native.py:22-33 の 'omit nonflip transfer'・'omit raster flip' ほか) は残る側の行なので、字面を変えない。

### 2-5. PowerMac の後端

| 項目 | 形 |
|---|---|
| present・present_rect | 共通 → g_backend->present_rect → of_fb_copy。切り詰め・保護行 (protected_rows)・語の端は of_fb_copy が持つ。コードの追加なし |
| add_dirty_rect・present_dirty・present_nosync | 共通の列 → packed の掃き出し → present_rect を矩形ごとに。commits は 1 回ぶんに揃う (移した字面のまま)。sync は無視される (OF に VSYNC 待ちが無い。display.c:35 が空) |
| present_raster | packed なので列の掃き出しだけ。パレットの表は使わない = x86 の PEGC・Cirrus と同じ答え (計画 §5「非対応 raster は能力に一致」) |
| planar の口 3 本 | display_provider.c に空実装 3 行 (呼ばれない。--no-undefined のため) |
| 計数 (契約 G7) | OF FB の present_rect は commits も present_bytes も数えない (backend_of_fb.c:82)。5b の後は列の掃き出しだけ commits が 1 増え、直接の present・present_rect は 0 のまま、present_bytes は常に 0 (gfx_diag_first_present の差も 0)。**5b では変えない** (gfx_stats は不在のままで、アプリから見えない)。gfx_stats を公開する 5c で OF FB の計数の責務 (of_fb_copy が書いた語数、直接の present の commits) を決める |
| gfx_init_200 | **足すコードなし**。門 (gfx_kapi_init_200) → gfx_init_mode(1) → OF FB の 1 個のモード。高さは実寸 (gfx_screen_info・gfx_get_framebuffer が返す値)。行を 2 度打ちする 200 行の見かけは作らない (未決 1) |

### 2-6. PowerMac の公開の門 (shutdown の後・非所有者)

実物: gfx_shutdown の後、端末の復帰 (platform/powermac/console_text.c の display_provider_console_return) が gfx_init() を呼ぶので bb_b と gfx_started は有効に戻る。present の生成ラッパーに所有者の門は無く、gfx_v1_present_rect は v1_bridge_begin の戻り値を捨てる。門が無いと shutdown したアプリ・所有者でないアプリの present が端末の画素を書き換える。

```c
/* platform/pc98/platform_display_fallback.h     */ #define DISPLAY_V1_PRESENT_OK() 1   /* x86 は今のまま */
/* platform/powermac/platform_display_fallback.h */ int display_provider_present_ok(void);
                                                    #define DISPLAY_V1_PRESENT_OK() display_provider_present_ok()
/* platform/powermac/display_provider.c */
int display_provider_present_ok(void)
{   /* 許す条件: 呼んだアプリが画面の所有者で、かつ端末が FB に戻っていない */
    return !pm_console_text_ready() && appslot_gfx_owner() >= 0 &&
           appslot_gfx_owner() == <今のアプリの番号>;
}
```

- 共通の 6 本は `if (!bb_b)` の所を `if (!bb_b || !DISPLAY_V1_PRESENT_OK())` にする。断った時は何もせず戻る (6 本とも void。列も触らない)。
- 「表示セッションの世代」は端末の状態で代える: shutdown → 復帰で pm_console_text_ready() が真になり、同じアプリがもう一度 gfx_init (門 gfx_kapi_init) を通るまで偽に戻らない。所有者は終了で外れる (display_provider_owner_exit、appslot_gfx_owner_exit)。
- カーネル内部: PowerMac のカーネルはこの 6 本を呼ばない (今は組まれていない)。端末の描画は今のまま後端を直に使う。x86 のカーネル内部の呼び出し (スプラッシュ、backend_pc98_osri.c:73-74 ほか) は門が定数 1 なので変わらない。
- 実装の前に確かめる 3 点 (読んでいない): <今のアプリの番号> の取得口の名前 (exec/appslot.h・exec.h の既存の物を使い、足さない)、gfx_init の enter (display_text_hide) で pm_console_text_ready() が偽になること、起動の端末の用意 (pm_console_text_start) が 6 本を呼ばないこと。どれかが違えば止めて報告。

## 3. 部品の大きさと x86 の差の見込み

| 部品 | 行 (見込み) | 備考 |
|---|---|---|
| gfx/gfx_present.c (新) | 約 95 | うち約 80 が移動 |
| include/gfx_present_contract.h (新) | 約 15 | |
| drivers/video/pc98/gfx_vram.c | 264 → 約 185 | |
| platform_display_fallback.h ×2 | +2 / +3 | 幅と門 |
| platform/powermac/display_provider.c | 125 → 約 137 | 空実装 3 本、門、include |
| build/sources/common.mk・order.mk | +1 語ずつ | order.mk:16 は gfx_dirty.c の隣 |

x86 の build_compare の見込み: **関数差 0 にはならない。移すだけの差**。

- 置き場が変わる関数 (中身は同じ): gfx_add_dirty_rect・gfx_present・gfx_present_rect・gfx_v1_present_rect (インライン先も同じ TU に付いて行く)。
- 形が変わる関数: gfx_present_dirty・gfx_present_nosync・gfx_present_raster (今は _present_dirty と packed の道が同じ TU でインラインされている。TU の境目で call が 1 本ずつ増える)。新しい外部記号 3 個 (gfx_planar_present_dirty・gfx_planar_present_raster・gfx_present_diag)。
- 変わらないはずの物: _flush_dirty_queue・_flip_page・gfx_dirty.c・gfx_core.c・後端 4 種。番地の変位だけ。
- 診断の外部化 (gfx_diag_px → gfx_present_diag) は cold の static が外部の call に変わるので、呼ぶ 3 関数の命令列も変わり得る。**見込みで受け入れず、build_compare の実測の一覧を票に貼る**。
- 受け入れる差は 5a-ii と同じ書き方で票に列挙する (上の 7 関数と 3 記号、変位、整列)。それ以外の関数に差が出たら止めて報告。
- 順の照合 (既存のトレース・host 試験で): 非 flip は sync の時だけ VSYNC を待ってから転送、flip は併合 → 転送 → prev_dirty の更新 → ページ切替、ラスタは転送 (flip なら切替まで) → 再生。

## 4. ファイルごとの変更と段

| 段 | 担当 | ファイル (1 ファイル = 1 コミット、取り込みは段の単位) | 中身 |
|---|---|---|---|
| 5b-A1 | 本線 | include/gfx_present_contract.h (新) | 口 3 本の宣言と呼ぶ条件 (§2-2) |
| 5b-A2 | 本線 | platform/pc98/platform_display_fallback.h | DISPLAY_V1_WIDTH・DISPLAY_V1_PRESENT_OK (定数) |
| 5b-A3 | PowerPC | platform/powermac/platform_display_fallback.h | 同じ 2 個 (モードの幅、門の宣言) |
| 5b-A4 | PowerPC | platform/powermac/display_provider.c | 空実装 3 本と display_provider_present_ok (§2-6) |
| 5b-B1 | 本線 | drivers/video/pc98/gfx_vram.c | 共通の分を削り、planar の口 3 本を外部に (§2-4) |
| 5b-B2 | 本線 | gfx/gfx_present.c (新) + common.mk・order.mk + 両像の build-selection の fixture | §2-3・§2-6 |
| 5b-B3 | 本線 | tools/tests/: 新しい host 試験 gfx_present_host.c、test_sep5_gfx_init.py (:99 が `_present_dirty` を名前で抜く → gfx_planar_present_dirty と共通の present_dirty へ)、test_boot_splash_native.py (fixture に gfx_present.c、変異の当て先 :22-33)、test_gfx_attach.py:20・test_gfx_boot.py:217 のソースの列 | A1〜A3・A9 |
| 5b-C1 | PowerPC | sdk/kapi_target_ppc.py、tools/ppc/test_kapi_binding.py | PROVIDER_GROUPS['gfx'] の不在の集合から 7 本を外す (19 → 12) |
| 5b-C2 | PowerPC | dispprb (ゲストの観測プログラム) と tools/accept/ppc_display.py | A4〜A6・A11・A12 |

- **取り込みの順: 5b-A (A1〜A4 を 1 単位) → 5b-B (B1〜B3 を 1 単位) → 5b-C**。5b-A の後は口が宣言され PowerMac に定義があるだけで、どちらの像も今のまま組める (PowerMac の 4 個は未使用の記号)。5b-B で gfx_present.c が共通の一覧に入っても PowerMac のリンクは欠けない。5b-B1 と B2 は片方だけでは x86 が組めないので必ず同じ単位。
- 並列: 5b-A の中の本線 2 本と PowerPC 2 本は別ファイルで並列。5b-B (本線) の間に PowerPC は 5b-C2 の dispprb を先に書ける (公開前は呼び出しが欠如の答えになる)。5b-C1 は 5b-B の取り込みの後に直列 (公開表は PowerPC の担当が直列)。
- 5b-B は TU の境目が動くので NP21/W の受入を 1 回入れる (本線の取り決め 3 の直列の列)。
- 既存の変異 (test_sep5_gfx_init.py の VSYNC・nosync・flip の順、test_boot_splash_native.py の 'omit nonflip transfer'・'omit raster flip' ほか) は、当て先を直したあと**実行時に RED になる所まで**確かめて報告する (当たらず素通りは不合格)。

### 4-1. 受入 → 試験と変異

| # | 受入 | 試験 | 変異 (1 行) |
|---|---|---|---|
| A1 | packed の列の掃き出しが矩形ごとに present_rect を呼び、commits は 1 だけ増え、列が空になる | host: gfx_present_host.c (偽の後端で gfx_present.c + gfx_dirty.c を直に組む。両機種の幅・門のマクロで 2 通り) | `gfx_counters.commits = commits_before + 1` を消す |
| A2 | bb_b == 0 では 6 本とも後端・planar の口を呼ばない | 同上 | gfx_present_rect の `!bb_b` を外す |
| A3 | present_raster: 表が不正なら何もしない、packed なら planar の口を呼ばず列だけ掃く、planar なら口を 1 回呼ぶ | 同上 | packed の判定を外して常に planar の口へ |
| A4 | PowerMac (800x600x8、tools/accept/ppc_display.py の Session の指定): 保護行より下で右端 x=799 を含む矩形を add_dirty_rect → present_dirty し、表示の画素が出る。番兵 = 列の 32 px の丸めと of_fb_copy の 4 px の丸めの外に置いた画素が変わらない | QEMU: check-ppc-display-guest、dispprb + fb_pixel.py、7450・7455 の両方 | PowerMac の DISPLAY_V1_WIDTH を 32 狭める (x=799 の画素が出ず実行時に赤) |
| A5 | PowerMac: 640 より右 (x=704〜767 など) だけの矩形が add_dirty_rect と全画面の present の両方で出る | 同上 | PowerMac の DISPLAY_V1_WIDTH を 640 に固定 (実行時に赤) |
| A6 | PowerMac: gfx_init_200 の後、面の寸法が 800x600 のまま (gfx2_screen_surface → gfx2_surface_desc)、gfx2_lock で影へ書いた画素が present_rect で表示に出る | 同上 (gfx_screen_info・gfx_get_framebuffer は 5b の後も不在なので使わない) | dispprb の期待でなくコード側: gfx_kapi_init_200 の束縛を欠如の答えに戻す (実行時に赤) |
| A7 | PowerMac の像が組め、7 本が通常の wrap で束縛、gfx の残り 12 本と gfx2 の 4 本は不在のまま、不在の組のヘッダ除外 (gfx_hal.h・palette.h) が崩れない | PPC のリンク (--no-undefined)、tools/ppc/test_kapi_binding.py (親設計 §4 の通常・欠如の両方) | 空実装 1 本を消す / 不在の集合に gfx_present を戻す |
| A8 | x86: 差が §3 の列挙の中だけ (実測の一覧を票に貼る) | build_compare | — |
| A9 | x86: 画面の振る舞いと順が変わらない (planar の flip・非 flip、PEGC の packed、ラスタ) | NP21/W: 既存の gfx の受入 (gdi_test・gfx200_test・起動のスプラッシュ)、host: test_sep5_gfx_init・test_boot_splash_native・test_gfx_attach・test_gfx_boot | 既存の変異が実行時に RED |
| A10 | 層の検査: gfx_present.c が機種・装置のヘッダを直に読まない | check-layers | gfx_present.c に `#include "pc98.h"` を足す |
| A11 | PowerMac: gfx_shutdown の後 (端末が戻った後) の present 6 本が端末の画素を変えない | QEMU: dispprb が shutdown → 6 本 → 端末の行の画素を照合、7450・7455 | display_provider_present_ok の `!pm_console_text_ready()` を外す |
| A12 | PowerMac: 所有者でないアプリの present 6 本が画素を変えない | QEMU: 所有者が描いたまま止まり、別のアプリが 6 本を呼ぶ (協調切替の中で成り立つ手順は実装者が link-sh で組む。組めなければ host の A1 の門の 2 通りで代え、未確認と報告) | 所有者の比較を外す |

### 4-2. 5c への申し送り

- gfx の残り 12 本、gfx2 の 4 本 (mode_set・flip・present・check_op)。
- gfx2_present: 同期の先が影で、表示へ届く道 (of_fb_copy) が無い (食い違い 7、レビューも同じ照合)。影 → 表示を誰が呼ぶか、失敗した時に面の dirty を残すかを決める。
- check_op: 答え (shim_check は NOSYS、共通の規則は条件により SW) が実際の gfx2_submit の結果と合うことを試験で確かめてから公開する。規則を変える案はユーザーへ。
- 計数: gfx_stats を公開する時に OF FB の commits・present_bytes の責務を決める (§2-5)。
- v1 の橋: gfx_v1_present_rect は v1_bridge_begin の戻り値を捨てる (x86 の今の形。5b では変えない)。

## 5. 確定 (PM 決定、ユーザー判断は不要)

1. PowerMac の gfx_init_200 は gfx_init と同じ実寸で公開する (x86 の packed の規則と同じ、コードの追加なし)。
2. v1 の present の幅は x86 を 640 のまま、PowerPC は選択モードの幅。
3. check_op: 5b は規則に触れず、公開もしない。x86 の答えは変わらない。

200 行の保証 (行の 2 度打ち)・x86 の幅の変更・check_op の公開の判定の規則の変更を採るなら、その時にユーザーに戻す。

## 6. 確かめていない物 ([V4])

- ビルド・build_compare・試験は回していない。§3 の差はコンパイラのインラインの見込みで、実測ではない。
- check_layers が platform_display_fallback.h 越しのマクロ 2 個を今の DISPLAY_FALLBACK_BACKEND と同じ扱いで通すかは、docs/design/tools/check_layers.md §8 を読んでいないので実装の前に確かめる。通らない時は関数の口に替える (x86 に call が増えるので §3 の列挙に足して報告)。
- §2-6 の 3 点 (今のアプリの番号の取得口、display_text_hide と pm_console_text_ready の関係、起動の端末が 6 本を呼ばないこと)。
- gfx_vram.c を直に組む・名前で抜く host 試験の全数。実装者が `grep -l "gfx_vram\|_present_dirty" tools/tests/*.py` で数える。
- build-selection の fixture の置き場と、dispprb のソースの置き場は読んでいない (レビューの指摘の名前のまま書いた)。

## 7. 反論

無し (9 件とも採った)。

## 確定 (2026-11-20、sol の第 2 回「条件付きで可」、PM が反映)
- C1 (P1 §2-6 の公開の条件): pm_console_text_ready() はターゲットとアダプタの世代の一致を返すだけで、gfx_shutdown はアプリの所有権を残し、端末の復帰で gfx_init がバックバッファを確保し直すので、ターゲットの取り直しに失敗すると (text_off で ready は偽のまま) 同じ所有者の present がまた許される。→ PowerPC の公開の入口では、**成功した公開の gfx_init / gfx_init_200 の所有者と表示のセッションの世代**を記録して照合し、**gfx_shutdown の開始の時にその許可を失効**させる。アプリ番号は appslot_cur()、公開の入口では既存の caller_access_get_user(&caller) と caller.app_id (gfx_core.c の今の使い方)。x86 の入口とスプラッシュ・OSRI の内部の呼び出しは門が定数 1 で通る。A11 は正常の復帰とターゲットの取り直しの失敗の両方を、影と表示の画素が違う位置で確かめる。
- C2 (P2 A4〜A6 の画素の入力): gfx2_screen_surface() の画面の alias の gfx2_lock() は GFXSYNC_DIRECT を要り、OF FB の同期先には read32 が無く成り立たない。寸法は gfx2_surface_desc で取り、画素の入力はゲストの観測用の経路 (受入の台本の画面の読み) で取る。
- C3 (P2 B2・B3): packed の移動の範囲は列の消去と return 1 を含む (gfx_vram.c の _present_dirty_packed の全体)。test_kapi_bounds.py のラスタの変異を新しい TU に追従、test_gfx_kernel_fb.py と test_unicode_surface.py のソースの一覧を B3 に加える。x86 の診断への分岐は今のまま。
- 取り込みの順: A (PowerPC の側の定義を先に) → B (共通の TU・x86 の側の移動・両像の fixture を同時に) → C (公開)。
- 起動の端末は gfx_init と gfx_console_present から後端の flip に進み、present の 6 本を呼ばない。
