#include "lan_boot.h"
#include "kboot_log.h"
#include "ofw.h"
#include "sys/net/link.h"
#include "sys/net/host_mailbox.h"
#include "os32/os32_kapi_shared.h"

static char input[64 * 256 + 256];
static u32 serial_count, transport_calls;
static int acknowledged, saw_tail;
u32 link_hello_ok = 1;

static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
#define CHECK(condition) do { if (!(condition)) finish(10); } while (0)
unsigned ppc_load8(cell address) { CHECK(address == 1); return PM_RR0_TX_READY; }
void ppc_store8(cell address, unsigned value)
{
    CHECK(address == 2);
    if (serial_count < sizeof(input)) CHECK(value == (u8)input[serial_count]);
    else CHECK(serial_count == sizeof(input) && value == 'Z');
    serial_count++;
}
u32 ppc_irq_save(void) { return 0; }
void ppc_irq_restore(u32 saved) { (void)saved; }
void host_alive_snapshot(struct HostAlive *a) { a->valid = 1; a->host_generation = 1; }
void boot_token_get(u8 b[16]) { for (int i=0; i<16; i++) b[i] = 0; }
i32 link_host_open(const char *p, u32 n, int o)
{
    (void)p; (void)n; (void)o; transport_calls++; return 1;
}
i32 link_host_status(i32 h, u32 *s, u32 *n, int o)
{
    (void)h; (void)n; (void)o; transport_calls++;
    *s = 200;
    return acknowledged ? 0 : OS32_ERR_AGAIN;
}
i32 link_host_write(i32 h, const void *p, u32 n, int o)
{
    (void)h; (void)o; transport_calls++;
    if (n == 1 && *(const char *)p == 'Z') saw_tail++;
    return (i32)n;
}
i32 link_host_close(i32 h, int o) { (void)h; (void)o; transport_calls++; return 0; }

void _start(void)
{
    struct pm_log_stats stats;
    u64 next, records, bytes;
    u32 flags;
    CHECK(!pm_log_escc_init(1, 2) && !pm_lan_log_register());
    for (u32 i=0; i<sizeof(input); i++) input[i] = (char)i;
    pm_log_write(input, 256);
    pm_link_log_poll();
    pm_link_log_poll();
    u32 before = transport_calls;
    pm_log_write(input + 256, sizeof(input) - 256);
    CHECK(serial_count == sizeof(input) && transport_calls == before);
    pm_lan_log_stats(&next, &records, &bytes, &flags);
    CHECK(records == 1 && bytes == 256);
    CHECK(!pm_log_get_stats(1, &stats) && !stats.dropped && !stats.failures);
    acknowledged = 1;
    pm_link_log_poll();
    before = transport_calls;
    pm_log_write("Z", 1);
    CHECK(serial_count == sizeof(input)+1 && transport_calls == before);
    for (u32 slot=0; slot<64; slot++) {
        acknowledged = 0;
        pm_link_log_poll();
        pm_link_log_poll();
        acknowledged = 1;
        pm_link_log_poll();
    }
    CHECK(saw_tail == 1);
    finish(0);
}
