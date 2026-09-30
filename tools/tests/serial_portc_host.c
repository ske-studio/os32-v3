/* ======================================================================== */
/*  SERIAL_PORTC_HOST.C — drivers/serial.c が 0035h を丸ごと書かないこと    */
/*                                                                          */
/*  実機 PC-9821Ra266 で rshell 中にビープが鳴り続けた件 (2026-09-24、      */
/*  docs/POLICY_DEBUG.md §4-59)。0035h (8255 ポート C) は RS-232C の割り込み */
/*  許可 bit0-2 と同じバイトに BUZ (bit3、0 = 鳴動)・MCHKEN・SHUT1・PSTBM・  */
/*  SHUT0 が同居する (docs/hw/undocumented/io_syste.md)。serial.c が 0x00 →  */
/*  0x01 を全体に書くたびに BUZ = 0 になっていた。                           */
/*                                                                          */
/*  実物の drivers/serial.c を #include し、ポート I/O だけを偽物にする      */
/*  (test_serial_portc.py が arch_io.h / platform_io.h を差し替える)。偽物は */
/*  8255 のポート C を模型として持ち、0037h の BSR (bit7 = 0) を 1 ビットの  */
/*  セット/リセットとして適用する。0035h への書き込みと 0037h のモード設定   */
/*  (bit7 = 1、実物ではポート C を 0 に戻す) は違反として数える。            */
/*                                                                          */
/*  IRQ4 の線も模型にする: 線 = RXRE (bit0) かつ受信データあり。立ち上がり */
/*  を数えて、ISR が汲み残したときに次の割り込みが作られるかを見る。       */
/*  0136h は FIFO 搭載機の真似もできる (bit6 が読むたびに反転)。             */
/*  kernel/sys.c の buz_on / buz_off は、test_serial_portc.py が関数本体を  */
/*  そのまま切り出した sys_buz.inc で通す。                                 */
/*                                                                          */
/*  NP21/W の sound/beepc.c も BUZ を bit3 = 0 で鳴らす (UNDOCUMENTED と同じ */
/*  向き)。エミュレータで鳴らなかった理由はここでは扱わない。               */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* ---- 偽のポート I/O (fake arch_io.h / platform_io.h から呼ばれる) ---- */
#define LOG_MAX 4096
struct io_rec { unsigned port; unsigned val; };
static struct io_rec g_log[LOG_MAX];
static int g_log_n;
static unsigned g_portc;           /* 8255 ポート C の模型 */
static int g_portc_full_writes;    /* 0035h への書き込み (違反) */
static int g_ctrl_mode_writes;     /* 0037h のモード設定 (違反) */
static int g_rx_pending;           /* 8251 に溜まっている受信バイト数 */
static int g_fifo_present;         /* 1 = 0136h が FIFO 搭載機として答える */
static unsigned g_iir_toggle;      /* 0136h bit6 (読むたびに反転) */
static int g_irq4_line;            /* IRQ4 の線 (RXRE かつ受信データあり) */
static int g_irq4_rises;           /* 線の立ち上がり回数 */

#define STS_RXRDY_BIT  0x02        /* 互換 0032h の RxRDY */
#define FSTS_RXRDY_BIT 0x04        /* FIFO 0132h の RxRDY */

static void update_line(void)
{
    int line = (g_portc & 0x01) && g_rx_pending > 0;
    if (line && !g_irq4_line) g_irq4_rises++;
    g_irq4_line = line;
}

void fake_outp(unsigned port, unsigned val)
{
    val &= 0xFF;
    if (g_log_n < LOG_MAX) {
        g_log[g_log_n].port = port;
        g_log[g_log_n].val = val;
        g_log_n++;
    }
    if (port == 0x35) {
        g_portc_full_writes++;
        g_portc = val;
    } else if (port == 0x37) {
        if (val & 0x80) {
            g_ctrl_mode_writes++;
            g_portc = 0;           /* 8255: モード設定は出力ラッチを 0 に */
        } else {
            unsigned bit = 1u << ((val >> 1) & 7);
            if (val & 1) g_portc |= bit;
            else         g_portc &= ~bit;
        }
    }
    update_line();
}

unsigned fake_inp(unsigned port)
{
    unsigned r = 0xFF;             /* 無いポート (FIFO 非搭載機の 0136h も) */
    if (port == 0x32)
        r = g_rx_pending ? STS_RXRDY_BIT : 0;
    else if (port == 0x132 && g_fifo_present)
        r = g_rx_pending ? FSTS_RXRDY_BIT : 0;
    else if (port == 0x30 || (port == 0x130 && g_fifo_present)) {
        if (g_rx_pending) g_rx_pending--;
        r = 'A';
    } else if (port == 0x136 && g_fifo_present) {
        g_iir_toggle ^= 0x40;      /* bit6 反転、bit5 = 0、bit0 = 要因なし */
        r = g_iir_toggle | 0x01;
    } else if (port == 0x35)
        r = g_portc;
    update_line();
    return r;
}

