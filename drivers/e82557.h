#ifndef E82557_H
#define E82557_H
#include "pci_bind.h"
#define E82557_TRY_PATH "/var/log/lan.try"
#define E82557_SEQ_PATH "/var/log/lan.seq"
#define E82557_LOG_PATH "/var/log/lan.log"
#define E82557_TRY_CAP 32
enum e82557_trace_stage {
    E82557_TRACE_TRY = 1, E82557_TRACE_WIN, E82557_TRACE_RST,
    E82557_TRACE_BASE, E82557_TRACE_EEPROM, E82557_TRACE_STOP,
    E82557_TRACE_DONE
};
/* Reserved S4 imports: LB-1 has no running NIC. */
int e82557_send(const void *buf, u32 len);
int e82557_recv(void *buf, u32 cap, u32 *len);
void e82557_get_mac(u8 *mac);
int e82557_state(void);
int e82557_loopback(int on);
u32 e82557_stat(u32 idx);
extern const struct pci_driver e82557_driver;
#endif
