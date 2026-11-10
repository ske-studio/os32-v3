/* ======================================================================== */
/*  GFX_HAL.H — グラフィクス HAL バックエンド表 (GUI v1.1, レーン H)         */
/*                                                                          */
/*  GUI (libos32gui / gshell) と WM はこの表と gfx_screen_info() だけを見て   */
/*  動き、機種 (9801 プレーン / PEGC 8bpp / Cirrus アクセラレータ) を知らない。 */
/*  各バックエンドは probe / init / present などを 1 枚の GfxBackend として   */
/*  実装し、GUI からはポート番号・VRAM アドレス・機種名を隠す。               */
/*                                                                          */
/*  設計: docs/decisions/gui/DESIGN.md §2 (B1〜B3), §6, §7 /                     */
/*        契約 docs/decisions/gui/API_CONTRACTS.md G5, G7, G8                    */
/* ======================================================================== */

#ifndef __GFX_HAL_H
#define __GFX_HAL_H

#include "types.h"             /* u8, u16, u32 */
#include "os32_kapi_shared.h"  /* GFX_ScreenInfo, GFX_FMT_*, GFX_CAP_* */
#include "gfx_mode.h"
#include "../gfx/gfx.h"        /* Existing GFX_HEIGHT_200 mode constant. */

/* ------------------------------------------------------------------------ */
/*  バックバッファの画素形式 (ソフトウェア系バックエンドの記述子 bb_format)    */
/*  ソフトウェアバックエンドは描画プリミティブを共通の CPU 実装へ委ね、       */
/*  「先頭・ピッチ・画素形式」だけを渡す (DESIGN §7-1)。                       */
/* ------------------------------------------------------------------------ */
#define GFX_BB_PLANAR4  0   /* 4 プレーン。bb_base=プレーン0, 各面 height*pitch */
#define GFX_BB_PACKED8  1   /* 1 バイト 1 ピクセル (PEGC / アクセラレータ CPU 転送) */

/* GF_FEAT4_DESIGN §2: HAL types; conversion to gf3_surface stays in GF. */
struct GfxCursorImage {
    const u32 *argb_pm;
    u32 bytes, pitch, width, height;
    i32 hot_x, hot_y;
};
struct GfxScanout {
    u32 reservation_id, offset, bytes, pitch, width, height, format;
};
/* offset is checked against reservation_id, never a user physical address.
 * GfxScanout.format uses GFX_FMT_* IDs from gfx_format.h. */

/* ------------------------------------------------------------------------ */
/*  バックエンド関数表 (DESIGN §7-1 の契約)。                                */
/*                                                                          */
/*  描画プリミティブ (fill_rect / blit) は「アクセラレータがあれば」の枠で、   */
/*  NULL のときは呼び出し側 (共有ライブラリ = libos32gfx) が CPU 共通実装へ    */
/*  フォールバックする。末尾の INDEX8 hooks は                            */
/*  GFX_ACCEL_HAL_DESIGN §3-1 に従う。                                     */
/* ------------------------------------------------------------------------ */
struct OsriImage;
struct OsriViewRun;

/* GFX_ACCEL_HAL_DESIGN §3-1/§3-7: INDEX8 only. */
#define GFX_POLICY_SHADOW 0
#define GFX_POLICY_DIRECT 1
#define GFX_ROP_COPY 0
#define GFX_ROP_XOR  1

typedef struct GfxDirectOps {
    int (*fill_rect)(int, int, int, int, u8);
    int (*blit)(int, int, int, int, int, int);
    int (*fill_rop)(int, int, int, int, u8, u8);
    int (*put)(int, int, int, int, const u8 *, u32, int);
    int (*mono)(int, int, int, int, const u8 *, u32, u8, int, int, int, int, int);
    int (*get)(int, int, int, int, u8 *, u32);
} GfxDirectOps;

