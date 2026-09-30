/* ======================================================================== */
/*  GFX_HAL.H — グラフィクス HAL バックエンド表 (GUI v1.1, レーン H)         */
/*                                                                          */
/*  GUI (libos32gui / gshell) と WM はこの表と gfx_screen_info() だけを見て   */
/*  動き、機種 (9801 プレーン / PEGC 8bpp / Cirrus アクセラレータ) を知らない。 */
/*  各バックエンドは probe / init / present などを 1 枚の GfxBackend として   */
/*  実装し、GUI からはポート番号・VRAM アドレス・機種名を隠す。               */
/*                                                                          */
/*  設計: docs/tasks/gui/DESIGN.md §2 (B1〜B3), §6, §7 /                     */
/*        契約 docs/tasks/gui/API_CONTRACTS.md G5, G7, G8                    */
/* ======================================================================== */

#ifndef __GFX_HAL_H
#define __GFX_HAL_H

#include "types.h"             /* u8, u16, u32 */
#include "os32_kapi_shared.h"  /* GFX_ScreenInfo, GFX_FMT_*, GFX_CAP_* */

/* ------------------------------------------------------------------------ */
/*  バックバッファの画素形式 (ソフトウェア系バックエンドの記述子 bb_format)    */
/*  ソフトウェアバックエンドは描画プリミティブを共通の CPU 実装へ委ね、       */
/*  「先頭・ピッチ・画素形式」だけを渡す (DESIGN §7-1)。                       */
/* ------------------------------------------------------------------------ */
#define GFX_BB_PLANAR4  0   /* 4 プレーン。bb_base=プレーン0, 各面 height*pitch */
#define GFX_BB_PACKED8  1   /* 1 バイト 1 ピクセル (PEGC / アクセラレータ CPU 転送) */

/* ------------------------------------------------------------------------ */
/*  バックエンド関数表 (DESIGN §7-1 の契約)。                                */
/*                                                                          */
/*  描画プリミティブ (fill_rect / blit) は「アクセラレータがあれば」の枠で、   */
/*  NULL のときは呼び出し側 (共有ライブラリ = libos32gfx) が CPU 共通実装へ    */
/*  フォールバックする。文字とビットマップは v1 では CPU 共通実装に任せるため  */
/*  この表には置かない (契約 G2 の対応表)。                                   */
/* ------------------------------------------------------------------------ */
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
    u32  bb_size;     /* バックバッファ全体のバイト数。CPL=3 へ USER マップする
                       * 範囲でもある (exec が gfx_bb_phys_range() で取る)。
                       * pitch×height から計算できない (9801 は 4 プレーン +
                       * 端数パディングで 128KB) ので明示的に持つ。
                       * **表示面を含めてはならない** — CPL=3 に見せてよいのは
                       * クライアント面だけ (契約 G4、レビュー #5 ②)。 */

    /* 起動時の下ごしらえ (gfx_prepare_backend が 1 回だけ呼ぶ)。NULL なら
     * 従来どおり init() → shutdown() で済ませる。
     * **表示のモードも同期も変えない** のが約束: 予約 (sys_reserve_top)・
     * 窓の写像・起動時の状態の記録だけを行う。init/shutdown で済ませると、
     * CUI しか使わない起動でも 09A8h とテキスト GDC の SYNC を送り直す。
     * 実機 (Ra266 + 液晶) でテキストが 1 行 1 文字ずつ右へずれたのは、この
     * 送り直しが原因だという **仮説** (GFX=pc98 で消えたことからの推定。
     * この変更での実機確認はまだ。TASK_FDC_REALHW §9-1。候補と限界は
     * backend_pegc.c の pegc_prepare の注記)。終わって probe() が 0 を返したら
     * 9801 へ落ちる。 */
    void (*prepare)(void);
} GfxBackend;

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
extern const GfxBackend *g_backend;

/* 9801 プレーンバックエンド (gfx/backend_pc98.c)。
 * H3 (Cirrus) はこの表にもう 1 枚足す。 */
extern const GfxBackend gfx_backend_pc98;

