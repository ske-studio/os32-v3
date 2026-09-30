/* ======================================================================== */
/*  IDT.C — IDT構築 / PIC初期化 / PIT設定                                  */
/*                                                                          */
/*  PC-98固有のポートアドレスを使用                                         */
/*  出典: FreeBSD sys/x86/isa/atpic.c, PC9800Bible                         */
/* ======================================================================== */

#include "idt.h"
#include "io.h"
#include "sysclk.h"   /* 0000:0501h から判定したシステムクロック */
#include "memmap.h"   /* PIT_HZ */

/* ======================================================================== */
/*  IDT テーブル (256エントリ, 各8バイト = 2048バイト)                      */
/* ======================================================================== */
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;

/* フレームカウンタ (タイマ割り込みで更新) */
volatile u32 tick_count = 0;

/* ======================================================================== */
/*  外部ASMスタブ (isr_stub.asm で定義)                                     */
/* ======================================================================== */

/* CPU例外ハンドラ (0-31) — 全ベクタに専用スタブを用意する */
extern void isr_stub_0(void);     /* #DE ゼロ除算 */
extern void isr_stub_1(void);     /* #DB デバッグ (V86シングルステップ) */
extern void isr_stub_2(void);     /* NMI */
extern void isr_stub_3(void);     /* #BP ブレークポイント */
extern void isr_stub_4(void);     /* #OF オーバーフロー */
extern void isr_stub_5(void);     /* #BR BOUND範囲外 */
extern void isr_stub_6(void);     /* #UD 未定義命令 */
extern void isr_stub_7(void);     /* #NM コプロセッサ不在 */
extern void isr_stub_8(void);     /* #DF ダブルフォルト */
extern void isr_stub_9(void);     /* コプロセッサセグメントオーバーラン */
extern void isr_stub_10(void);    /* #TS 無効TSS */
extern void isr_stub_11(void);    /* #NP セグメント不在 */
extern void isr_stub_12(void);    /* #SS スタックフォルト */
extern void isr_stub_13(void);    /* #GP 一般保護例外 */
extern void isr_stub_14(void);    /* #PF ページフォルト */
extern void isr_stub_15(void);    /* 予約 */
extern void isr_stub_16(void);    /* #MF x87 FPUエラー */
extern void isr_stub_17(void);    /* #AC アライメントチェック */
extern void isr_stub_18(void);    /* #MC マシンチェック */
extern void isr_stub_19(void);    /* #XM SIMD FPU例外 */
extern void isr_stub_20(void);    /* 予約 (20-31) */
extern void isr_stub_21(void);
extern void isr_stub_22(void);
extern void isr_stub_23(void);
extern void isr_stub_24(void);
extern void isr_stub_25(void);
extern void isr_stub_26(void);
extern void isr_stub_27(void);
extern void isr_stub_28(void);
extern void isr_stub_29(void);
extern void isr_stub_30(void);
extern void isr_stub_31(void);

/* IRQハンドラ */
extern void irq_stub_0(void);     /* IRQ0: タイマ (INT 0x20) */
extern void irq_stub_1(void);     /* IRQ1: キーボード (INT 0x21) */
extern void irq_stub_4(void);     /* IRQ4: RS-232C (INT 0x24) */
extern void irq_stub_7(void);     /* IRQ7: スプリアス (INT 0x27) */
extern void irq_stub_11(void);    /* IRQ11: FDD (INT 0x2B) */
extern void irq_stub_2(void);     /* IRQ2:  VSYNC (INT 0x22) — V86 ゲスト専用 */
extern void irq_stub_12(void);    /* IRQ12: サウンドボード (INT 0x2C) */
extern void irq_stub_13(void);    /* IRQ13: マウス (INT 0x2D) */

/* 動的 IRQ の共通スタブ (票 TASK_HAL_WIRING §1-1)。
 * C 側 irq_dispatch() が kernel/irq.c の表を引いて登録者を呼び、
 * EOI は irq_finish() の 1 経路だけが送る。登録が 0 件でも EOI は出る
 * (送らないと PIC の ISR ビットが立ったままになり同順位以下が止まる)。 */
extern void irq_stub_common_3(void);
extern void irq_stub_common_5(void);
extern void irq_stub_common_6(void);
extern void irq_stub_common_8(void);
extern void irq_stub_common_9(void);
extern void irq_stub_common_10(void);
extern void irq_stub_common_14(void);
extern void irq_stub_common_15(void);

/* デフォルトハンドラ (ベクタ 0x30 以降のみ) */
extern void isr_stub_default(void);

/* リング3 システムコール入口 (kernel/ring3_entry.asm, v2 M1/M2)。
 * ベクタ 0x80, ゲート DPL=3 で CPL=3 から呼べる。M1 では sys_exit のみ。 */
extern void int80_stub(void);
#define RING3_SYSCALL_VECTOR 0x80