typedef struct GfxBackend {
    const char *name;

    int  (*probe)(void);                /* 機種検出。1 = このバックエンドが使える */
    void (*init)(void);                 /* ハードウェア初期化 (PEGC/Cirrus)。NULL = 別 hw-init 不要 (9801)。
                                         * gfx_init が select_backend の直後に 1 回だけ呼ぶ (レビュー ⑤) */
    int  (*query)(GFX_ScreenInfo *info);/* 能力ビットと画面情報を埋める (副作用なし・冪等)。0 = 成功。
                                         * gfx_screen_info はこれを呼ぶ。init と分離してあるので
                                         * 情報取得でハードを再初期化しない (レビュー ⑤) */
    void (*shutdown)(void);             /* バックエンドのハードウェア終了処理 */

    void (*present_rect)(int x, int y, int w, int h); /* バックバッファ→表示面 */
    void (*set_palette)(int first, int count, const u8 *rgb); /* rgb は 3B/項目 */

    void (*enter)(void);  /* 表示出力の切替 (フルスクリーン GFX プログラム前後) */
    void (*leave)(void);

    /* 描画プリミティブ。NULL = CPU 共通実装へフォールバック (アクセラレータ用の枠) */
    int  (*fill_rect)(int x, int y, int w, int h, u8 color);
    int  (*blit)(int dx, int dy, int sx, int sy, int w, int h);

    /* バックバッファ記述子 (ソフトウェア系のみ。アクセラレータ系は bb_base=NULL) */
    u8  *bb_base;
    u32  bb_pitch;
    u8   bb_format;   /* GFX_BB_* */
    u32  bb_size;     /* バックバッファ全体のバイト数。PEGC / Cirrus は台帳の
                       * SURFACE から埋める (CLIENT lease の backing 範囲と
                       * 同じ情報源、TASK_T1_LEDGER §3-8、e11b2)。
                       * pitch×height から計算できない (9801 は 4 プレーン +
                       * 端数パディングで 128KB) ので明示的に持つ。
                       * **表示面を含めてはならない** — CPL=3 に見せてよいのは
                       * クライアント面だけ (契約 G4、レビュー #5 ②)。 */

    /* 起動時の下ごしらえ (gfx_prepare_backend が 1 回だけ呼ぶ)。NULL なら
     * 従来どおり init() → shutdown() で済ませる。
     * **表示のモードも同期も変えない** のが約束: probe と起動時の状態の
     * 記録だけを行う (窓の予約・写像と BB は起動時の ⑥ gfx_boot_reserve が
     * 済ませている、TASK_T1_LEDGER §3-8)。init/shutdown で済ませると、
     * CUI しか使わない起動でも 09A8h とテキスト GDC の SYNC を送り直す。
     * 実機 (Ra266 + 液晶) でテキストが 1 行 1 文字ずつ右へずれたのは、この
     * 送り直しが原因だという **仮説** (GFX=pc98 で消えたことからの推定。
     * この変更での実機確認はまだ。TASK_FDC_REALHW §9-1。候補と限界は
     * backend_pegc.c の pegc_prepare の注記)。終わって probe() が 0 を返したら
     * 9801 へ落ちる。 */
    void (*prepare)(void);

    const GfxMode *modes;
    u16 mode_count, default_mode;
    const GfxMode *selected_mode;

    /* Optional display hooks (GF_FEAT4_DESIGN §2); NULL means NOSYS. */
    int (*set_mode)(u32 id);
    int (*dpms)(u8 head, u32 state);
    int (*cursor_image)(u8 head, const struct GfxCursorImage *);
    int (*cursor_move)(u8 head, i32 x, i32 y);
    int (*cursor_show)(u8 head, int show);
    int (*vsync)(u8 head, u32 after, u32 deadline, u32 *seen);
    int (*page_flip)(u8 head, const struct GfxScanout *, int sync, u32 deadline);
    int (*gamma)(u8 head, const u16 rgb[3][256]);
    /* Synchronous OSRI presentation; NULL means NOSYS. Backend owns the
     * plan and hardware restoration, service owns image and borrowed BB. */
    int (*osri_show)(const struct OsriImage *, struct OsriViewRun *);
    /* INDEX8 hooks; only gfx_policy_reeval writes policy and active hooks.
     * key/bg: -1 = transparent, 0..255 = palette index. */
    u8 policy;
    int (*direct_ok)(void);
    int (*fill_rop)(int x, int y, int w, int h, u8 color, u8 rop);
    /* Completion drains WC writes. */
    int (*put)(int x, int y, int w, int h, const u8 *src, u32 pitch, int key);
    /* Optional; opaque only (bg >= 0). Transparent mono uses CPU expansion. */
    int (*mono)(int x, int y, int w, int h, const u8 *bits, u32 pitch,
                u8 fg, int bg, int cx, int cy, int cw, int ch);
    int (*get)(int x, int y, int w, int h, u8 *dst, u32 pitch);
    /* Filled once by backend prepare/init; NULL disables DIRECT. */
    const GfxDirectOps *direct_ops;
    int (*shadow_fill_rect)(int, int, int, int, u8);
    int (*shadow_blit)(int, int, int, int, int, int);
    const struct GfxBackendV2 *v2;
    const struct module_record *v2_mod;
} GfxBackend;

