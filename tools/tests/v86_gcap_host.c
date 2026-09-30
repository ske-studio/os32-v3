/* ======================================================================== */
/*  v86_gcap_host.c — `v86 -g` の記録器と引数の判定のホスト試験              */
/*                                                                          */
/*  実物の kernel/v86_gcap_math.c を 1 行も写さずに #include する (変異は    */
/*  写しの上で。tools/tests/test_v86_gcap.py)。ポートにも V86 にも触らない   */
/*  部分だけなので、模型は ops (関数ポインタ) の記録係 1 つで足りる。        */
/*                                                                          */
/*  票: docs/tasks/realhw/TASK_PEGC480_REALHW.md §3 段 1                    */
/* ======================================================================== */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../../kernel/v86_gcap_math.c"

static int fails = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fails++; \
    } \
} while (0)

/* 記録の後ろに番兵を置いて、溢れの境界で書き越したら分かるようにする */
static struct {
    V86Gcap g;
    unsigned char guard[64];
} box;

static void fresh(void)
{
    memset(&box, 0xA5, sizeof(box));
    v86g_reset(&box.g);
}

static int guard_intact(void)
{
    unsigned int i;
    for (i = 0; i < sizeof(box.guard); i++) {
        if (box.guard[i] != 0xA5) return 0;
    }
    return 1;
}

/* ---- ops の記録係 ------------------------------------------------------ */
static unsigned int m_in8, m_in16, m_out8, m_out16;
static unsigned int m_last_port, m_last_val;

static unsigned int mk_in8(unsigned int p)  { m_in8++;  m_last_port = p; return 0x1234U; }
static unsigned int mk_in16(unsigned int p) { m_in16++; m_last_port = p; return 0x9ABCDEU; }
static void mk_out8(unsigned int p, unsigned int v)  { m_out8++;  m_last_port = p; m_last_val = v; }
static void mk_out16(unsigned int p, unsigned int v) { m_out16++; m_last_port = p; m_last_val = v; }
static const V86gIoOps mk_ops = { mk_in8, mk_in16, mk_out8, mk_out16 };

static void mk_clear(void)
{
    m_in8 = m_in16 = m_out8 = m_out16 = 0;
    m_last_port = m_last_val = 0;
}

/* ------------------------------------------------------------------------ */
static int case_port_list(void)
{
    static const unsigned int yes[] = {
        0x09A8, 0x09A0, 0x0060, 0x0062, 0x0064, 0x0068, 0x006A, 0x006C,
        0x006E, 0x0070, 0x0072, 0x0074, 0x0076, 0x0078, 0x007A,
        0x00A0, 0x00A2, 0x00A4, 0x00A6
    };
    static const unsigned int no[] = {
        0x0000, 0x0002, 0x005F, 0x0061, 0x0063, 0x0071, 0x0077, 0x007B,
        0x007C, 0x005E, 0x00A1, 0x00A8, 0x00AA, 0x00AE, 0x0188, 0x09A2,
        0x09A9, 0x09AA, 0x04A0, 0x0041, 0x0043, 0x0030
    };
    unsigned int i;
    for (i = 0; i < sizeof(yes) / sizeof(yes[0]); i++) {
        if (!v86g_port_passed(yes[i])) {
            printf("  port %04x should pass\n", yes[i]);
            fails++;
        }
    }
    for (i = 0; i < sizeof(no) / sizeof(no[0]); i++) {
        if (v86g_port_passed(no[i])) {
            printf("  port %04x must not pass\n", no[i]);
            fails++;
        }
    }
    return 0;
}

static int case_insn(void)
{
    unsigned int op;
    for (op = 0x6C; op <= 0x6F; op++) {
        CHECK(v86g_insn_check(1, op) == V86G_ABORT_INSOUTS);
        CHECK(v86g_insn_check(0, op) == V86G_ABORT_INSOUTS);
    }
    CHECK(v86g_insn_check(1, 0x6B) == V86G_ABORT_NONE);
    CHECK(v86g_insn_check(1, 0x70) == V86G_ABORT_NONE);
    /* 66h 付きのワード形 = 32 ビット */
    CHECK(v86g_insn_check(0, 0xE5) == V86G_ABORT_IO32);
    CHECK(v86g_insn_check(0, 0xE7) == V86G_ABORT_IO32);
    CHECK(v86g_insn_check(0, 0xED) == V86G_ABORT_IO32);
    CHECK(v86g_insn_check(0, 0xEF) == V86G_ABORT_IO32);
    /* 前置なしのワード形 = 16 ビット (通す) */
    CHECK(v86g_insn_check(1, 0xE5) == V86G_ABORT_NONE);
    CHECK(v86g_insn_check(1, 0xE7) == V86G_ABORT_NONE);
    CHECK(v86g_insn_check(1, 0xED) == V86G_ABORT_NONE);
    CHECK(v86g_insn_check(1, 0xEF) == V86G_ABORT_NONE);
    /* バイト形は 66h があっても 8 ビット */
    CHECK(v86g_insn_check(0, 0xE4) == V86G_ABORT_NONE);
    CHECK(v86g_insn_check(0, 0xE6) == V86G_ABORT_NONE);
    CHECK(v86g_insn_check(0, 0xEC) == V86G_ABORT_NONE);
    CHECK(v86g_insn_check(0, 0xEE) == V86G_ABORT_NONE);
    CHECK(v86g_insn_check(0, 0xCD) == V86G_ABORT_NONE);
    return 0;
}

