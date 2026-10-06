/* ======================================================================== */
/*  V86_GCAP.C — `v86 -g`: 実機の ROM の INT 18h AH=31h/30h の I/O を記録    */
/*                                                                          */
/*  票 docs/tasks/realhw/TASK_PEGC480_REALHW.md §3 段 1。実機 PC-9821Ra266   */
/*  の PEGC 640x480 で桁がずれる件で、include/pegc.h の 480 ラインの SYNC が */
/*  NP21/W 由来のまま実機の ROM と同じか確かめられていない。uPD7220 の SYNC  */
/*  は書き込み専用で読み戻せないので、**ROM 自身に 480 ラインへ切り替えさせ、 */
/*  その間の OUT を記録する**。                                             */
/*                                                                          */
/*  段取り (V86G_MODE_ROM):                                                 */
/*    1. AH=31h — 今のモードを読む (AL/BH に印 FFh を入れておき、答えが無け */
/*       れば印のまま = V86G_ST_NO31)                                       */
/*    2. その値の bit の並びから AH=30h の引数を決める (v86g_decide)。       */
/*       決められなければ 30h を呼ばずに止める (V86G_ST_UNDECIDED)           */
/*    3. AH=30h → 640x480 / 31kHz / 30 行。戻り AH が 05h でなければ記録を   */
/*       捨てる (V86G_ST_REJECTED)                                          */
/*    4. AH=30h → 1 で読んだモード (**3 が失敗しても必ず通る**)。ここが     */
/*       05h で戻らなければ OS32 の表で戻す (pegc_restore_text_sync、       */
/*       1 で読んだ周波数 — 24kHz 固定にはしない)                            */
/*    5. OS32 の CUI を作り直す (グラフィック GDC 停止・表示可・テキスト     */
/*       GDC 開始・テキスト VRAM 30 行ぶんを採取前の内容へ・カーソル形状)     */
/*                                                                          */
/*  通常の V86 (v86_boot 等) の開始・終了は使わない。あちらは開始で画面を    */
/*  200 ライン・8 色へ変え (ROM が見る出発点が変わる)、終了で 400 ライン・   */
/*  16 色へ決め打ちで戻すが同期 (SYNC / 09A8h) は戻さない (v86_io.c)。       */
/*  採取中は v86_io.c の方針が v86_gcap_active() を見て切り替わる。          */
/*                                                                          */
/*  V86G_MODE_SELFTEST は記録器そのものの試験。決まった I/O 列を出す小さな    */
/*  ゲスト (kernel/v86_test16_gcap.asm) を走らせ、列・順序・幅・溢れ・       */
/*  INS/OUTS と 32 ビット IN/OUT での打ち切りを確かめる。捕まえたポートは     */
/*  実機へ通さない (模型の ops)。NP21/W の ROM は SYNC を内部状態へ直接      */
/*  書くので (bios18.c)、ROM の採取では記録器を試験できない (票 B4)。         */
/* ======================================================================== */

#include "memmap.h"
#include "v86_gcap.h"
#include "v86_gcap_math.h"
#include "v86.h"
#include "v86_io.h"
#include "v86_mem.h"
#include "pgalloc.h"
#include "exec.h"       /* exec_ledger_owner: バッキングの台帳の owner */
#include "v86_pic.h"
#include "kmalloc.h"
#include "kstring.h"
#include "io.h"
#include "pc98.h"
#include "console.h"
#include "gfx_hal.h"    /* pegc_restore_text_sync (weak) */
#include "gfx.h"        /* gfx_get_height / GFX_HEIGHT */

STATIC_ASSERT(sizeof(V86GcapOut) == 16, v86gcap_out_is_16_bytes);
STATIC_ASSERT(sizeof(V86GcapIn) == 12, v86gcap_in_is_12_bytes);
STATIC_ASSERT(sizeof(V86Gcap) == 76 + 12 * V86G_IN_MAX + 16 * V86G_OUT_MAX,
              v86gcap_layout);

/* #GP ハンドラが記録しているゲストの CS:IP (kernel/v86.c) */
extern u32 v86_gp_cs;
extern u32 v86_gp_ip;

