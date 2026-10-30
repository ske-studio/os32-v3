/* Resident IRQ0 callbacks. Registration is foreground-only; no removal. */
#ifndef TICK_H
#define TICK_H

#define TICK_SLOT_COUNT 6
#define TICK_ERR_CTX   -1
#define TICK_ERR_INVAL -2
#define TICK_ERR_BUSY  -3

typedef void (*tick_fn)(void);
struct tick_slot { tick_fn fn; };

/* Identical registrations succeed. Errors leave the table and IF unchanged. */
int tick_register(unsigned int slot, tick_fn fn);
/* IRQ0, IF=0: each occupied slot runs once, in ascending slot order. */
void tick_dispatch(void);

#endif
