/* ======================================================================== */
/*  SERIAL_GATE_HOST.C — drivers/serial.c の SerialFS ゲート                 */
/*                                                                          */
/*  票 TASK_SERIAL_HOSTFS §1-v3「送受信のゲートは呼び口で分ける」。          */
/*  実物の drivers/serial.c を #include し、ポート I/O だけを偽物にする      */
/*  (test_serialfs.py が arch_io.h / platform_io.h を差し替える。           */
/*  tools/tests/serial_portc_host.c と同じ作り)。偽物は 8251 の送信 (0030h   */
/*  への書き込みを記録) と受信 (待ち行列から RxRDY) を持つ。                */
/*                                                                          */
/*  見ること:                                                               */
/*   - ゲート中の serial_putchar / serial_puts は線へ出ず保留リングへ        */
/*     (console の複写・KAPI・ime_dict・ISR の kprintf は全部ここを通る)     */
/*   - 保留リングは溢れたら**古い方**を捨てて数える                          */
/*   - ゲート中の serial_trygetchar / peekchar / has_data / getchar は無い   */
/*     = rshell と kbd.c にフレームが漏れない                               */
/*   - serial_gate_get は受信リングと、ISR が汲み残した FIFO の中を読む      */
/*   - ゲートを下ろすと受信リングを空にする (遅れた応答が rshell に入らない) */
/*   - ゲート中の serial_init / serial_init_vfast は断る                     */
/*   - serial_puts_polled (パニック) はゲートを通る                          */
/*   - ISR が OE / FE / PE を数える                                          */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TX_MAX 65536
static unsigned char g_tx[TX_MAX];
static int g_tx_n;
static unsigned char g_rxq[4096];
static int g_rxq_head, g_rxq_n;
static unsigned g_sts_err;          /* 次の 0032h 読みに乗せる誤りビット */
static int g_cmd_writes;            /* 0032h への書き込み (モード設定の検出) */

void fake_outp(unsigned port, unsigned val)
{
    val &= 0xFF;
    if (port == 0x30 || port == 0x130) {
        if (g_tx_n < TX_MAX) g_tx[g_tx_n++] = (unsigned char)val;
    } else if (port == 0x32) {
        g_cmd_writes++;
    }
}

unsigned fake_inp(unsigned port)
{
    unsigned r = 0xFF;
    if (port == 0x32) {
        /* TxRDY 常に 1、RxRDY = 待ち行列にある */
        r = 0x01 | (g_rxq_n ? 0x02 : 0) | g_sts_err;
        g_sts_err = 0;
    } else if (port == 0x30) {
        if (g_rxq_n) {
            r = g_rxq[g_rxq_head];
            g_rxq_head = (g_rxq_head + 1) % 4096;
            g_rxq_n--;
        } else {
            r = 0;
        }
    } else if (port == 0x136) {
        r = 0xFF;                      /* FIFO 非搭載 */
    } else if (port == 0x35) {
        r = 0xF8;
    }
    return r;
}

static void rx_push(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        g_rxq[(g_rxq_head + g_rxq_n) % 4096] = (unsigned char)s[i];
        g_rxq_n++;
    }
}

unsigned long sysclk_hz(void) { return 2457600UL; }
int sysclk_is_8mhz(void) { return 0; }
void irq_enable(unsigned int irq) { (void)irq; }
void irq_disable(unsigned int irq) { (void)irq; }

#include "serial.c"
#include "../../drivers/serial_plan.c"

void cpu_delay_us(u32 us) { (void)us; }
volatile u32 tick_count;
static int g_kprintf_n;
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr;
    /* 実物の console.c は rshell 中に serial_putchar で複写する。その経路を
     * 真似て、ゲート中の kprintf が線に出ないことを見る */
    g_kprintf_n++;
    while (*fmt) serial_putchar(*fmt++);
}

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)
static int failed;

static void reset(void)
{
    g_tx_n = 0;
    g_rxq_head = g_rxq_n = 0;
    g_sts_err = 0;
    g_cmd_writes = 0;
    serial_gate_set(0);
    serial_init(9600UL);
    g_tx_n = 0;
    g_cmd_writes = 0;
}

static void gate_tx(void)
{
    u8 buf[64];
    u32 n;

    reset();
    serial_putchar('a');
    CHECK(g_tx_n == 1 && g_tx[0] == 'a');          /* ゲート外は線へ */
    serial_gate_set(1);
    g_tx_n = 0;
    serial_putchar('x');
    serial_puts("yz");
    kprintf(0x07, "k\n");
    CHECK(g_tx_n == 0);                              /* 何も線に出ない */
    n = serial_hold_take(buf, sizeof(buf));
    CHECK(n == 5 && memcmp(buf, "xyzk\n", 5) == 0);
    /* SerialFS の口は線へ出る */
    CHECK(serial_gate_put((const u8 *)"\x05SF", 3) == SER_TX_OK);
    CHECK(g_tx_n == 3 && g_tx[0] == 0x05);
    /* パニックの口も通る */
    serial_puts_polled("P");
    CHECK(g_tx_n == 4 && g_tx[3] == 'P');
    serial_gate_set(0);
    g_tx_n = 0;
    serial_putchar('b');
    CHECK(g_tx_n == 1 && g_tx[0] == 'b');
}

