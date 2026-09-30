/* ========================================================================
 *  tools/tests/kbd_hostshim/io.h — ホスト試験用の io.h 差し替え
 *
 *  include/io.h は inp / outp / irq_save を特権命令のインライン asm で定義
 *  するので、drivers/kbd.c (IRQ1 ハンドラ) をホストで走らせるために -I で
 *  先に置いて差し替え、ポート操作を試験側の 8251 の模型
 *  (tools/tests/kbd_dlog_host.c) へ回す。pcm_hostshim/io.h と同じ作法。
 *  RING_DEQUEUE は include/io.h の写し (kbd.c が使う)。
 *
 *  [C1] C89 / GNU89。
 * ======================================================================== */

#ifndef IO_H
#define IO_H

/* 試験側 (tools/tests/kbd_dlog_host.c) が定義する */
unsigned int kbd_shim_inp(unsigned int port);
void kbd_shim_outp(unsigned int port, unsigned int value);

static inline unsigned int inp(unsigned int port) { return kbd_shim_inp(port); }
static inline void outp(unsigned int port, unsigned int value)
{ kbd_shim_outp(port, value); }

static inline void io_wait(void) { }

/* 割り込み禁止は深さで数える (入れ子の釣り合いを試験が見る) */
extern int kbd_shim_irq_depth;
static inline unsigned int irq_save(void) { kbd_shim_irq_depth++; return 0x200u; }
static inline void irq_restore(unsigned int flags)
{ (void)flags; kbd_shim_irq_depth--; }
static inline void _enable(void) { }
static inline void _disable(void) { }
static inline void _halt(void) { }

#define RING_DEQUEUE(entry, buf, head, count, bufsize)      \
    do {                                                    \
        unsigned int ring_flags_ = irq_save();              \
        (entry) = (buf)[(head)];                            \
        (head) = ((head) + 1) % (bufsize);                  \
        (count)--;                                          \
        irq_restore(ring_flags_);                           \
    } while (0)

#endif /* IO_H */