/* ------------------------------------------------------------------------ */
/*  ゲストのコード。正本は kernel/v86_test16_gcap.asm                        */
/*    nasm -f bin kernel/v86_test16_gcap.asm -o /tmp/g.bin                  */
/*  で組んだバイト列そのもの (tools/tests/test_v86_gcap.py が照合する)。     */
/* ------------------------------------------------------------------------ */
static const u8 v86_gcap_code[] = {
    0xB8, 0x00, 0x8C, 0x8E, 0xD8, 0xA1, 0x00, 0x00, 0x8B, 0x1E, 0x02, 0x00,
    0x8B, 0x0E, 0x04, 0x00, 0x8B, 0x16, 0x06, 0x00, 0x1E, 0xCD, 0x18, 0x1F,
    0xA3, 0x08, 0x00, 0x89, 0x1E, 0x0A, 0x00, 0x89, 0x0E, 0x0C, 0x00, 0x89,
    0x16, 0x0E, 0x00, 0x9C, 0x58, 0xA3, 0x12, 0x00, 0xC7, 0x06, 0x10, 0x00,
    0xDE, 0xC0, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xB8, 0x00, 0x8C, 0x8E, 0xD8, 0xB0, 0x11, 0xE6,
    0x62, 0xBA, 0xA8, 0x09, 0xB0, 0x01, 0xEE, 0xB8, 0x33, 0x22, 0xE7, 0xA2,
    0xBA, 0x60, 0x00, 0xB8, 0x55, 0x44, 0xEF, 0xB0, 0x0B, 0xE6, 0x00, 0xE4,
    0xA0, 0xA2, 0x20, 0x00, 0xBA, 0x6A, 0x00, 0xED, 0xA3, 0x22, 0x00, 0xE6,
    0x5F, 0xC7, 0x06, 0x10, 0x00, 0xDE, 0xC0, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xB8, 0x00, 0x8C, 0x8E,
    0xD8, 0xB9, 0x01, 0x02, 0x88, 0xC8, 0xE6, 0x68, 0xE2, 0xFA, 0xC7, 0x06,
    0x10, 0x00, 0xDE, 0xC0, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xB8, 0x00, 0x8C, 0x8E, 0xD8, 0x31, 0xF6, 0xBA, 0x62, 0x00, 0x6E, 0xC7,
    0x06, 0x10, 0x00, 0xDE, 0xC0, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xB8, 0x00, 0x8C, 0x8E, 0xD8, 0xBA, 0xA0, 0x00,
    0x66, 0x31, 0xC0, 0x66, 0xEF, 0xC7, 0x06, 0x10, 0x00, 0xDE, 0xC0, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xB8, 0x00, 0x8C, 0x8E,
    0xD8, 0x31, 0xC0, 0x8E, 0xC0, 0x26, 0xC7, 0x06, 0xFC, 0x03, 0x60, 0x01,
    0x26, 0xC7, 0x06, 0xFE, 0x03, 0x00, 0x8A, 0xCD, 0xFF, 0xC7, 0x06, 0x10,
    0x00, 0xDE, 0xC0, 0xF4, 0x9C, 0x58, 0xA3, 0x24, 0x00, 0xEB, 0xFE, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4,
    0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4, 0xF4
};

#define GCAP_ENTRY_ROM      0x0000U    /* rom_call */
#define GCAP_ENTRY_SEQ      0x0040U    /* st_seq  */
#define GCAP_ENTRY_OVF      0x0080U    /* st_ovf  */
#define GCAP_ENTRY_OUTS     0x00C0U    /* st_outs */
#define GCAP_ENTRY_IO32     0x0100U    /* st_io32 */
#define GCAP_ENTRY_HANG     0x0140U    /* st_hang */

/* ゲストのデータ (0x8C00:0000 = V86_TEST_MAGIC_ADDR) の番地 (バイト) */
#define GD_IN_AX    0x00U
#define GD_IN_BX    0x02U
#define GD_IN_CX    0x04U
#define GD_IN_DX    0x06U
#define GD_OUT_AX   0x08U
#define GD_OUT_BX   0x0AU
#define GD_DONE     0x10U
#define GD_FLAGS    0x12U
#define GD_SEQ_IN8  0x20U
#define GD_SEQ_IN16 0x22U
#define GD_HANG_FL  0x24U
#define GD_SIZE     0x26U
#define GD_DONE_MARK 0xC0DEU

/* ROM の 1 呼び出しの時間上限 (100Hz の tick)。ROM の呼び出しは IF を立てたまま
 * 入る (v86_gcap_keep_if) のでタイマ IRQ が来て数えられる。ROM が自分で CLI
 * したまま回ったときだけは効かず、#GP のウォッチドッグ (V86_GP_LIMIT) 頼み。 */
