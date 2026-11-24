#ifndef SND_WRITER_PROBE_H
#define SND_WRITER_PROBE_H

/* Private diagnostic counters appended to the refill snapshot. */
enum {
    SW_WAIT_EXIT = 10, SW_EXIT_WRITE, SW_TOTAL, SW_JUMPS, SW_WORK,
    SW_WORK_US, SW_WAKE_US, SW_WAKE_IRQ, SW_PAIRS
};
enum { SW_OTHER, SW_DISPLAY, SW_GFX, SW_ABORT, SW_PARK, SW_IO_END,
       SW_GUI, SW_IDLE };

/* SW_WORK holds the total, wait-to-exit and exit-to-write winners in bytes 0..2. */
#define SW_WORK_BYTE 0xffU
#define SW_WAIT_SHIFT 8
#define SW_WRITE_SHIFT 16

#ifdef SND2_WRITER_DIAG
void sw_reset(void);
void sw_sleep(void);
void sw_irq(unsigned irq);
void sw_ready(void);
void sw_cancel(void);
void sw_point(unsigned completed_work);
void sw_exit(void);
void sw_write(void);
void sw_snapshot(unsigned long *out);
#endif
#endif
