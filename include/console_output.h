/* Normal console duplication; registration is boot-only, with no device I/O. */
#ifndef OS32_CONSOLE_OUTPUT_H
#define OS32_CONSOLE_OUTPUT_H

typedef struct ConOutputOps { int (*putchar)(char c); } ConOutputOps;
void con_output_register(const ConOutputOps *ops);
int con_output_putchar(char c);

/* Link-time fixed provider, callable before registration and with IF=0.
 * No allocation, locks, console, hlt or calibrated waits in these operations. */
typedef struct EmergencyOps {
    void (*puts_polled)(const char *str);
    int (*gate_active)(void);
} EmergencyOps;
extern const EmergencyOps emergency_output;
#endif