#define GCAP_TICK_LIMIT     300U
/* 自己試験の st_hang の上限 (見切りが効くことだけ確かめるので短く) */
#define GCAP_HANG_TICKS     30U

/* 採取の前後で残すテキスト VRAM (480 ラインで見える 30 行ぶん) */
#define GCAP_TV_CELLS       (TVRAM_COLS * TVRAM_ROWS_30)

/* ------------------------------------------------------------------------ */
/*  採取中の状態 (#GP / I/O 経路の差し込み口が見る)                          */
/*  gcap_rec が NULL でなければ採取中。kernel.map から読めるよう static に    */
/*  しない (セッションが戻らないときに PM が emu_read_mem で覗く)。           */
/* ------------------------------------------------------------------------ */
V86Gcap *v86_gcap_rec = 0;
u32 v86_gcap_phase = 0;
static const V86gIoOps *gcap_ops = 0;
static V86Gcap *gcap_owned;
static u16 *gcap_tv;
u32 v86_gcap_alloc_count, v86_gcap_free_count;
static int gcap_keep_if = 0;    /* 1 = ゲストの INT n で IF を落とさない */

int v86_gcap_active(void)
{
    return v86_gcap_rec != 0;
}

int v86_gcap_keep_if(void)
{
    return v86_gcap_rec != 0 && gcap_keep_if;
}

int v86_gcap_insn_abort(int opsize16, u8 opcode)
{
    int kind;
    V86Gcap *g = v86_gcap_rec;

    if (!g) return 0;
    kind = v86g_insn_check(opsize16, opcode);
    if (kind == V86G_ABORT_NONE) return 0;
    if (g->abort_kind == V86G_ABORT_NONE) {
        g->abort_kind = (u32)kind;
        g->abort_cs = (u16)v86_gp_cs;
        g->abort_ip = (u16)v86_gp_ip;
    }
    return 1;
}

int v86_gcap_pass_in(u16 port, int size, u32 *value)
{
    V86Gcap *g = v86_gcap_rec;
    if (!g || !v86g_port_passed(port)) return 0;
    *value = v86g_pass_in(g, gcap_ops, port, size);
    return 1;
}

int v86_gcap_pass_out(u16 port, int size, u32 value)
{
    V86Gcap *g = v86_gcap_rec;
    if (!g || !v86g_port_passed(port)) return 0;
    v86g_pass_out(g, gcap_ops, port, size, (unsigned int)value,
                  (unsigned int)v86_gp_cs, (unsigned int)v86_gp_ip,
                  (unsigned int)v86_gcap_phase);
    return 1;
}

void v86_gcap_note_emul_in(u16 port, int size, u32 value)
{
    V86Gcap *g = v86_gcap_rec;
    if (!g || v86g_port_passed(port)) return;
    v86g_note_in(g, port, size, (unsigned int)value, 0);
}

void v86_gcap_note_emul_out(u16 port, int size, u32 value)
{
    V86Gcap *g = v86_gcap_rec;
    if (!g || v86g_port_passed(port)) return;
    (void)v86g_note_out(g, port, size, (unsigned int)value,
                        (unsigned int)v86_gp_cs, (unsigned int)v86_gp_ip, 0,
                        (unsigned int)v86_gcap_phase);
}

/* ------------------------------------------------------------------------ */
/*  実ポートの口 (ROM の採取) と模型の口 (自己試験)                          */
/* ------------------------------------------------------------------------ */
static unsigned int hw_in8(unsigned int port)   { return inp(port) & 0xFFU; }
static unsigned int hw_in16(unsigned int port)  { return inpw(port) & 0xFFFFU; }
static void hw_out8(unsigned int port, unsigned int v)  { outp(port, v & 0xFFU); }
static void hw_out16(unsigned int port, unsigned int v) { outpw(port, v & 0xFFFFU); }

static const V86gIoOps gcap_hw_ops = { hw_in8, hw_in16, hw_out8, hw_out16 };

/* 模型: IN は幅で違う決まった値、OUT は数えるだけ (実機に触らない)。 */
#define GCAP_DRY_IN8    0x5AU
#define GCAP_DRY_IN16   0xA55AU
static u32 dry_n_in8, dry_n_in16, dry_n_out8, dry_n_out16;
static unsigned int dry_in8(unsigned int port)  { (void)port; dry_n_in8++;  return GCAP_DRY_IN8; }
static unsigned int dry_in16(unsigned int port) { (void)port; dry_n_in16++; return GCAP_DRY_IN16; }
static void dry_out8(unsigned int port, unsigned int v)  { (void)port; (void)v; dry_n_out8++; }
static void dry_out16(unsigned int port, unsigned int v) { (void)port; (void)v; dry_n_out16++; }

