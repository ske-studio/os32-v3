/* Real terminal FIFO, stream and diagnostic endpoints; transport is drained
 * synchronously as the cooperative foreground poll would do. */
#include "console.h"
#include "console_output.h"
#include "platform/powermac/lan_boot.h"
#include "platform/powermac/ofw.h"
#include "arch/ppc/kernel_boot.h"
static struct ppc_boot_devices devices;
static const ConOutputOps *output;
int boot_serial_mirror, rshell_active, pm_log_stream_only;
static unsigned delivered, serial_bytes, log_bytes, depth, max_depth, clears;
static int bad;
static char tail[7];
const struct ppc_boot_devices *ppc_boot_devices(void) { return &devices; }
int pm_escc_write(const struct pm_escc *s, const char *p, unsigned n)
{ (void)s; if (n==7) for (unsigned i=0;i<7;i++) tail[i]=p[i]; serial_bytes += n; return n; }
void con_output_register(const ConOutputOps *p) { output=p; }
void console_write(const char *p, u32 n, u8 color)
{ (void)color; if (boot_serial_mirror || rshell_active) while (n--) output->putchar(*p++); }
void tvram_clear(void) { clears++; }
void pm_log_write(const char *p, u32 n) { (void)p; log_bytes+=n; serial_bytes+=n; }
void pm_terminal_open(void) {}
void pm_terminal_feed(const u8 *p, u32 n) { (void)p; (void)n; }
void pm_terminal_line_lost(void) {}
extern void pm_diag_write(const char *, u32, u8);
void pm_terminal_poll(void)
{
    struct lan_message m;
    depth++; if (depth>max_depth) max_depth=depth;
    if (depth>1) { bad=1; depth--; return; }
    while (pm_term_tx_peek(&m)) {
        for (unsigned i=0;i<m.payload_bytes;i++)
            if (m.payload[i] != (u8)('a'+(delivered+i)%26)) bad=1;
        delivered+=m.payload_bytes;
        pm_term_tx_complete();
    }
    /* A poll may emit a diagnostic; it must never feed this stream again. */
    pm_diag_write("diag",4,1);
    depth--;
}
#include "stream.inc"
#include "log.inc"
#include "clear.inc"
static void finish(int rc)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
#define CHECK(x) do { if (!(x)) finish(1); } while (0)
void _start(void)
{
    struct lan_message open={0}, state;
    u8 peer[6]={0}; open.type=PLN1_TERM_OPEN;
    pm_term_receive(&open,peer);
    pm_console_stream_init();
    for (unsigned i=0;i<12288;i++) output->putchar('a'+i%26);
    CHECK(delivered==12288 && !bad && max_depth==1);
    CHECK(pm_term_state(&state) && !state.drop_records && !state.drop_bytes);
    CHECK(serial_bytes==12288+log_bytes && boot_serial_mirror==1);
    unsigned before=serial_bytes;
    rshell_active=1; pm_diag_write("test",4,1);
    CHECK(serial_bytes==before+4 && boot_serial_mirror==1 && rshell_active==1);
    /* clear sends control bytes only to stream and clears the local grid. */
    delivered=0; bad=0;
    wrap_tvram_clear();
    const char escape[]="\033[2J\033[H";
    for (unsigned i=0;i<7;i++) CHECK(tail[i]==escape[i]);
    CHECK(clears==1 && serial_bytes==before+4+7+4);
    finish(0);
}