/* 9821 PEGC 256 色バックエンド (gfx/backend_pegc.c, H2)。
 * **const ではない**: バックバッファは物理メモリ末尾から実行時に切り出すので、
 * bb_base / bb_size を init() が埋める (9801 はコンパイル時定数で済む)。
 *
 * **weak 宣言**: gfx/backend_pegc.c を build/kernel.mk の C_KERNEL に入れて
 * いないビルドではこのシンボルは 0 になり、バックエンド表の該当要素が NULL に
 * なって probe から外れる (= 9801 だけの従来どおりの動作)。表に強い参照を
 * 置くとファイルを足すまでリンクが通らなくなるため。 */
extern GfxBackend gfx_backend_pegc __attribute__((weak));

/* ROM (INT 18h AH=30h) で 400 ラインへ戻れなかったときの OS32 側の戻し
 * (gfx/backend_pegc.c、`v86 -g` だけが呼ぶ)。拡張モードを抜け、hsync31
 * (1 = 31kHz / 0 = 24kHz) の 400 ラインの SYNC を入れてグラフィック GDC を
 * 止める。backend_pegc.c を入れないビルドでは 0 (weak) — 呼ぶ側が確かめる。 */
void pegc_restore_text_sync(int hsync31) __attribute__((weak));

/* Cirrus GD54xx アクセラレータバックエンド (gfx/backend_cirrus.c, H3)。
 * PEGC と同じく **const ではない / weak 宣言**:
 *   - const でない: bb_base / bb_size を init() が実行時に埋める。
 *   - weak: gfx/backend_cirrus.c を build/kernel.mk に入れていないビルドでは
 *     0 になり、バックエンド表の要素が NULL になって probe から外れる。
 * **bb_base はカード VRAM の中を指す** (票 H3b, 2026-09-06)。クライアント面は
 * 主記憶ではなくカード VRAM の非表示領域にあり (契約 G4 / DESIGN §8)、
 * 0FABh レジスタ 02h の 2MB リニア窓 (物理 01000000h) 越しに CPU から線形に
 * 見える。H3 の時点では 32KB のバンク窓しか無く bb_base = NULL だった。 */
extern GfxBackend gfx_backend_cirrus __attribute__((weak));

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
#define GFX_PREF_CIRRUS 3   /* Cirrus 強制。probe が通らなければ 9801 へ落とす */

/* 起動設定から読んだ希望バックエンドを覚える。gfx_init() より前に呼ぶこと
 * (probe の直前に参照される)。範囲外の値は GFX_PREF_AUTO 扱い。 */
void gfx_set_backend_pref(int pref);

/* 現在の希望値 (GFX_PREF_*)。既定は GFX_PREF_AUTO。 */
int  gfx_get_backend_pref(void);

/* 希望値に従って実バックエンドを 1 回選択・初期化し、表示はテキストへ戻す。
 * probe の結果と描画面 (PEGC は主記憶の予約、Cirrus はリニア窓の写像) が
 * ここで確定する。**起動時のカーネル文脈から、アプリを 1 つも走らせる前に
 * 呼ぶこと**:
 *   - PEGC の probe は BIOS ワークエリアを読むが、アプリ PD には写像が無い
 *     (2026-09-09 の gdi_test #PF addr=0x045C)
 *   - PEGC のバックバッファは物理末尾側の予約なので、アプリが走っていると
 *     そのスタック/ヒープに阻まれて必ず失敗する
 *     (Cirrus はカード VRAM 内なので予約は不要。窓の写像は init で 1 回だけ) */
void gfx_prepare_backend(void);

/* 現在のバックエンドのバックバッファの物理範囲を返す (base は 4KB 境界)。
 * exec が CPL=3 アプリへ USER マップする範囲。まだ init() が済んでいない
 * (= 面が決まっていない) バックエンドでは *size に 0 が入る。
 *   9801  : 常に (MEM_GFX_BB_BASE, MEM_GFX_BB_SIZE) = 従来と同じ
 *   PEGC  : 物理末尾から切り出した 300KB (主記憶)
 *   Cirrus: リニア窓の中の非表示面 300KB (カード VRAM, H3b) */
void gfx_bb_phys_range(u32 *base, u32 *size);

#endif /* __GFX_HAL_H */
