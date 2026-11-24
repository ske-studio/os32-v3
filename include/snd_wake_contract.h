#ifndef SND_WAKE_CONTRACT_H
#define SND_WAKE_CONTRACT_H

#include "types.h"

/* The session owner calls these with interrupts disabled. The notification
 * runs inside the clock IRQ and may only mark work for later dispatch. */
int snd_wake_register(void (*notify)(void));
int snd_wake_arm(u32 after_us);

#endif
