/* PC-9821 reset and built-in buzzer: preserve the existing I/O sequence. */
#include "platform_sys.h"
#include "io.h"
#include "pc98.h"

/* The reset port is triggered by a write; retain the original written value. */
#define PC98_RESET_VALUE 0x00

void platform_reset(void)
{
    /* PC-98 hardware reset (existing FreeBSD-derived sequence). */
    outp(SYSPORT_C_BSR, BSR_SHUT0_SET);
    outp(SYSPORT_C_BSR, BSR_SHUT1_SET);
    outp(CPU_RESET_PORT, PC98_RESET_VALUE);
}

/* 0037h BSR changes only BUZ (0035h bit3).
 * UNDOCUMENTED polarity: 06h sounds, 07h stops (POLICY_DEBUG §4-59). */
void platform_buzzer_on(void)
{
    outp(SYSPORT_C_BSR, BSR_BUZ_ON);
}

void platform_buzzer_off(void)
{
    outp(SYSPORT_C_BSR, BSR_BUZ_OFF);
}