/* ---- serial.c が extern で引くもの ---- */
unsigned long sysclk_hz(void) { return 2457600UL; }
int sysclk_is_8mhz(void) { return 1; }
static int g_irq4_enabled;
void irq_enable(unsigned int irq) { if (irq == 4) g_irq4_enabled = 1; }
void irq_disable(unsigned int irq) { if (irq == 4) g_irq4_enabled = 0; }

#include "serial.c"          /* 変異の写しを引けるよう -I で探させる */
#include "../../drivers/serial_plan.c"
#include "sys_buz.inc"       /* kernel/sys.c の buz_on / buz_off (実物の写し) */

void cpu_delay_us(u32 us) { (void)us; }
volatile u32 tick_count;
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr; (void)fmt;
}

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* 実機 BIOS の後の想定: SHUT0/PSTBM/SHUT1/MCHKEN = 1、BUZ = 1 (停止)、
 * 割り込み許可 0。NP21/W の systemport_reset は 0xF9 (RXRE = 1)。 */
#define PORTC_BOOT      0xF8u
#define PORTC_HIGH_MASK 0xF8u      /* bit3-7: serial が触ってはいけない */

static void reset_model(unsigned portc)
{
    g_log_n = 0;
    g_portc = portc;
    g_portc_full_writes = 0;
    g_ctrl_mode_writes = 0;
    g_rx_pending = 0;
    g_fifo_present = 0;
    g_iir_toggle = 0;
    g_irq4_line = 0;
    g_irq4_rises = 0;
    s_fifo_probed = 0;             /* 搭載判定を案件ごとにやり直す */
    s_setup.has_fifo = 0;
}

static int count_writes(unsigned port, unsigned val)
{
    int i, n = 0;
    for (i = 0; i < g_log_n; i++)
        if (g_log[i].port == port && g_log[i].val == val) n++;
    return n;
}

/* ---- pc98.h の BSR 定数が UNDOCUMENTED の極性どおりか ---- */
static void bsr_polarity(void)
{
    /* io_syste.md I/O 0037h: 06h = 鳴動、07h = 停止。bit3 = 1 で停止。 */
    CHECK(BSR_BUZ_ON == 0x06);
    CHECK(BSR_BUZ_OFF == 0x07);
    reset_model(0x00);
    fake_outp(0x37, BSR_BUZ_OFF);
    CHECK((g_portc & 0x08) == 0x08);        /* 停止 = bit3 が 1 */
    fake_outp(0x37, BSR_BUZ_ON);
    CHECK((g_portc & 0x08) == 0x00);
    /* ほかの行は Bible と UNDOCUMENTED で一致 (偶数 = 0、奇数 = 1) */
    CHECK(BSR_RXRE_OFF == 0x00 && BSR_RXRE_ON == 0x01);
    CHECK(BSR_TXEE_OFF == 0x02 && BSR_TXEE_ON == 0x03);
    CHECK(BSR_TXRE_OFF == 0x04 && BSR_TXRE_ON == 0x05);
    CHECK(BSR_MCKEN_OFF == 0x08 && BSR_MCKEN_ON == 0x09);
    CHECK(BSR_SHUT1_CLR == 0x0A && BSR_SHUT1_SET == 0x0B);
    CHECK(BSR_PSTBM_OFF == 0x0C && BSR_PSTBM_ON == 0x0D);
    CHECK(BSR_SHUT0_CLR == 0x0E && BSR_SHUT0_SET == 0x0F);
}

/* ---- 初期化は 0035h を書かず、上位 5 ビットを保つ ---- */
static void init_bsr_only(void)
{
    reset_model(PORTC_BOOT);
    serial_init(9600UL);
    CHECK(g_portc_full_writes == 0);
    CHECK(g_ctrl_mode_writes == 0);
    CHECK((g_portc & PORTC_HIGH_MASK) == PORTC_BOOT);   /* BUZ 停止のまま */
    CHECK((g_portc & 0x07) == IEN_RX);                  /* 受信だけ許可 */
    CHECK(g_irq4_enabled == 1);
    /* 許可ビットが 0 で渡されたら、落とす BSR は 1 本も書かない
     * (NP21/W の sysp_o37 は bit2 への BSR を送信要求と読み IRQ4 を立てうる) */
    CHECK(count_writes(0x37, BSR_RXRE_OFF) == 0);
    CHECK(count_writes(0x37, BSR_TXEE_OFF) == 0);
    CHECK(count_writes(0x37, BSR_TXRE_OFF) == 0);
    CHECK(count_writes(0x37, BSR_TXRE_ON) == 0);
    CHECK(count_writes(0x37, BSR_TXEE_ON) == 0);
    CHECK(count_writes(0x37, BSR_BUZ_ON) == 0);         /* 鳴らさない */
    CHECK(count_writes(0x37, BSR_BUZ_OFF) >= 1);

    /* BIOS が BUZ = 0 (鳴動) で渡しても、初期化の後は止まっている */
    reset_model(PORTC_BOOT & ~0x08u);
    serial_init(9600UL);
    CHECK((g_portc & 0x08) == 0x08);
    CHECK((g_portc & 0xF0) == (PORTC_BOOT & 0xF0));
}