/* ======================================================================== */
/*  idt_set_gate — IDTエントリを設定                                        */
/* ======================================================================== */
static void idt_set_gate(int num, void (*handler)(void), u8 type_attr)
{
    u32 addr = (u32)handler;
    idt[num].offset_low  = (u16)(addr & 0xFFFF);
    idt[num].selector    = KERNEL_CS;
    idt[num].zero        = 0;
    idt[num].type_attr   = type_attr;
    idt[num].offset_high = (u16)((addr >> 16) & 0xFFFF);
}

/* ======================================================================== */
/*  idt_init — IDTを構築してLIDTでロード                                    */
/* ======================================================================== */
void idt_init(void)
{
    int i;

    /* 全エントリをデフォルトハンドラで埋める */
    for (i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, isr_stub_default, IDT_ATTR_INT_GATE32);
    }

    /* CPU例外ハンドラ — 0-31 全ベクタを専用スタブで受ける */
    {
        static void (*const exc_stubs[32])(void) = {
            isr_stub_0,  isr_stub_1,  isr_stub_2,  isr_stub_3,
            isr_stub_4,  isr_stub_5,  isr_stub_6,  isr_stub_7,
            isr_stub_8,  isr_stub_9,  isr_stub_10, isr_stub_11,
            isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
            isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
            isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
            isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
            isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
        };
        for (i = 0; i < 32; i++) {
            idt_set_gate(i, exc_stubs[i], IDT_ATTR_INT_GATE32);
        }
    }

    /* IRQハンドラ (PIC再マッピング後) */
    idt_set_gate(0x20, irq_stub_0, IDT_ATTR_INT_GATE32);  /* IRQ0: タイマ */
    idt_set_gate(0x21, irq_stub_1, IDT_ATTR_INT_GATE32);  /* IRQ1: キーボード */
    idt_set_gate(0x24, irq_stub_4, IDT_ATTR_INT_GATE32);  /* IRQ4: RS-232C */
    idt_set_gate(0x27, irq_stub_7, IDT_ATTR_INT_GATE32);  /* IRQ7: スプリアス対策 */
    idt_set_gate(0x2B, irq_stub_11, IDT_ATTR_INT_GATE32); /* IRQ11: FDD */
    idt_set_gate(0x22, irq_stub_2,  IDT_ATTR_INT_GATE32); /* IRQ2:  VSYNC (V86ゲスト用) */
    idt_set_gate(0x2C, irq_stub_12, IDT_ATTR_INT_GATE32); /* IRQ12: サウンド (V86ゲスト用) */
    idt_set_gate(0x2D, irq_stub_13, IDT_ATTR_INT_GATE32); /* IRQ13: マウス */

    /* 動的登録を受ける IRQ (3/5/6/8/9/10/14/15) は共通スタブで受ける。
     * 登録が 0 件のあいだの挙動は従来の irq_stub_unexp_* と同じ
     * (EOI + 初回だけ診断) で、違うのは irq_register した driver が
     * 居れば呼ばれること。固定スタブの線はここに入れない。 */
    idt_set_gate(0x23, irq_stub_common_3,  IDT_ATTR_INT_GATE32);
    idt_set_gate(0x25, irq_stub_common_5,  IDT_ATTR_INT_GATE32);
    idt_set_gate(0x26, irq_stub_common_6,  IDT_ATTR_INT_GATE32);
    idt_set_gate(0x28, irq_stub_common_8,  IDT_ATTR_INT_GATE32);
    idt_set_gate(0x29, irq_stub_common_9,  IDT_ATTR_INT_GATE32);
    idt_set_gate(0x2A, irq_stub_common_10, IDT_ATTR_INT_GATE32);
    idt_set_gate(0x2E, irq_stub_common_14, IDT_ATTR_INT_GATE32);
    idt_set_gate(0x2F, irq_stub_common_15, IDT_ATTR_INT_GATE32);

    /* リング3 システムコール (int 0x80): ゲート DPL=3 で CPL=3 から呼べる。
     * 他の例外/IRQ ゲートは DPL=0 のまま (ユーザから直接呼べない, C4)。 */
    idt_set_gate(RING3_SYSCALL_VECTOR, int80_stub, IDT_ATTR_INT_GATE32_DPL3);

    /* IDTRをロード */
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (u32)&idt;
    _lidt(&idtp);
}

/* ======================================================================== */
/*  idt_register_irq — ハード IRQ のスタブを差し替える (ドライバの起動時登録)  */
/*  LAN (IRQ3/5/6) のように使う IRQ が設定で決まるものに使う。呼び出し側は     */
/*  デバイス初期化後に irq_enable() すること。                               */
/* ======================================================================== */
void idt_register_irq(unsigned int irq, void (*stub)(void))
{
    if (irq >= 16 || !stub) return;
    idt_set_gate(IRQ_TO_INT(irq), stub, IDT_ATTR_INT_GATE32);
}

