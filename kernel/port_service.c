/* Stateless forwarding; UART state, gates and wait policy stay in the driver. */
#include "port_service.h"
#include "console_output.h"
#include "os32_kapi_shared.h"

static void port_empty_init(u32 baud) { (void)baud; }
static void port_empty_puts(const char *str) { (void)str; }
static int port_empty_getchar(void) { return -1; }
static int port_empty_putchar(char c) { (void)c; return OS32_ERR_NOSYS; }
static int port_empty_initialized(void) { return 0; }
static int port_empty_vfast(u32 baud) { (void)baud; return OS32_ERR_NOSYS; }
static void port_empty_diag(u32 *oe, u32 *fe, u32 *pe, u32 *overflow)
{
    if (oe) *oe = 0;
    if (fe) *fe = 0;
    if (pe) *pe = 0;
    if (overflow) *overflow = 0;
}
static int port_empty_status(u32 *mode, u32 *baud, u32 *fifo)
{
    port_empty_diag(mode, baud, fifo, 0);
    return OS32_ERR_NOSYS;
}
static const PortOps port_empty_port = {
    .init = port_empty_init, .puts_kapi = port_empty_puts,
    .getchar_kapi = port_empty_getchar, .putchar_kapi = port_empty_putchar,
    .trygetchar = port_empty_getchar, .is_initialized = port_empty_initialized,
    .init_vfast = port_empty_vfast, .get_status = port_empty_status, .diag = port_empty_diag,
};
static const PortOps *port_provider = &port_empty_port;
static const ConOutputOps port_empty_output = { .putchar = port_empty_putchar };
static const ConOutputOps *output_provider = &port_empty_output;

void port_register(const PortOps *ops) { port_provider = ops; }
void port_init(u32 baud) { port_provider->init(baud); }
void port_puts_kapi(const char *str) { port_provider->puts_kapi(str); }
int port_getchar_kapi(void) { return port_provider->getchar_kapi(); }
int port_putchar_kapi(char c) { return port_provider->putchar_kapi(c); }
int port_trygetchar(void) { return port_provider->trygetchar(); }
int port_is_initialized(void) { return port_provider->is_initialized(); }
int port_init_vfast(u32 baud) { return port_provider->init_vfast(baud); }
int port_get_status(u32 *mode, u32 *baud, u32 *fifo)
{ return port_provider->get_status(mode, baud, fifo); }
void port_diag_get(u32 *oe, u32 *fe, u32 *pe, u32 *overflow)
{ port_provider->diag(oe, fe, pe, overflow); }
void con_output_register(const ConOutputOps *ops) { output_provider = ops; }
int con_output_putchar(char c) { return output_provider->putchar(c); }