/* 畳まない: 同じポート・同じ値・同じ CS:IP が続いても 1 件ずつ積む */
static int case_record_order(void)
{
    fresh();
    CHECK(v86g_note_out(&box.g, 0x62, 1, 0x0E, 0xFD80, 0x1000, 1, V86G_PH_SET480) == 0);
    v86g_note_in(&box.g, 0x60, 1, 0x04, 1);
    v86g_note_in(&box.g, 0x60, 1, 0x05, 1);
    CHECK(v86g_note_out(&box.g, 0x60, 1, 0x1234, 0xFD80, 0x1004, 1, V86G_PH_SET480) == 0);
    CHECK(v86g_note_out(&box.g, 0x60, 1, 0x1234, 0xFD80, 0x1004, 1, V86G_PH_SET480) == 0);
    CHECK(v86g_note_out(&box.g, 0xA2, 2, 0x12345, 0xFD80, 0x1008, 1, V86G_PH_RESTORE) == 0);
    CHECK(v86g_note_out(&box.g, 0x00, 1, 0x20, 0xFD81, 0x0002, 0, V86G_PH_READ31) == 0);

    CHECK(box.g.n_out == 5);
    CHECK(box.g.seq_next == 7);
    CHECK(box.g.out[0].seq == 0 && box.g.out[1].seq == 3 &&
          box.g.out[2].seq == 4 && box.g.out[3].seq == 5 &&
          box.g.out[4].seq == 6);
    CHECK(box.g.out[0].port == 0x62 && box.g.out[0].value == 0x0E &&
          box.g.out[0].width == 1);
    /* 幅 1 は下位 8 ビットだけ */
    CHECK(box.g.out[1].value == 0x34 && box.g.out[2].value == 0x34);
    /* 幅 2 は下位 16 ビット */
    CHECK(box.g.out[3].width == 2 && box.g.out[3].value == 0x2345);
    CHECK(box.g.out[1].cs == 0xFD80 && box.g.out[1].ip == 0x1004);
    CHECK(box.g.out[4].cs == 0xFD81 && box.g.out[4].ip == 0x0002);
    CHECK((box.g.out[0].flags & V86G_F_PASSED) != 0);
    CHECK((box.g.out[4].flags & V86G_F_PASSED) == 0);
    CHECK((box.g.out[0].flags >> V86G_F_PHASE_SHIFT) == V86G_PH_SET480);
    CHECK((box.g.out[3].flags >> V86G_F_PHASE_SHIFT) == V86G_PH_RESTORE);
    CHECK((box.g.out[4].flags >> V86G_F_PHASE_SHIFT) == V86G_PH_READ31);
    CHECK(box.g.overflow == 0);
    return 0;
}

static int case_overflow(void)
{
    unsigned int i;
    fresh();
    for (i = 0; i < V86G_OUT_MAX; i++) {
        CHECK(v86g_note_out(&box.g, 0x68, 1, i, 0x8A00, 0x8A, 1, 0) == 0);
    }
    CHECK(box.g.n_out == V86G_OUT_MAX);
    CHECK(box.g.overflow == 0);
    CHECK(v86g_note_out(&box.g, 0x68, 1, 0x77, 0x8A00, 0x8A, 1, 0) == -1);
    CHECK(v86g_note_out(&box.g, 0x68, 1, 0x78, 0x8A00, 0x8A, 1, 0) == -1);
    CHECK(box.g.overflow == 1);
    CHECK(box.g.n_out == V86G_OUT_MAX);
    CHECK(box.g.seq_next == V86G_OUT_MAX + 2);
    CHECK(box.g.out[V86G_OUT_MAX - 1].value == ((V86G_OUT_MAX - 1) & 0xFF));
    CHECK(guard_intact());
    return 0;
}