static const V86gIoOps gcap_dry_ops = { dry_in8, dry_in16, dry_out8, dry_out16 };

/* ------------------------------------------------------------------------ */
/*  ゲストを 1 回走らせる                                                   */
/* ------------------------------------------------------------------------ */
static u16 gd_get(u32 off)
{
    return *(volatile u16 *)(V86_TEST_MAGIC_ADDR + off);
}

static void gd_set(u32 off, u16 v)
{
    *(volatile u16 *)(V86_TEST_MAGIC_ADDR + off) = v;
}

/* 戻り: v86_run の終了理由。*done = ゲストが最後の印まで来たか。
 * keep_if = 1 は ROM の呼び出しの形 — IF を立てて始め、ゲストの INT n でも
 * IF を落とさない (時間の見切りとホットキーを効かせる)。ticks は時間の上限。 */
static int gcap_run(V86Gcap *g, u32 entry, u32 phase, u16 ax, u16 bx,
                    u16 *oax, u16 *obx, int *done, int keep_if, u32 ticks)
{
    struct v86_context ctx;
    u32 off;
    int reason;

    for (off = 0; off < GD_SIZE; off += 2) {
        gd_set(off, 0);
    }
    gd_set(GD_IN_AX, ax);
    gd_set(GD_IN_BX, bx);

    ctx.eip    = entry;
    ctx.cs     = (u32)(V86_TEST_CODE_ADDR >> 4);
    ctx.eflags = V86_EFLAGS_INIT | (keep_if ? EFLAGS_IF : 0);
    ctx.esp    = 0x0FFE;
    ctx.ss     = (u32)(V86_TEST_STACK_ADDR >> 4);
    ctx.es     = ctx.ss;
    ctx.ds     = ctx.ss;
    ctx.fs     = ctx.ss;
    ctx.gs     = ctx.ss;

    v86_gcap_phase = phase;
    gcap_keep_if = keep_if;
    reason = v86_run_limit(&ctx, ticks);
    gcap_keep_if = 0;

    if (oax) *oax = gd_get(GD_OUT_AX);
    if (obx) *obx = gd_get(GD_OUT_BX);
    *done = (gd_get(GD_DONE) == GD_DONE_MARK);
    g->exit_reason = (u32)reason;
    return reason;
}

/* 1 回の実行の成否を status にする (0 = 正常に最後まで走った)。 */
static u32 gcap_run_status(const V86Gcap *g, int reason, int done)
{
    if (g->abort_kind != V86G_ABORT_NONE) return V86G_ST_ABORT;
    if (g->overflow) return V86G_ST_OVERFLOW;
    if (reason != V86_EXIT_HLT || !done) return V86G_ST_GUEST;
    return V86G_ST_OK;
}

/* 最後まで走ったか (打ち切り・暴走・時間切れでない)。溢れは数えない —
 * 記録が欠けただけで、ROM は最後まで動いている。 */
static int gcap_ran(const V86Gcap *g, int reason, int done)
{
    return g->abort_kind == V86G_ABORT_NONE &&
           reason == V86_EXIT_HLT && done;
}

