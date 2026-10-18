#include "lan_module.h"

int lan_module_init(LanModuleApi *out)
{
    *out = (LanModuleApi){
        .probe = e82557_driver.probe, .send = e82557_send, .recv = e82557_recv,
        .mac = e82557_get_mac, .state = e82557_state, .rx_free = e82557_rx_free,
        .rx_ready = e82557_rx_ready, .loopback = e82557_loopback,
        .stat = e82557_stat, .tick = e82557_tick,
        .quiet = lan_module_quiet, .release = lan_module_release,
        .lan_init = sfs_lan_init, .lan_peer = sfs_lan_set_peer,
        .lan_reset = sfs_lan_reset, .io = sfs_lan_io
    };
    return 0;
}
const struct {
    u32 version;
    int (*initialize)(LanModuleApi *);
} lan_module_ops = {LAN_MODULE_OPS_VERSION, lan_module_init};