static int case_in_table(void)
{
    unsigned int i;
    fresh();
    v86g_note_in(&box.g, 0xA0, 1, 0x104, 1);        /* 幅 1 → 04 */
    v86g_note_in(&box.g, 0xA0, 1, 0x05, 1);
    v86g_note_in(&box.g, 0xA0, 2, 0xBEEF, 1);
    v86g_note_in(&box.g, 0xA0, 1, 0xFF, 0);         /* 同じポートでも仮想化は別 */
    CHECK(box.g.n_in == 2);
    CHECK(box.g.in_total == 4);
    CHECK(box.g.seq_next == 4);
    CHECK(box.g.in[0].port == 0xA0 && box.g.in[0].count == 3);
    CHECK(box.g.in[0].first == 0x04 && box.g.in[0].last == 0xBEEF);
    CHECK(box.g.in[0].widths == 3);
    CHECK((box.g.in[0].flags & V86G_F_PASSED) != 0);
    CHECK(box.g.in[1].count == 1 && (box.g.in[1].flags & V86G_F_PASSED) == 0);

    fresh();
    for (i = 0; i < V86G_IN_MAX + 3; i++) {
        v86g_note_in(&box.g, 0x100 + i, 1, i, 1);
    }
    CHECK(box.g.n_in == V86G_IN_MAX);
    CHECK(box.g.in_other == 3);
    CHECK(box.g.in_total == V86G_IN_MAX + 3);
    CHECK(guard_intact());
    return 0;
}

static int case_pass_ops(void)
{
    unsigned int v;
    unsigned int i;

    fresh();
    mk_clear();
    v86g_pass_out(&box.g, &mk_ops, 0xA2, 2, 0x12345, 0xFD80, 0x10, V86G_PH_SET480);
    CHECK(m_out16 == 1 && m_out8 == 0);
    CHECK(m_last_port == 0xA2 && m_last_val == 0x2345);
    v86g_pass_out(&box.g, &mk_ops, 0x62, 1, 0x1234, 0xFD80, 0x12, V86G_PH_SET480);
    CHECK(m_out8 == 1 && m_out16 == 1);
    CHECK(m_last_port == 0x62 && m_last_val == 0x34);
    CHECK(box.g.n_out == 2 && (box.g.out[0].flags & V86G_F_PASSED) &&
          box.g.out[0].width == 2);

    v = v86g_pass_in(&box.g, &mk_ops, 0x60, 1);
    CHECK(v == 0x34 && m_in8 == 1 && m_in16 == 0);
    v = v86g_pass_in(&box.g, &mk_ops, 0x6A, 2);
    CHECK(v == 0xBCDE && m_in16 == 1 && m_in8 == 1);
    CHECK(box.g.n_in == 2 && box.g.in[1].last == 0xBCDE &&
          (box.g.in[1].flags & V86G_F_PASSED));

    /* 溢れても実機へは通し続ける (ROM のモード切り替えを途中で崩さない) */
    fresh();
    mk_clear();
    for (i = 0; i < V86G_OUT_MAX + 5; i++) {
        v86g_pass_out(&box.g, &mk_ops, 0x68, 1, i, 0x8A00, 0x8A, 0);
    }
    CHECK(m_out8 == V86G_OUT_MAX + 5);
    CHECK(box.g.overflow == 1 && box.g.n_out == V86G_OUT_MAX);
    CHECK(guard_intact());
    return 0;
}