/* ---- ISR は BSR で RXRE を落として戻す (エッジを作り直す) ---- */
static void isr_edge_bsr(void)
{
    int i, off_at = -1, on_at = -1;

    reset_model(PORTC_BOOT);
    serial_init(9600UL);
    g_log_n = 0;
    g_rx_pending = 1;
    serial_irq_handler();
    CHECK(g_portc_full_writes == 0);
    CHECK(g_ctrl_mode_writes == 0);
    CHECK((g_portc & PORTC_HIGH_MASK) == PORTC_BOOT);
    CHECK((g_portc & 0x07) == IEN_RX);
    CHECK(serial_trygetchar() == 'A');

    for (i = 0; i < g_log_n; i++) {
        if (g_log[i].port != 0x37) continue;
        if (g_log[i].val == BSR_RXRE_OFF && off_at < 0) off_at = i;
        if (g_log[i].val == BSR_RXRE_ON && off_at >= 0) on_at = i;
    }
    CHECK(off_at >= 0);          /* 一度落とす (Bible §2-10 の意図) */
    CHECK(on_at > off_at);       /* そして戻す */
    /* TXRE (bit2) は許可していないので ISR で書かない — NP21/W の sysp_o37 は
     * bit2 への BSR を送信要求と読んで IRQ4 を立てうる */
    CHECK(count_writes(0x37, BSR_TXRE_OFF) == 0);
    CHECK(count_writes(0x37, BSR_TXRE_ON) == 0);
    CHECK(count_writes(0x37, BSR_TXEE_OFF) == 0);
    CHECK(count_writes(0x37, BSR_BUZ_ON) == 0);
}

/* ---- 実機の筋書き: 初期化して rshell で 200 バイト受ける ---- */
static void real_hw_story(void)
{
    int i;
    reset_model(PORTC_BOOT);
    serial_init(9600UL);
    for (i = 0; i < 200; i++) {
        g_rx_pending = 1;
        serial_irq_handler();
        (void)serial_trygetchar();
        /* 1 バイトごとに見る: 一瞬でも BUZ = 0 なら実機では鳴る */
        CHECK((g_portc & 0x08) == 0x08);
    }
    CHECK(g_portc_full_writes == 0);
    CHECK((g_portc & 0x80) == 0x80);   /* SHUT0 が残る (リセットで ITF が初期化する) */
    CHECK((g_portc & 0x20) == 0x20);   /* SHUT1 */
    CHECK((g_portc & PORTC_HIGH_MASK) == PORTC_BOOT);
}

/* ---- 許可ビットが 1 で渡されたら、落としてから受信だけ立てる ---- */
static void init_from_all_ones(void)
{
    int i, last_off = -1, first_on = -1;

    reset_model(0xFFu);            /* 許可 3 本とも 1、上位も全部 1 */
    serial_init(9600UL);
    CHECK(g_portc_full_writes == 0);
    CHECK(g_ctrl_mode_writes == 0);
    CHECK((g_portc & 0x07) == IEN_RX);
    CHECK((g_portc & PORTC_HIGH_MASK) == PORTC_HIGH_MASK);
    CHECK(count_writes(0x37, BSR_RXRE_OFF) == 1);
    CHECK(count_writes(0x37, BSR_TXEE_OFF) == 1);
    CHECK(count_writes(0x37, BSR_TXRE_OFF) == 1);
    CHECK(count_writes(0x37, BSR_TXRE_ON) == 0);
    CHECK(count_writes(0x37, BSR_TXEE_ON) == 0);
    /* 落とすのは切替の前、立てるのは切替の後 */
    for (i = 0; i < g_log_n; i++) {
        if (g_log[i].port != 0x37) continue;
        if (g_log[i].val == BSR_RXRE_OFF || g_log[i].val == BSR_TXRE_OFF ||
            g_log[i].val == BSR_TXEE_OFF) last_off = i;
        if (g_log[i].val == BSR_RXRE_ON && first_on < 0) first_on = i;
    }
    CHECK(last_off >= 0 && first_on > last_off);

    /* NP21/W の systemport_reset の値 (RXRE だけ 1) */
    reset_model(0xF9u);
    serial_init(9600UL);
    CHECK((g_portc & 0xFFu) == 0xF9u);
    CHECK(count_writes(0x37, BSR_RXRE_OFF) == 1);
    CHECK(count_writes(0x37, BSR_TXRE_OFF) == 0);
    CHECK(count_writes(0x37, BSR_TXEE_OFF) == 0);
}