static inline const GfxMode *gfx_mode_pick(const GfxBackend *be, int legacy200)
{
    if (!be || !be->modes || !be->mode_count ||
        be->default_mode >= be->mode_count) return NULL;
    if (legacy200) {
        for (u16 i = 0; i < be->mode_count; ++i)
            if (be->modes[i].format == GFX_FMT_PLANAR4 &&
                be->modes[i].height == GFX_HEIGHT_200) return &be->modes[i];
    }
    return &be->modes[be->default_mode];
}

static inline const GfxMode *gfx_backend_mode(const GfxBackend *be)
{
    return be && be->selected_mode ? be->selected_mode : gfx_mode_pick(be, 0);
}

/* ------------------------------------------------------------------------ */
/*  カウンタ (契約 G7 / DESIGN §8)。                                        */
/*  NP21/W はバスウェイトを再現しないため性能は回数で見積もる。バックエンドが  */
/*  present / fill / blit / パレット設定の中で累積し、gfx_stats() が          */
/*  GFX_Stats へ写す。VRAM は読まない — 書き込みバイト数だけを数える。         */
/* ------------------------------------------------------------------------ */
typedef struct GfxCounters {
    u32 present_bytes;  /* present で表示面へ書いたバイト数 (機種に依らず正確) */
    u32 hw_ops;         /* 発行したエンジン操作数 (CPU バックエンドは 0) */
    u32 io_accesses;    /* I/O ポートアクセス数 (パレット OUT / ページ切替 等) */
    u32 commits;        /* present (commit) 回数 */
} GfxCounters;

/* gfx_core.c が実体を持つ (バックエンドは加算するだけ) */
extern GfxCounters gfx_counters;

/* 現在選択されているバックエンド。gfx_init() が probe 順に最初の 1 枚を選ぶ。
 * 静的初期化子で 9801 を指すので、gfx_init 前でも NULL にはならない。 */
extern GfxBackend *g_backend;

/* 9801 プレーンバックエンド (drivers/video/pc98/backend_pc98.c)。
 * H3 (Cirrus) はこの表にもう 1 枚足す。 */
extern GfxBackend gfx_backend_pc98;

/* 9821 PEGC 256 色バックエンド (drivers/video/pc98/backend_pegc.c, H2)。
 * **const ではない**: バックバッファは起動時の ⑥ が池から確保する (台帳の
 * SURFACE) から埋める。9801 も gfx_core の共通設定で CLIENT に結び付ける。
 *
 * **weak 宣言**: drivers/video/pc98/backend_pegc.c を build/kernel.mk の C_KERNEL に入れて
 * いないビルドではこのシンボルは 0 になり、バックエンド表の該当要素が NULL に
 * なって probe から外れる (= 9801 だけの従来どおりの動作)。表に強い参照を
 * 置くとファイルを足すまでリンクが通らなくなるため。 */
extern GfxBackend gfx_backend_pegc __attribute__((weak));

/* ROM (INT 18h AH=30h) で 400 ラインへ戻れなかったときの OS32 側の戻し
 * (drivers/video/pc98/backend_pegc.c、`v86 -g` だけが呼ぶ)。拡張モードを抜け、hsync31
 * (1 = 31kHz / 0 = 24kHz) の 400 ラインの SYNC を入れてグラフィック GDC を
 * 止める。backend_pegc.c を入れないビルドでは 0 (weak) — 呼ぶ側が確かめる。 */
void pegc_restore_text_sync(int hsync31) __attribute__((weak));

/* Cirrus GD54xx: core の表へ cirrus.mod が HAL callbacks を登録する。
 * 欠落時は probe=0。識別と永久窓の予約は core、probe/enable は graphics
 * 境界。bb_base は FE000000h のリニア窓内の CLIENT 面 (表示面は含まない)。
 * weak は backend を持たない host fixture のため維持する。 */
extern GfxBackend gfx_backend_cirrus __attribute__((weak));
extern GfxBackend gfx_backend_geforce __attribute__((weak));
extern GfxBackend gfx_backend_trident __attribute__((weak));
/* H1 NOSYS placeholders; H2 supplies the checked implementations. */
int gfx_hw_submit(const GFX_Cmd *cmds, u32 n, u32 gen, u32 *done);
int gfx_hw_get(int x, int y, int w, int h, u8 *dst, u32 pitch, u32 gen);

/* Config-only boot reservation; absent until the Trident core is linked. */
int trident_boot_reserve(void) __attribute__((weak));


