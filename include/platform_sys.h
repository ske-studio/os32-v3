/* Internal, link-time platform boundary. No registration or persistent state. */
#ifndef KERNEL_PLATFORM_SYS_H
#define KERNEL_PLATFORM_SYS_H

/* Only the reset I/O sequence; may return. Caller owns the halt loop. */
void platform_reset(void);
/* One buzzer control write each; preserve caller IF, no wait or timer setup. */
void platform_buzzer_on(void);
void platform_buzzer_off(void);

#endif /* KERNEL_PLATFORM_SYS_H */
