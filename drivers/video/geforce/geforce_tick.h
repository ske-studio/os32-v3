/* Resident IRQ0 adapter; module callbacks must not remain in the tick table. */
#ifndef GEFORCE_TICK_H
#define GEFORCE_TICK_H
/* Foreground registration once, identical registration is idempotent.
 * NULL,NULL clears callback/arg under irq_save/restore, before unregister,
 * unmap or module detach. Poll runs once per tick, without wait/alloc/printf.
 * Returns 0 on success; the foundation adapter reports failure as GF1_EIRQ. */
int geforce_tick_bind(void (*poll)(void *), void *arg);
#endif