static int case_decide(void)
{
    unsigned int al, bh;

    /* bit2 並び (NP21/W): 400 ライン 24kHz / 31kHz、20 / 25 行 */
    CHECK(v86g_decide(0x08, 0x21, &al, &bh) == V86G_LAYOUT_BIT2);
    CHECK(al == 0x0C && bh == 0x32);
    CHECK(v86g_decide(0x0C, 0x21, &al, &bh) == V86G_LAYOUT_BIT2);
    CHECK(v86g_decide(0x08, 0x20, &al, &bh) == V86G_LAYOUT_BIT2);
    /* NP21/W の起動直後 (0597h 未設定 = 200 LOWER / 200 UPPER と返る) */
    CHECK(v86g_decide(0x08, 0x01, &al, &bh) == V86G_LAYOUT_BIT2);
    CHECK(al == 0x0C && bh == 0x32);
    CHECK(v86g_decide(0x08, 0x11, &al, &bh) == V86G_LAYOUT_BIT2);
    /* 今が 480 でも並びは決まる */
    CHECK(v86g_decide(0x0C, 0x32, &al, &bh) == V86G_LAYOUT_BIT2);
    /* bit3 並び (Bible 3-2) */
    CHECK(v86g_decide(0x00, 0x0C, &al, &bh) == V86G_LAYOUT_BIT3);
    CHECK(al == 0x08 && bh == 0x16);
    CHECK(v86g_decide(0x08, 0x0C, &al, &bh) == V86G_LAYOUT_BIT3);
    CHECK(v86g_decide(0x00, 0x04, &al, &bh) == V86G_LAYOUT_BIT3);
    CHECK(v86g_decide(0x00, 0x08, &al, &bh) == V86G_LAYOUT_BIT3);   /* 200 LOWER・25 行 */
    CHECK(v86g_decide(0x08, 0x16, &al, &bh) == V86G_LAYOUT_BIT3);   /* 480・30 行 */
    CHECK(v86g_decide(0x08, 0x0E, &al, &bh) == V86G_LAYOUT_BIT3);   /* 480・25 行 */
    /* 印のまま = 答えが無い */
    CHECK(v86g_decide(0xFF, 0xFF, &al, &bh) == V86G_DEC_NO31);
    CHECK(al == 0 && bh == 0);
    /* 両方の並びで正しい唯一の値 → 決めない (両方は試さない) */
    CHECK(v86g_decide(0x08, 0x00, &al, &bh) == V86G_LAYOUT_NONE);
    CHECK(al == 0 && bh == 0);
    /* どちらでも正しくない → 決めない */
    CHECK(v86g_decide(0xFF, 0x21, &al, &bh) == V86G_LAYOUT_NONE);
    CHECK(v86g_decide(0x08, 0xFF, &al, &bh) == V86G_LAYOUT_NONE);
    CHECK(v86g_decide(0x12, 0x34, &al, &bh) == V86G_LAYOUT_NONE);
    CHECK(al == 0 && bh == 0);
    CHECK(v86g_decide(0x08, 0x22, &al, &bh) == V86G_LAYOUT_NONE);   /* bit2 400 で 30 行 */
    CHECK(v86g_decide(0x08, 0x02, &al, &bh) == V86G_LAYOUT_BIT3);   /* bit2 では 200・30 行 (不正)、bit3 では 200 UPPER */
    CHECK(v86g_decide(0x00, 0x14, &al, &bh) == V86G_LAYOUT_NONE);   /* bit3 400 で 30 行 */
    CHECK(v86g_decide(0x08, 0x31, &al, &bh) == V86G_LAYOUT_NONE);   /* bit2 480 で 24kHz */
    CHECK(v86g_decide(0x00, 0x0E, &al, &bh) == V86G_LAYOUT_NONE);   /* bit3 480 で 24kHz */
    CHECK(v86g_decide(0x0C, 0x33, &al, &bh) == V86G_LAYOUT_NONE);   /* bit2 行数 3 */
    CHECK(v86g_decide(0x00, 0x18, &al, &bh) == V86G_LAYOUT_NONE);   /* bit3 行数 3 */
    CHECK(v86g_decide(0x04, 0x21, &al, &bh) == V86G_LAYOUT_NONE);
    CHECK(v86g_decide(0x0A, 0x21, &al, &bh) == V86G_LAYOUT_NONE);   /* bit1 は予約 */
    CHECK(v86g_decide(0x02, 0x0C, &al, &bh) == V86G_LAYOUT_NONE);
    CHECK(v86g_decide(0x05, 0x0C, &al, &bh) == V86G_LAYOUT_NONE);   /* bit3 並びで bit2 */
    CHECK(v86g_decide(0x08, 0x61, &al, &bh) == V86G_LAYOUT_NONE);
    CHECK(v86g_decide(0x00, 0x0D, &al, &bh) == V86G_LAYOUT_NONE);
    /* AL bit0 = ラスタスキャン ([US] memsys.md 0459h bit3「Function 30h,31h の
     * モード設定情報 bit 0」)。**実機 Ra266 の AH=31h は AX=310Dh BX=0100h**
     * (2026-09-29、44bd0fe の v86 -g)。bit2 並び + bit0 で、30h へは bit0 を
     * 保って渡す (ROM の値を勝手に落とさない)。 */
    CHECK(v86g_decide(0x0D, 0x01, &al, &bh) == V86G_LAYOUT_BIT2);
    CHECK(al == 0x0D && bh == 0x32);
    CHECK(v86g_decide(0x09, 0x21, &al, &bh) == V86G_LAYOUT_BIT2);
    CHECK(al == 0x0D && bh == 0x32);
    CHECK(v86g_decide(0x0C, 0x01, &al, &bh) == V86G_LAYOUT_BIT2);   /* bit0 無しは 0 のまま */
    CHECK(al == 0x0C && bh == 0x32);
    CHECK(v86g_mode_is_31k(V86G_LAYOUT_BIT2, 0x0D) == 1);
    CHECK(v86g_mode_is_31k(V86G_LAYOUT_BIT2, 0x09) == 0);
    /* bit3 並びでも同じ bit0 (資料は並びを区別しない) */
    CHECK(v86g_decide(0x01, 0x0C, &al, &bh) == V86G_LAYOUT_BIT3);
    CHECK(al == 0x09 && bh == 0x16);
    CHECK(v86g_decide(0x09, 0x0C, &al, &bh) == V86G_LAYOUT_BIT3);
    CHECK(al == 0x09 && bh == 0x16);
    /* 両方の並びで正しい値は bit0 が立っていても決めない */
    CHECK(v86g_decide(0x09, 0x00, &al, &bh) == V86G_LAYOUT_NONE);
    /* 上位ビットは見ない (AX / BX から切り出して渡す呼び手の保険) */
    CHECK(v86g_decide(0x3108, 0x2100 >> 8, &al, &bh) == V86G_LAYOUT_BIT2);
    CHECK(v86g_decide(0x310D, 0x0100 >> 8, &al, &bh) == V86G_LAYOUT_BIT2);
    CHECK(al == 0x0D && bh == 0x32);
    return 0;
}