/* ------------------------------------------------------------------------ */
/*  実機の ROM の採取                                                       */
/* ------------------------------------------------------------------------ */
static void gcap_rom(V86Gcap *g)
{
    volatile u32 ivt18 = 0x18U * 4U;    /* 定数畳み込みで -Warray-bounds を踏まない */
    u32 lin;
    u16 ax, bx;
    int done, reason, dec, set_ran;
    u32 st, st3;
    unsigned int al480, bh480;

    /* INT 18h はゲストの IVT (起動時に保存した実機の値) へ流れる。ROM を
     * 指していなければ (誰かが RAM へ付け替えた) 呼ばない。 */
    lin = ((u32)*(volatile u16 *)(ivt18 + 2U) << 4) +
          (u32)*(volatile u16 *)ivt18;
    if (lin < V86_EXTROM_START) {
        g->status = V86G_ST_NOVEC;
        return;
    }

    /* 1. AH=31h — 印 (FFh) を入れて呼ぶ */
    reason = gcap_run(g, GCAP_ENTRY_ROM, V86G_PH_READ31,
                      (u16)(0x3100U | V86G_SENTINEL_AL),
                      (u16)(V86G_SENTINEL_BH << 8), &ax, &bx, &done,
                      1, GCAP_TICK_LIMIT);
    g->r31_ax = ax;
    g->r31_bx = bx;
    st = gcap_run_status(g, reason, done);
    if (st != V86G_ST_OK) {
        g->status = st;
        return;
    }

    /* 2. 並びを決める。決められなければ 30h は呼ばない (両方は試さない) */
    dec = v86g_decide(ax & 0xFFU, (bx >> 8) & 0xFFU, &al480, &bh480);
    if (dec == V86G_DEC_NO31) {
        g->status = V86G_ST_NO31;
        return;
    }
    if (dec == V86G_LAYOUT_NONE) {
        g->status = V86G_ST_UNDECIDED;
        return;
    }
    g->layout = (u32)dec;

    /* 3. AH=30h → 640x480 */
    g->set_ax = (u16)(0x3000U | al480);
    g->set_bx = (u16)(bh480 << 8);
    reason = gcap_run(g, GCAP_ENTRY_ROM, V86G_PH_SET480, g->set_ax, g->set_bx,
                      &ax, &bx, &done, 1, GCAP_TICK_LIMIT);
    g->set_ret_ax = ax;
    g->set_ret_bx = bx;
    st = gcap_run_status(g, reason, done);
    set_ran = gcap_ran(g, reason, done);
    if (!v86g_need_restore(set_ran, (ax >> 8) & 0xFFU)) {
        /* ROM が引数を断った (最後まで走って AH≠05h) = 何も変えていない。
         * 戻しの AH=30h は呼ばない (それも断られると OS32 の表で同期を
         * 書き換えることになり、並びの誤判定と重なると 24kHz 機に 31kHz の
         * 同期を入れて画面を失う — 代行レビュー P2-2)。 */
        g->restore = V86G_RST_NONE;
        g->status = V86G_ST_REJECTED;
        return;
    }

    /* 4. AH=30h → 1 で読んだモード。3 が 05h を返したか、途中で終わった
     *    (何を変えたか分からない) ときに通る。3 の打ち切りの理由は退避して
     *    おき、4 は印を消してから走らせる (4 の打ち切りが 3 の理由を上書き
     *    しないように)。 */
    {
        u32 keep_kind = g->abort_kind;
        u16 keep_cs = g->abort_cs, keep_ip = g->abort_ip;

        g->rst_ax = (u16)(0x3000U | (g->r31_ax & 0xFFU));
        g->rst_bx = g->r31_bx;
        g->abort_kind = V86G_ABORT_NONE;
        reason = gcap_run(g, GCAP_ENTRY_ROM, V86G_PH_RESTORE,
                          g->rst_ax, g->rst_bx, &ax, &bx, &done,
                          1, GCAP_TICK_LIMIT);
        g->rst_ret_ax = ax;
        g->rst_ret_bx = bx;

        /* 戻れたかは「最後まで走って AH=05h」だけで決める。溢れは記録の
         * 欠けであって、ROM の戻しの成否とは関係ない。 */
        g->restore = v86g_restore_kind(gcap_ran(g, reason, done),
                                       (ax >> 8) & 0xFFU);

        /* status: 3 が失敗していれば 3 の理由 (打ち切りの印も 3 のもの)。
         * 3 が通って 4 で打ち切り・溢れなら記録が欠けているので 4 の理由。
         * 4 の戻り AH だけの失敗は restore = FALLBACK で表す。 */
        st3 = gcap_run_status(g, reason, done);
        if (st != V86G_ST_OK) {
            g->abort_kind = keep_kind;
            g->abort_cs = keep_cs;
            g->abort_ip = keep_ip;
            g->status = st;
        } else if (st3 == V86G_ST_ABORT || st3 == V86G_ST_OVERFLOW) {
            g->status = st3;
        } else {
            g->status = V86G_ST_OK;
        }
    }
}

/* ------------------------------------------------------------------------ */
/*  自己試験                                                                */
/* ------------------------------------------------------------------------ */
#define STF_SEQ_RUN      0x0001U   /* st_seq が最後まで走らなかった */
#define STF_SEQ_LIST     0x0002U   /* OUT 列 (順序・ポート・幅・値・CS:IP・通したか) */
#define STF_SEQ_INVAL    0x0004U   /* ゲストが受け取った IN の値 (幅) */
#define STF_SEQ_INTAB    0x0008U   /* IN の表 */
#define STF_SEQ_OPS      0x0010U   /* 模型の口を幅どおりに呼んだか */
#define STF_SEQ_SEQNO    0x0020U   /* seq が IN も数えて振られたか */
#define STF_OVF          0x0040U   /* 溢れ */
#define STF_OUTS         0x0080U   /* OUTSB で打ち切り */
#define STF_IO32         0x0100U   /* OUT DX,EAX で打ち切り */
#define STF_HANG         0x0200U   /* INT で入って回り続けるゲストを時間で見切れない / IF が落ちた */

