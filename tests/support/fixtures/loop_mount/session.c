/* Run the foreground LAN lifecycle against an in-memory RPC peer. */
#include "serialfs/serialfs.h"
#include "serialfs/sfs_lan.h"
#include "lan_contract.h"
#include "loop_contract.h"
#include "kstring.h"

int serial_is_initialized(void) { return 1; }
int sysconfig_get_int(const char *path, const char *key, int def) { return 1; }
int sysconfig_get_str(const char *path, const char *key, char *out, int cap)
{
    out[0] = 0;
    return 0;
}
int lan_module_session_acquire(void) { return 1; }
void lan_module_session_release(void) {}
int e82557_state(void) { return E82557_RUNNING; }
int e82557_rx_ready(void) { return 1; }
void e82557_get_mac(u8 *mac) { kmemset(mac, 0, 6); }

void sfs_client_init(SfsClient *c, const SfsIo *io, void *ctx, u32 baud)
{
    kmemset(c, 0, sizeof(*c));
    c->io = io;
    c->ctx = ctx;
}
int sfs_client_hello(SfsClient *c, u32 nonce) { c->sid = 1; return 0; }
int sfs_client_oneway(SfsClient *c, u8 type, const u8 *pl, u16 len)
{
    return type == SFS_T_BYE ? 0 : -1;
}
const SfsClient *sfs_persistent_client(void) { return 0; }

static unsigned calls;
int sfs_client_call(SfsClient *c, u8 type, const u8 *pl, u16 len,
                    const u8 **resp, u16 *rlen)
{
    static u8 reply[SFS_LAN_MAX_PAYLOAD];
    calls++;
    if (!c->io) __builtin_trap();
    kmemset(reply, 0, sizeof(reply));
    *resp = reply;
    if (type == SFS_T_STAT) {
        reply[SFS_STATUS_LEN] = SFS_KIND_FILE;
        sfs_put32(reply + SFS_STATUS_LEN + 1, 163840);
        *rlen = SFS_STATUS_LEN + SFS_STAT_RSP_LEN;
    } else if (type == SFS_T_READ) {
        u16 count = sfs_get16(pl + SFS_READ_COUNT_OFF);
        if (count > sizeof(reply) - SFS_STATUS_LEN) return -1;
        sfs_put32(reply, count);
        *rlen = SFS_STATUS_LEN + count;
    } else if (type == SFS_T_WRITE) {
        unsigned header = SFS_WRITE_HDR_LEN + 1 + pl[SFS_WRITE_HDR_LEN];
        if (header > len) return -1;
        sfs_put32(reply, len - header);
        *rlen = SFS_STATUS_LEN;
    } else {
        return -1;
    }
    return 0;
}

int probe_session(void)
{
    u8 sector[512];
    if (serialfs_init() || sfs_session_fg.begin() ||
        loop_dev_attach("/host/a.img", 0) ||
        loop_dev_read_lba(0, 0, 1, sector) ||
        loop_dev_write_lba(0, 0, 1, sector)) return 22;
    if (sfs_session_fg.end(0) || vfs_is_mounted("/host") ||
        serialfs_is_mounted()) return 23;
    for (int session = 0; session < 2; session++) {
        unsigned before = calls;
        if (loop_dev_read_lba(0, 0, 1, sector) >= 0 ||
            loop_dev_write_lba(0, 0, 1, sector) >= 0 || calls != before) return 24;
        if (session == 0 && sfs_session_fg.begin()) return 25;
    }
    loop_dev_detach(0);
    return sfs_session_fg.end(0) ? 25 : 0;
}

const SfsIo sfs_lan_io = {0};
void sfs_lan_init(SfsLan *lan, const SfsLanNic *nic, const SfsIo *clock, void *ctx) {}
void sfs_lan_reset(SfsLan *lan) {}
int sfs_lan_set_peer(SfsLan *lan, const char *mac) { return 0; }
