#include "sys/platform/pc98/legacy/v86.h"
#include "sys/platform/pc98/legacy/v86_mem.h"
#include "usb_input.h"

/* The guest entry is the hardware edge. The production session publishes
 * active and performs its normal longjmp unwind around this callback. */
struct v86_session_state v86_session;
static int failure;
void v86_longjmp_out(void);
extern unsigned work_pending(void);
int platform_irq_reserved(unsigned line) { (void)line; return 0; }
void irq_enable(unsigned line) { (void)line; }
void irq_disable(unsigned line) { (void)line; }
void tss_set_esp0(u32 esp) { (void)esp; }
u32 tss_get_esp0(void) { return 0; }
void v86_enter(const struct v86_context *ctx)
{
    (void)ctx;
    if (!v86_is_active() || work_pending()) failure = 57;
    usb_input_usage(3, 6, 1, 1);
    if (!failure && work_pending()) failure = 58;
    v86_longjmp_out();
}
int check_v86(void)
{
    v86_session.open = 1;
    usb_input_usage(3, 6, 1, 1);
    if (work_pending() != 1) return 59;
    v86_run_limit(0, 10);
    if (v86_is_active() || work_pending()) return 60;
    return failure;
}
