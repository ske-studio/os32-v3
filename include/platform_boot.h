#ifndef PLATFORM_BOOT_H
#define PLATFORM_BOOT_H
/* Capture is connected in stage G; IRQ stages preserve IF. */
void platform_boot_capture(void);
void platform_boot_irq_controller(void);
void platform_boot_clock_detect(void);
void platform_boot_irq_start(void);
#endif
