/* PC-9821 PIC operations; no IRQ policy or software mask state. */
#include "idt.h"
#include "io.h"
#include "platform_irq.h"

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

    if (irq < PIC_IRQS) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= PIC_IRQS;
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

    if (irq < PIC_IRQS) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= PIC_IRQS;
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
    if (irq >= PIC_IRQS) {
        /* スレーブPICにEOI → マスタPICにもEOI */
        outp(PIC2_CMD, OCW2_EOI);
        outp(PIC1_CMD, OCW2_EOI);
    } else {
        /* マスタPICにEOI */
        outp(PIC1_CMD, OCW2_EOI);
    }
}

/* IRQ15 finish, IF=0. Restore the default register selection after reading. */
int platform_irq_slave_last_in_service(void)
{
    u8 slave_isr;
    outp(PIC2_CMD, OCW3_ISR);
    slave_isr = (u8)inp(PIC2_CMD);
    outp(PIC2_CMD, OCW3_IRR);
    return (slave_isr & PIC_LAST_ISR_BIT) ? 1 : 0;
}

void platform_irq_eoi_master(void)
{
    outp(PIC1_CMD, OCW2_EOI);
}

/* Caller owns the IF=0 interval; no cached pending state. */
int platform_irq_timer_pending(void)
{
    outp(PIC1_CMD, OCW3_IRR);
    return (inp(PIC1_CMD) & 1u) ? 1 : 0;
}
