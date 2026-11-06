/* The shared NIC borrows lan.mod operations while holding a task reference. */
#include "net/nic.h"
#include "drivers/net/e82557/lan_module.h"
#include "drivers/net/e82557/e82557_regs.h"
#include "os32/os32_kapi_shared.h"

static int nic_result(int rc)
{
    return rc == OS32_ERR_NOSYS ? OS32_ERR_HOSTDOWN : rc;
}

static int nic_tx(const void *frame, u32 len)
{
    return nic_result(e82557_send(frame, len));
}

static int nic_rx(void *frame, u32 cap, u32 *len)
{
    return nic_result(e82557_recv(frame, cap, len));
}

/* PC98_TICK_E82557 already services IRQ events and recovery before net_tick. */
static void nic_poll(void) { }

static u32 nic_credit(void)
{
    return e82557_rx_free() * E82557_FRAME_MAX;
}

static const struct NetNicOps nic_ops = {
    nic_tx, nic_rx, e82557_get_mac, nic_poll, nic_credit
};

static int nic_acquire(const struct NetNicOps **out)
{
    if (!out) return OS32_ERR_INVAL;
    if (!lan_module_session_acquire()) return OS32_ERR_NOSYS;
    int state = e82557_state();
    if (state != E82557_RUNNING && state != E82557_RX_WAIT &&
        state != E82557_RECOVER) {
        lan_module_session_release();
        return OS32_ERR_NOSYS;
    }
    *out = &nic_ops;
    return 0;
}

const struct NetAdapter net_e82557_adapter = {
    nic_acquire, lan_module_session_release
};