static void hold_overflow(void)
{
    static u8 buf[SER_HOLD_SIZE + 16];
    u32 i, n;

    reset();
    serial_gate_set(1);
    for (i = 0; i < SER_HOLD_SIZE + 10; i++) serial_putchar((char)('A' + i % 26));
    CHECK(serial_hold_dropped() == 10);
    n = serial_hold_take(buf, sizeof(buf));
    CHECK(n == SER_HOLD_SIZE);
    /* **古い方を捨てる** — 残るのは最後の SER_HOLD_SIZE バイト */
    CHECK(buf[0] == (u8)('A' + 10 % 26));
    CHECK(buf[n - 1] == (u8)('A' + (SER_HOLD_SIZE + 9) % 26));
    /* 上げ直すと数も中身も 0 */
    serial_putchar('q');
    serial_gate_set(1);
    CHECK(serial_hold_dropped() == 0 && serial_hold_take(buf, 4) == 0);
    serial_gate_set(0);
}

static void gate_rx(void)
{
    int a, b, c;

    reset();
    rx_push("hi", 2);
    serial_irq_handler();                /* ISR が受信リングへ */
    CHECK(serial_has_data());
    serial_gate_set(1);                  /* 上げると前の会話の残りは捨てる */
    CHECK(!serial_has_data());
    rx_push("\x05SF", 3);
    serial_irq_handler();
    /* **rshell / kbd.c / KAPI からは見えない** */
    CHECK(serial_trygetchar() == -1);
    CHECK(serial_peekchar() == -1);
    CHECK(serial_has_data() == 0);
    CHECK(serial_getchar() == -1);       /* 待たずに無い */
    /* SerialFS の受信器は読める */
    a = serial_gate_get(); b = serial_gate_get(); c = serial_gate_get();
    CHECK(a == 0x05 && b == 'S' && c == 'F');
    CHECK(serial_gate_get() == -1);
    /* ISR を通らずに FIFO に残ったバイトも直接汲む */
    rx_push("Z", 1);
    CHECK(serial_gate_get() == 'Z');
    /* 下ろすときに受信リングを空にする: 遅れて届いた応答が rshell に入らない */
    rx_push("late", 4);
    serial_irq_handler();
    serial_gate_set(0);
    CHECK(serial_trygetchar() == -1);
    rx_push("ok", 2);
    serial_irq_handler();
    CHECK(serial_trygetchar() == 'o');

    /* **UART / FIFO に残ったまま** (ISR がまだ汲んでいない) のバイトも、
     * ゲートを下ろすときに読み捨てる (Codex 6)。下ろした直後の ISR が
     * rshell に渡さない */
    reset();
    serial_gate_set(1);
    rx_push("\x05SFtail", 6);          /* ISR を呼ばない = 8251 に残っている */
    serial_gate_set(0);
    serial_irq_handler();
    CHECK(serial_trygetchar() == -1);
    CHECK(g_rxq_n == 0);
    /* 上げるときも同じ (前の会話の残りを SerialFS の受信器に渡さない) */
    rx_push("old", 3);
    serial_gate_set(1);
    CHECK(serial_gate_get() == -1);
    serial_gate_set(0);
}

static void gate_init_refused(void)
{
    reset();
    serial_gate_set(1);
    g_cmd_writes = 0;
    serial_init(9600UL);                 /* どのクロックでもちょうど出る速度 */
    CHECK(g_cmd_writes == 0);            /* 8251 に触っていない */
    CHECK(serial_init_vfast(115200UL) == SER_INIT_REFUSED);
    CHECK(g_cmd_writes == 0);
    CHECK(g_tx_n == 0);                  /* 断りの kprintf も線に出ない */
    serial_gate_set(0);
    serial_init(9600UL);
    CHECK(g_cmd_writes > 0);
}

static void isr_counts(void)
{
    u32 oe = 0, fe = 0, pe = 0, ov = 0;
    reset();
    serial_diag_get(&oe, &fe, &pe, &ov);
    g_sts_err = STS_OE;
    rx_push("a", 1);
    serial_irq_handler();
    g_sts_err = STS_FE | STS_PE;
    rx_push("b", 1);
    serial_irq_handler();
    {
        u32 oe2, fe2, pe2;
        serial_diag_get(&oe2, &fe2, &pe2, (u32 *)0);
        CHECK(oe2 == oe + 1 && fe2 == fe + 1 && pe2 == pe + 1);
    }
    CHECK(serial_oe_mask(SER_MODE_VFAST) == SER_FSTS_OE);
    CHECK(serial_fe_mask(SER_MODE_COMPAT) == STS_FE);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "gate_tx")) gate_tx();
    else if (!strcmp(c, "hold_overflow")) hold_overflow();
    else if (!strcmp(c, "gate_rx")) gate_rx();
    else if (!strcmp(c, "gate_init_refused")) gate_init_refused();
    else if (!strcmp(c, "isr_counts")) isr_counts();
    else { fprintf(stderr, "unknown case %s\n", c); return 2; }
    return failed ? 1 : 0;
}