static const struct {
    u16 port;
    u8  width;
    u8  passed;
    u16 value;
    u16 ip;
} gcap_expect[] = {
    { 0x0062, 1, 1, 0x0011, 0x0047 },
    { 0x09A8, 1, 1, 0x0001, 0x004E },
    { 0x00A2, 2, 1, 0x2233, 0x0052 },
    { 0x0060, 2, 1, 0x4455, 0x005A },
    { 0x0000, 1, 0, 0x000B, 0x005D },
};
#define GCAP_EXPECT_N   (sizeof(gcap_expect) / sizeof(gcap_expect[0]))
#define GCAP_OUTS_IP    0x00CAU
#define GCAP_IO32_IP    0x010BU

static void gcap_reset_keep(V86Gcap *g)
{
    u32 mode = g->mode, fail = g->selftest_fail;
    v86g_reset(g);
    g->mode = mode;
    g->selftest_fail = fail;
    dry_n_in8 = dry_n_in16 = dry_n_out8 = dry_n_out16 = 0;
}

static void gcap_selftest(V86Gcap *g)
{
    int reason, done;
    u32 i, fail = 0;
    const u16 cs = (u16)(V86_TEST_CODE_ADDR >> 4);

    /* 溢れ: 513 回の OUT。溢れても最後まで走り、512 件で止まる */
    gcap_reset_keep(g);
    reason = gcap_run(g, GCAP_ENTRY_OVF, V86G_PH_SELFTEST, 0, 0, 0, 0, &done,
                      0, GCAP_TICK_LIMIT);
    if (reason != V86_EXIT_HLT || !done || !g->overflow ||
        g->n_out != V86G_OUT_MAX || g->seq_next != V86G_OUT_MAX + 1U ||
        g->out[V86G_OUT_MAX - 1].port != 0x0068U ||
        g->out[V86G_OUT_MAX - 1].seq != V86G_OUT_MAX - 1U ||
        dry_n_out8 != V86G_OUT_MAX + 1U) {
        fail |= STF_OVF;
    }

    /* OUTSB: 打ち切り。印まで来ない、何も積まない */
    gcap_reset_keep(g);
    reason = gcap_run(g, GCAP_ENTRY_OUTS, V86G_PH_SELFTEST, 0, 0, 0, 0, &done,
                      0, GCAP_TICK_LIMIT);
    if (reason != V86_EXIT_UNKNOWN_OP || done ||
        g->abort_kind != V86G_ABORT_INSOUTS ||
        g->abort_cs != cs || g->abort_ip != GCAP_OUTS_IP || g->n_out != 0) {
        fail |= STF_OUTS;
    }

    /* 66h EFh: 打ち切り。幅 2 で通さない */
    gcap_reset_keep(g);
    reason = gcap_run(g, GCAP_ENTRY_IO32, V86G_PH_SELFTEST, 0, 0, 0, 0, &done,
                      0, GCAP_TICK_LIMIT);
    if (reason != V86_EXIT_UNKNOWN_OP || done ||
        g->abort_kind != V86G_ABORT_IO32 ||
        g->abort_cs != cs || g->abort_ip != GCAP_IO32_IP ||
        g->n_out != 0 || dry_n_out16 != 0) {
        fail |= STF_IO32;
    }

    /* 時間の見切り: ROM の呼び出しと同じ形 (IF を立てて始め、INT FFh で
     * 入る) で、I/O も #GP も出さずに回るゲスト。IF が落ちずに入り
     * (INT の先で見た FLAGS の IF)、タイマの見切りで戻ること。
     * 代行レビュー P2-1: IF を落としていた頃は見切りが死んでいた。
     * 下の `g->n_out != 0` は**仮想 PIC 全塞ぎの回帰検査も兼ねる** — IF=1 で
     * 0.3 秒回す間に実 IRQ (タイマ) がゲストへ反射されると、ゲストの IVT の
     * 先 (BIOS の ISR) が EOI などの OUT を出して記録に載る。 */
    gcap_reset_keep(g);
    reason = gcap_run(g, GCAP_ENTRY_HANG, V86G_PH_SELFTEST, 0, 0, 0, 0, &done,
                      1, GCAP_HANG_TICKS);
    if (reason != V86_EXIT_TIMEOUT || done ||
        !(gd_get(GD_HANG_FL) & (u16)EFLAGS_IF) || g->n_out != 0) {
        fail |= STF_HANG;
    }

    /* 決まった列 (最後に走らせて、この記録を呼び手へ返す) */
    gcap_reset_keep(g);
    reason = gcap_run(g, GCAP_ENTRY_SEQ, V86G_PH_SELFTEST, 0, 0, 0, 0, &done,
                      0, GCAP_TICK_LIMIT);
    if (reason != V86_EXIT_HLT || !done || g->abort_kind != V86G_ABORT_NONE) {
        fail |= STF_SEQ_RUN;
    }
    if (g->n_out != GCAP_EXPECT_N || g->overflow) {
        fail |= STF_SEQ_LIST;
    } else {
        for (i = 0; i < GCAP_EXPECT_N; i++) {
            const V86GcapOut *e = &g->out[i];
            if (e->port != gcap_expect[i].port ||
                e->width != gcap_expect[i].width ||
                e->value != gcap_expect[i].value ||
                e->cs != cs || e->ip != gcap_expect[i].ip ||
                ((e->flags & V86G_F_PASSED) ? 1 : 0) != gcap_expect[i].passed ||
                (e->flags >> V86G_F_PHASE_SHIFT) != V86G_PH_SELFTEST) {
                fail |= STF_SEQ_LIST;
            }
            if (e->seq != i) {
                fail |= STF_SEQ_SEQNO;
            }
        }
    }
    if (gd_get(GD_SEQ_IN8) != GCAP_DRY_IN8 ||
        gd_get(GD_SEQ_IN16) != GCAP_DRY_IN16) {
        fail |= STF_SEQ_INVAL;
    }
    if (g->in_total != 2 || g->n_in != 2 || g->in_other != 0 ||
        g->in[0].port != 0x00A0U || g->in[0].widths != 1 ||
        g->in[0].count != 1 || !(g->in[0].flags & V86G_F_PASSED) ||
        g->in[1].port != 0x006AU || g->in[1].widths != 2 ||
        g->in[1].count != 1 || g->in[1].last != GCAP_DRY_IN16) {
        fail |= STF_SEQ_INTAB;
    }
    if (dry_n_out8 != 2 || dry_n_out16 != 2 ||
        dry_n_in8 != 1 || dry_n_in16 != 1) {
        fail |= STF_SEQ_OPS;
    }
    if (g->seq_next != GCAP_EXPECT_N + 2U) {
        fail |= STF_SEQ_SEQNO;
    }

    g->selftest_fail = fail;
    g->status = fail ? V86G_ST_SELFTEST : V86G_ST_OK;
}