/* ------------------------------------------------------------------------ */
/*  バックエンドの強制指定 (票 H2b)。                                        */
/*                                                                            */
/*  /etc/system.cfg の GFX= を kernel.c が読み、gfx_init より前に              */
/*  gfx_set_backend_pref() へ渡す。probe より先に効く。                       */
/*  目的: NP21/W は常に PEGC 相当なので、既定 (auto) のままでは 9801 プレーン  */
/*  経路をエミュレータで回帰試験できない。GFX=pc98 でそれを強制する。          */
/* ------------------------------------------------------------------------ */
#define GFX_PREF_AUTO   0   /* 既定: probe 順 (Cirrus → PEGC → 9801) */
#define GFX_PREF_PC98   1   /* 9801 プレーン強制 (probe を行わない) */
#define GFX_PREF_PEGC   2   /* PEGC 強制。probe が通らなければ 9801 へ落とす */
#define GFX_PREF_GEFORCE 4
#define GFX_PREF_TRIDENT 5   /* Explicit only; never part of auto probing. */
#define GFX_PREF_CIRRUS 3   /* Cirrus 強制。probe が通らなければ 9801 へ落とす */

/* The config reader supplies GUI's existing default (0). */
static inline int gfx_geforce_pref(int gui)
{ return gui == 1 ? GFX_PREF_PC98 : GFX_PREF_GEFORCE; }

/* 起動設定から読んだ希望バックエンドを覚える。gfx_init() より前に呼ぶこと
 * (probe の直前に参照される)。範囲外の値は GFX_PREF_AUTO 扱い。 */
void gfx_set_backend_pref(int pref);

/* 現在の希望値 (GFX_PREF_*)。既定は GFX_PREF_AUTO。 */
int  gfx_get_backend_pref(void);

/* Cirrus の実 probe が graphics 境界で試行済みか (成功・失敗とも記録)。 */
int gfx_cirrus_probed(void);

/* ver/About: internal backend identity, not a hardware detection result. */
#define GFX_VER_UNKNOWN 0
#define GFX_VER_PC98    1
#define GFX_VER_PEGC    2
#define GFX_VER_GEFORCE 4
#define GFX_VER_CIRRUS  3
#define GFX_VER_TRIDENT 5
/* Saved query only: active height while started, selected GUI target otherwise.
 * No probe, mode switch or allocation. All outputs required (INVAL if NULL);
 * otherwise failure leaves zeros/UNKNOWN and returns NOSYS or the query error. */
int gfx_ver_state(GFX_ScreenInfo *out, u32 *kind, u32 *active);

/* 起動時は Cirrus 登録前に master PD で PEGC を準備する。
 * Cirrus 未 probe の最初の GUI 境界でも呼ぶ。CUI 描画アプリは gfx_init が境界。
 * 窓・BB は gfx_boot_reserve が確保済み。表示はテキストへ戻す。 */
void gfx_prepare_backend(void);

/* 現在のバックエンドのバックバッファの物理範囲を返す (base は 4KB 境界)。
 * exec が CPL=3 アプリへ USER マップする範囲。= 台帳の **選択中 backend の
 * CLIENT の SURFACE** (TASK_T1_LEDGER §3-8)。⑥ より前・面の無い backend
 * では *size に 0 が入る。
 *   9801  : poolから確保した MEM_GFX_BB_SIZE (128KiB) の面
 *   PEGC  : ⑥ が池の CPL=0 子のアリーナ内の上端から確保した 300KB (主記憶)
 *   Cirrus: リニア窓の中の非表示面 300KB (カード VRAM, H3b、MMIO) */
void gfx_bb_phys_range(u32 *base, u32 *size);

/* ⑥ (TASK_T1_LEDGER §3-3・§3-8): 副作用のない識別 → 候補の窓の一括予約
 * (owner = DEVICE gfx) → 写像 → PEGC の BB の確保 → SURFACE の登録 → CPL=0
 * 子のアリーナの上端の凍結。起動時 (exec_init より前、live AS 0)、GFX= を
 * gfx_set_backend_pref() へ渡した後に 1 回だけ。probe は呼ばない。 */
void gfx_boot_reserve(void);
/* ⑨: GUI の開始 (gshell の exec の直前) に選択中 backend の CLIENT を
 * boot → gshell へ移し、ledger_selfcheck("gui")。2 回目以降は無操作。 */
void gfx_client_to_gshell(void);
/* backend の副作用のない識別 (⑥ と各 probe の最初が使う)。BIOS ワークと
 * 物理地図・ボードの表を読むだけ。weak: backend を入れないビルドでは 0。 */
int pegc_identify(void) __attribute__((weak));
int cirrus_identify(void) __attribute__((weak));


/* R7FIX2 boot-only stripes setting (internal, not KAPI). */
extern int gfx_diag_stripes;
extern int gfx_diag_altwin;

#endif /* __GFX_HAL_H */
