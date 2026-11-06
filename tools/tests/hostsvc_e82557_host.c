#include "net/nic.h"
#include "drivers/net/e82557/lan_module.h"
#include "drivers/net/e82557/e82557_regs.h"
#include "os32/os32_kapi_shared.h"

static int available, refs, acquires, releases, state, tx_rc, rx_rc, ticks;
static const void *sent;
static u32 sent_len, free_slots, received_cap;
static void *received;
static u32 *received_len;
int lan_module_session_acquire(void)
{
    acquires++;
    if (!available) return 0;
    refs++;
    return 1;
}
void lan_module_session_release(void) { releases++; refs--; }
int e82557_state(void) { return state; }
int e82557_send(const void *p, u32 n) { sent = p; sent_len = n; return tx_rc; }
int e82557_recv(void *p, u32 cap, u32 *len)
{
    received = p; received_cap = cap; received_len = len;
    if (len) *len = rx_rc == 0 ? 1 : 0;
    if (rx_rc == 0 && p && cap) *(u8 *)p = 42;
    return rx_rc;
}
void e82557_get_mac(u8 *p) { for (u32 i = 0; i < 6; i++) p[i] = i + 1; }
u32 e82557_rx_free(void) { return free_slots; }
void e82557_tick(void) { ticks++; }

static int run(void)
{
    const struct NetNicOps *ops = 0;
    u8 data[8];
    u32 len;
    if (net_e82557_adapter.acquire(0) != OS32_ERR_INVAL || acquires) return 1;
    if (net_e82557_adapter.acquire(&ops) != OS32_ERR_NOSYS || ops || refs || acquires != 1) return 2;
    available = 1;
    for (state = E82557_OFF; state <= E82557_QUARANTINED; state++) {
        int usable = state == E82557_RUNNING || state == E82557_RX_WAIT || state == E82557_RECOVER;
        int rc = net_e82557_adapter.acquire(&ops);
        if (usable) {
            if (rc || !ops || refs != 1) return 3;
            net_e82557_adapter.release();
        } else if (rc != OS32_ERR_NOSYS || refs) return 4;
        if (refs) return 5;
    }
    if (acquires != releases + 1) return 6;
    state = E82557_RUNNING;
    if (net_e82557_adapter.acquire(&ops) || refs != 1) return 7;
    const int codes[] = {0, OS32_ERR_AGAIN, OS32_ERR_NOSYS, OS32_ERR_INVAL,
                         OS32_ERR_NOSPC, OS32_ERR_IO};
    for (u32 i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        tx_rc = rx_rc = codes[i];
        int want = codes[i] == OS32_ERR_NOSYS ? OS32_ERR_HOSTDOWN : codes[i];
        if (ops->tx(data, sizeof(data)) != want || sent != data || sent_len != sizeof(data)) return 8;
        len = 99;
        if (ops->rx(data, sizeof(data), &len) != want || received != data ||
            received_cap != sizeof(data) || received_len != &len) return 9;
        if (len != (codes[i] == 0 ? 1u : 0u)) return 10;
        if (codes[i] == 0 && data[0] != 42) return 11;
    }
    ops->mac(data);
    for (u32 i = 0; i < 6; i++) if (data[i] != i + 1) return 12;
    for (free_slots = 0; free_slots <= E82557_RFD_COUNT; free_slots++)
        if (ops->rx_credit_bytes() != free_slots * E82557_FRAME_MAX) return 13;
    ops->poll(); ops->poll();
    if (ticks || refs != 1) return 14;
    net_e82557_adapter.release();
    if (refs || acquires != releases + 1) return 15;
    return 0;
}
void _start(void)
{
    int rc = run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
