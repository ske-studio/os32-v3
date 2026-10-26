/* Cooperative serial service. Register a complete resident table at boot. */
#ifndef OS32_PORT_SERVICE_H
#define OS32_PORT_SERVICE_H
#include "types.h"

typedef struct PortOps {
    void (*init)(u32 baud);
    void (*puts_kapi)(const char *str);
    int (*getchar_kapi)(void);
    int (*putchar_kapi)(char c);
    int (*trygetchar)(void);
    int (*is_initialized)(void);
    int (*init_vfast)(u32 baud);
    int (*get_status)(u32 *mode, u32 *baud, u32 *fifo);
    void (*diag)(u32 *oe, u32 *fe, u32 *pe, u32 *overflow);
} PortOps;

void port_register(const PortOps *ops);
void port_init(u32 baud);
void port_puts_kapi(const char *str);
int port_getchar_kapi(void);
int port_putchar_kapi(char c);
int port_trygetchar(void);
int port_is_initialized(void);
int port_init_vfast(u32 baud);
int port_get_status(u32 *mode, u32 *baud, u32 *fifo);
void port_diag_get(u32 *oe, u32 *fe, u32 *pe, u32 *overflow);
#endif