/* ------------------------------------------------------------------------ */
/*  CUI の作り直し                                                          */
/*                                                                          */
/*  同期 (SYNC / 09A8h / CRTC) は ROM の AH=30h が元のモードへ戻している     */
/*  (戻れなければ pegc_restore_text_sync)。ここは OS32 の CUI の前提だけ:    */
/*  グラフィック GDC は止める (CUI の定常。pc98_shutdown / pegc_shutdown と  */
/*  同じ)、表示可、テキスト GDC は開始 (NP21/W の AH=30h はテキストの STOP を */
/*  出す — bios18.c)、テキスト VRAM は採取前の 30 行ぶんへ、カーソル形状は   */
/*  OS32 のもの (DOS 由来の形を残さない、console_hw_cursor_enable)。          */
/* ------------------------------------------------------------------------ */
static void tv_save(u16 *buf)
{
    u32 i;
    volatile u16 *ch = (volatile u16 *)P2V_IO(TVRAM_CHAR_BASE);
    volatile u16 *at = (volatile u16 *)P2V_IO(TVRAM_ATTR_BASE);
    for (i = 0; i < GCAP_TV_CELLS; i++) {
        buf[i] = ch[i];
        buf[GCAP_TV_CELLS + i] = at[i];
    }
}

static void tv_restore(const u16 *buf)
{
    u32 i;
    volatile u16 *ch = (volatile u16 *)P2V_IO(TVRAM_CHAR_BASE);
    volatile u16 *at = (volatile u16 *)P2V_IO(TVRAM_ATTR_BASE);
    for (i = 0; i < GCAP_TV_CELLS; i++) {
        ch[i] = buf[i];
        at[i] = buf[GCAP_TV_CELLS + i];
    }
}