/* ---- ISR が上限で抜けて汲み残したら、次の IRQ4 の立ち上がりが出る ---- */
static void isr_drain_limit_edge(void)
{
    int rises_before;

    reset_model(PORTC_BOOT);
    serial_init(9600UL);
    g_rx_pending = SER_IRQ_DRAIN_MAX + 5;
    update_line();
    CHECK(g_irq4_line == 1);
    rises_before = g_irq4_rises;
    serial_irq_handler();
    CHECK(g_rx_pending == 5);      /* 上限 128 で抜けて 5 バイト残る */
    CHECK(g_irq4_line == 1);
    /* 線が一度落ちて立ち直った = 8259 (エッジ) に次の割り込みが届く */
    CHECK(g_irq4_rises == rises_before + 1);
    CHECK((g_portc & PORTC_HIGH_MASK) == PORTC_BOOT);

    /* 残りを汲み切ったら線は落ちたまま (余分な立ち上がりを作らない) */
    rises_before = g_irq4_rises;
    serial_irq_handler();
    CHECK(g_rx_pending == 0);
    CHECK(g_irq4_line == 0);
    CHECK(g_irq4_rises == rises_before);
}

/* ---- V･FAST (FIFO) 経路でも 0035h を書かない ---- */
static void vfast_fifo_path(void)
{
    int rc, rises_before;

    reset_model(PORTC_BOOT);
    g_fifo_present = 1;
    rc = serial_init_vfast(115200UL);
    CHECK(rc == SER_INIT_VFAST);
    CHECK(s_setup.has_fifo == 1);
    CHECK(s_port_data == SER_FIFO_DATA);
    CHECK(g_portc_full_writes == 0);
    CHECK(g_ctrl_mode_writes == 0);
    CHECK((g_portc & PORTC_HIGH_MASK) == PORTC_BOOT);
    CHECK((g_portc & 0x07) == IEN_RX);
    CHECK(count_writes(0x37, BSR_TXRE_OFF) == 0);
    CHECK(count_writes(0x37, BSR_BUZ_ON) == 0);

    /* FIFO 1 杯 (16) で抜けて残りは次の割り込みへ */
    g_log_n = 0;
    g_rx_pending = SER_FIFO_DEPTH + 3;
    update_line();
    rises_before = g_irq4_rises;
    serial_irq_handler();
    CHECK(g_rx_pending == 3);
    CHECK(g_irq4_rises == rises_before + 1);
    CHECK(g_portc_full_writes == 0);
    CHECK((g_portc & PORTC_HIGH_MASK) == PORTC_BOOT);
    CHECK(count_writes(0x37, BSR_TXRE_OFF) == 0);
    CHECK(serial_trygetchar() == 'A');

    /* 互換へ戻しても同じ */
    serial_init(9600UL);
    CHECK(s_port_data == SER_DATA);
    CHECK(g_portc_full_writes == 0);
    CHECK((g_portc & PORTC_HIGH_MASK) == PORTC_BOOT);
    CHECK((g_portc & 0x07) == IEN_RX);
}

/* ---- kernel/sys.c の buz_on / buz_off が書く値 ---- */
static void sys_buz_values(void)
{
    reset_model(PORTC_BOOT);
    buz_off();
    CHECK(g_log_n == 1);
    CHECK(g_log[0].port == 0x37 && g_log[0].val == 0x07);
    CHECK((g_portc & 0x08) == 0x08);            /* 停止 */
    buz_on();
    CHECK(g_log_n == 2);
    CHECK(g_log[1].port == 0x37 && g_log[1].val == 0x06);
    CHECK((g_portc & 0x08) == 0x00);            /* 鳴動 */
    buz_off();
    CHECK((g_portc & 0xFFu) == PORTC_BOOT);     /* ほかのビットは触らない */
    CHECK(g_portc_full_writes == 0);
}

int main(int argc, char **argv)
{
    const char *only = (argc > 1) ? argv[1] : NULL;
#define RUN(f) do { if (!only || !strcmp(only, #f)) { int b = failed; f(); \
    printf("%s %s\n", failed == b ? "PASS" : "FAIL", #f); } } while (0)
    RUN(bsr_polarity);
    RUN(init_bsr_only);
    RUN(isr_edge_bsr);
    RUN(real_hw_story);
    RUN(init_from_all_ones);
    RUN(isr_drain_limit_edge);
    RUN(vfast_fifo_path);
    RUN(sys_buz_values);
    return failed ? 1 : 0;
}