/* ======================================================================== */
/*  pic_init — PIC (8259A ×2) 初期化                                       */
/*                                                                          */
/*  PC-98固有: ポート 0x00/0x02 (マスタ), 0x08/0x0A (スレーブ)             */
/*  FreeBSDの i8259_init() に準拠                                           */
/* ======================================================================== */
void pic_init(void)
{
    /* === マスタPIC === */
    outp(PIC1_CMD,  ICW1_INIT);       /* ICW1: 初期化開始 */
    outp(PIC1_DATA, PIC_MASTER_OFFSET); /* ICW2: IRQ0-7 → INT 0x20-0x27 */
    outp(PIC1_DATA, ICW3_MASTER);     /* ICW3: IR7にスレーブ接続 */
    outp(PIC1_DATA, ICW4_MASTER);     /* ICW4: SFNM+バッファマスタ+8086 */

    /* === スレーブPIC === */
    outp(PIC2_CMD,  ICW1_INIT);       /* ICW1: 初期化開始 */
    outp(PIC2_DATA, PIC_SLAVE_OFFSET); /* ICW2: IRQ8-15 → INT 0x28-0x2F */
    outp(PIC2_DATA, ICW3_SLAVE);      /* ICW3: マスタのIR7に接続 */
    outp(PIC2_DATA, ICW4_SLAVE);      /* ICW4: バッファスレーブ+8086 */

    /* 全IRQマスク (必要なものだけ後で有効化) */
    outp(PIC1_DATA, 0xFF);
    outp(PIC2_DATA, 0xFF);

    /* OCW3: IRR読み出しモードに設定 */
    outp(PIC1_CMD, OCW3_IRR);
    outp(PIC2_CMD, OCW3_IRR);
}

/* ======================================================================== */
/*  pit_init — PIT (8254) インターバルタイマ設定                            */
/*  カウンタ#0 をモード2(レートジェネレータ)で設定                          */
/*                                                                          */
/*  **リロード値は判定したシステムクロックから出す** (kernel/sysclk.c)。    */
/*  1.9968MHz → 19968 / 2.4576MHz → 24576。どちらもちょうど 10ms。          */
/*  決め打ちだったころ、2.4576MHz 系 (実機 PC-9821Ra266) の tick は         */
/*  8.125ms (123Hz) で、tick を数える待ち・番犬・CPU 校正が全部 23% 速く     */
/*  なっていた。NP21/W は 1.9968MHz 設定なので**ここは踏めない**            */
/*  (docs/POLICY_DEBUG.md §4-54)。                                          */
/*                                                                          */
/*  hz: 割り込み周波数。いまは PIT_HZ のみ。                                */
/*  戻り: 0 = 頼まれたとおり / 負 = 出せないので既定 PIT_HZ を積んだ         */
/* ======================================================================== */
static struct pit_setup s_pit = { 0, 0, 0, 0, 0, 0 };

const struct pit_setup *pit_get_setup(void)
{
    return &s_pit;
}

int pit_init(unsigned int hz)
{
    struct pit_setup s;
    int rc;

    rc = pit_compute(sysclk_hz(), hz, &s);
    if (rc != 0) {
        /* 出せない頼みでも**タイマ無しでは起動を続けない**。既定の
         * PIT_HZ を積んで、頼まれたとおりでないことは戻り値で伝える。 */
        if (pit_compute(sysclk_hz(), (unsigned int)PIT_HZ, &s) != 0) {
            return rc;
        }
    }
    s.mode = (u8)PIT_MODE_TIMER0;
    s_pit = s;

    outp(PIT_MODE, PIT_MODE_TIMER0);              /* モード設定 */
    outp(PIT_CNTR0, s.reload & 0xFF);             /* LSB */
    outp(PIT_CNTR0, (s.reload >> 8) & 0xFF);      /* MSB */
    return rc;
}

/* ======================================================================== */
/*  IRQ個別マスク制御                                                       */
/* ======================================================================== */
/* IMR の read-modify-write は割り込みで分断されると、間に走った別の
 * irq_enable/irq_disable の変更を書き戻しで巻き戻してしまう。
 * 必ず irq_save で括る。 */
void irq_enable(unsigned int irq)
{
    u16 port;
    u8 mask;
    unsigned int flags;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    flags = irq_save();
    mask = (u8)inp(port) & ~(1 << irq);
    outp(port, mask);
    irq_restore(flags);
}

void irq_disable(unsigned int irq)
{
    u16 port;
    u8 mask;
    unsigned int flags;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    flags = irq_save();
    mask = (u8)inp(port) | (1 << irq);
    outp(port, mask);
    irq_restore(flags);
}

/* ======================================================================== */
/*  pic_eoi — End Of Interrupt 送出                                         */
/*  PC-98ではAuto EOI不可 → 手動でEOIを送る必要がある                      */
/* ======================================================================== */
void pic_eoi(unsigned int irq)
{
    if (irq >= 8) {
        /* スレーブPICにEOI → マスタPICにもEOI */
        outp(PIC2_CMD, OCW2_EOI);
        outp(PIC1_CMD, OCW2_EOI);
    } else {
        /* マスタPICにEOI */
        outp(PIC1_CMD, OCW2_EOI);
    }
}