static void gcap_cui_rebuild(const u16 *tv)
{
    v86_cui_display_restore();
    tv_restore(tv);
    console_hw_cursor_enable();
}

/* Called only on the normal-stack release path, or after copying the result.
 * Exception teardown only disconnects the recorder; it must not free here. */
void v86_gcap_release(void)
{
    V86Gcap *g = gcap_owned;
    u16 *tv = gcap_tv;
    if (!g || !tv || kctx_irq_depth || kctx_exc_depth) return;
    v86_gcap_rec = 0;
    gcap_ops = 0;
    gcap_keep_if = 0;
    if (g->mode == V86G_MODE_ROM &&
        g->restore == V86G_RST_FALLBACK && pegc_restore_text_sync)
        pegc_restore_text_sync(v86g_mode_is_31k((int)g->layout, g->r31_ax & 0xFFU));
    if (g->mode == V86G_MODE_ROM || v86_session.aborting)
        gcap_cui_rebuild(tv);
    gcap_owned = 0;
    gcap_tv = 0;
    kfree(tv);
    kfree(g);
    v86_gcap_free_count += 2;
}

/* ------------------------------------------------------------------------ */
/*  KAPI v68 v86_gdc_capture                                                */
/* ------------------------------------------------------------------------ */
int v86_gdc_capture(int mode, V86Gcap *out)
{
    V86Gcap *g;
    u16 *tv;
    int rc;

    if (!out || (mode != V86G_MODE_ROM && mode != V86G_MODE_SELFTEST)) {
        return OS32_ERR_INVAL;
    }
    if (v86_gui_refuse()) return OS32_ERR_INVAL;        /* 票 T8-2 */
    if (v86_is_active() || v86_gcap_rec) return OS32_ERR_BUSY;
    /* 480 ライン (PEGC の GUI・全画面アプリ) の最中は採らない — 戻す先の
     * 「今のモード」が OS32 の描画中の状態になり、CUI の作り直しとも合わない。 */
    if (gfx_get_height() > GFX_HEIGHT) return OS32_ERR_BUSY;

    g = (V86Gcap *)kzalloc((u32)sizeof(V86Gcap));
    tv = (u16 *)kmalloc((u32)(GCAP_TV_CELLS * 2U * sizeof(u16)));
    if (!g || !tv) {
        if (g) kfree(g);
        if (tv) kfree(tv);
        return OS32_ERR_NOSPC;
    }
    gcap_owned = g;
    gcap_tv = tv;
    v86_gcap_alloc_count += 2;
    v86g_reset(g);
    g->mode = (u32)mode;
    tv_save(tv);

    gcap_ops = (mode == V86G_MODE_SELFTEST) ? &gcap_dry_ops : &gcap_hw_ops;
    v86_gcap_rec = g;           /* ここから v86_io.c の方針が採取用になる */

    if (v86_mem_setup(exec_ledger_owner()) != 0) {
        g->status = V86G_ST_SETUP;
    } else {
        u32 i;
        /* ゲストの PIC は全部塞ぐ。実 IRQ を ROM の割り込みハンドラへ反射
         * すると、その I/O (EOI 等) が記録に混ざる。ROM が自分で開けるなら
         * それは ROM の動作として記録される。 */
        v86_pic_set_imr(0, 0xFF);
        v86_pic_set_imr(1, 0xFF);
        for (i = 0; i < sizeof(v86_gcap_code); i++) {
            ((volatile u8 *)V86_TEST_CODE_ADDR)[i] = v86_gcap_code[i];
        }
        if (mode == V86G_MODE_SELFTEST) {
            gcap_selftest(g);
        } else {
            gcap_rom(g);
        }
        v86_mem_teardown();     /* 採取中なので v86_io_reset_policy は画面に触らない */
    }

    v86_gcap_rec = 0;
    gcap_ops = 0;

    if (mode == V86G_MODE_ROM) {
        /* 失敗した採取は列を出さない (溢れ・05h 以外・打ち切り・暴走)。
         * 回数と理由は残す。 */
        if (g->status != V86G_ST_OK) {
            g->n_out = 0;
            g->n_in = 0;
        }
    }

    kmemcpy(out, g, (u32)sizeof(*g));
    rc = (int)g->status;
    v86_gcap_release();
    return rc;
}