static int case_mode_31k(void)
{
    CHECK(v86g_mode_is_31k(V86G_LAYOUT_BIT2, 0x0C) == 1);
    CHECK(v86g_mode_is_31k(V86G_LAYOUT_BIT2, 0x08) == 0);
    CHECK(v86g_mode_is_31k(V86G_LAYOUT_BIT3, 0x08) == 1);
    CHECK(v86g_mode_is_31k(V86G_LAYOUT_BIT3, 0x00) == 0);
    CHECK(v86g_mode_is_31k(V86G_LAYOUT_BIT3, 0x04) == 0);
    CHECK(v86g_mode_is_31k(V86G_LAYOUT_NONE, 0xFF) == 0);
    return 0;
}

/* ③ の終わり方 → ④ を呼ぶか / ④ の終わり方 → 戻し方 (代行レビュー P2-2) */
static int case_restore(void)
{
    /* ③ が最後まで走って断られた = 無変化 → ④ を呼ばない */
    CHECK(v86g_need_restore(1, 0x01) == 0);
    CHECK(v86g_need_restore(1, 0x00) == 0);
    CHECK(v86g_need_restore(1, 0x31) == 0);
    CHECK(v86g_need_restore(1, 0x0105) == 1);    /* 下位 8 ビットだけ見る */
    /* ③ が 05h → 戻す */
    CHECK(v86g_need_restore(1, 0x05) == 1);
    /* ③ が途中で終わった (何を変えたか分からない) → 戻す */
    CHECK(v86g_need_restore(0, 0x01) == 1);
    CHECK(v86g_need_restore(0, 0x05) == 1);
    CHECK(v86g_need_restore(0, 0x31) == 1);
    /* ④ */
    CHECK(v86g_restore_kind(1, 0x05) == V86G_RST_ROM);
    CHECK(v86g_restore_kind(1, 0x01) == V86G_RST_FALLBACK);
    CHECK(v86g_restore_kind(0, 0x05) == V86G_RST_FALLBACK);
    CHECK(v86g_restore_kind(0, 0x00) == V86G_RST_FALLBACK);
    CHECK(v86g_restore_kind(1, 0x1205) == V86G_RST_ROM);
    return 0;
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "port_list"))         case_port_list();
    else if (!strcmp(c, "insn"))         case_insn();
    else if (!strcmp(c, "record_order")) case_record_order();
    else if (!strcmp(c, "overflow"))     case_overflow();
    else if (!strcmp(c, "in_table"))     case_in_table();
    else if (!strcmp(c, "pass_ops"))     case_pass_ops();
    else if (!strcmp(c, "decide"))       case_decide();
    else if (!strcmp(c, "mode_31k"))     case_mode_31k();
    else if (!strcmp(c, "restore"))      case_restore();
    else {
        printf("unknown case %s\n", c);
        return 2;
    }
    return fails ? 1 : 0;
}
