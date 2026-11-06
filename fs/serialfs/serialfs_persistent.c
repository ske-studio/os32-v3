/* LAN resident /host owner (NET_HOSTSVC_DESIGN §3, #5/#6/#7/#10).
 * Task context only; IRQ notifications are observed through the snapshot. */
#include "serialfs.h"
#include "net/host_mailbox.h"
#include "os32_kapi_shared.h"

#define SFS_PERSISTENT_IDLE_TICKS 3000u
#define SFS_PERSISTENT_SEQ_LIMIT  0xF000u
#define SFS_PERSISTENT_BAUD       115200u

static struct SfsPersistent resident;
static struct SfsPersistentConfig config;
static u32 host_generation, nonce;
static int enabled, scope, host_valid, reconnect;

int sfs_persistent_enabled(void) { return enabled; }
const SfsClient *sfs_persistent_client(void)
{
    return enabled ? &resident.client : 0;
}
u32 sfs_persistent_generation(void) { return resident.generation; }

void sfs_persistent_stale(void)
{
    if (!enabled) return;
    resident.generation++;
    resident.client.sid = 0;
    reconnect = 1;
}

void sfs_alive_observe(u32 generation)
{
    if (!enabled) return;
    if (host_valid && host_generation != generation)
        sfs_persistent_stale();
    host_generation = generation;
    host_valid = 1;
    reconnect = 1;
}

static void observe_snapshot(const struct HostAlive *alive)
{
    if (alive->valid && alive->observation != resident.alive_seen) {
        sfs_alive_observe(alive->host_generation);
        /* Consume only our snapshot; a notification during HELLO remains new. */
        resident.alive_seen = alive->observation;
    }
}

int sfs_persistent_start(const struct SfsPersistentConfig *cfg)
{
    struct HostAlive alive;
    int rc;

    if (enabled) return OS32_ERR_BUSY;
    if (!cfg || !cfg->lan_enabled || !cfg->io || !cfg->io->put ||
        !cfg->io->get || !cfg->io->now || !cfg->io->idle || !cfg->io->can_wait)
        return OS32_ERR_INVAL;
    if (vfs_is_mounted(SERIALFS_PREFIX) || serialfs_is_mounted())
        return OS32_ERR_BUSY;
    config = *cfg;
    sfs_client_init(&resident.client, config.io, config.io_ctx,
                    SFS_PERSISTENT_BAUD);
    resident.client.max_payload = SFS_LAN_MAX_PAYLOAD;
    resident.generation = 1;
    resident.last_use = config.io->now(config.io_ctx);
    host_alive_snapshot(&alive);
    host_valid = alive.valid;
    host_generation = alive.host_generation;
    resident.alive_seen = alive.observation;
    serialfs_permit(&resident.client);
    rc = vfs_mount(SERIALFS_PREFIX, SERIALFS_DEVNAME, SERIALFS_NAME);
    if (rc != 0) {
        serialfs_forbid();
        resident.generation = 0;
        return rc;
    }
    enabled = 1;
    reconnect = 0;
    scope = 0;
    return 0;
}

int sfs_persistent_scope_begin(void)
{
    if (!enabled) return OS32_ERR_INVAL;
    if (scope) return OS32_ERR_BUSY;
    scope = 1;
    return 0;
}

int sfs_persistent_scope_end(int exit_code)
{
    (void)exit_code;
    if (!enabled || !scope) return OS32_ERR_INVAL;
    scope = 0;
    return 0;
}

int sfs_ensure_session(u32 deadline)
{
    SfsClient *c = &resident.client;
    struct HostAlive alive;
    u32 now;
    int rc, renew;

    if (!enabled) return OS32_ERR_HOSTDOWN;
    if (!config.io->can_wait(config.io_ctx)) return OS32_ERR_IO;
    host_alive_snapshot(&alive);
    observe_snapshot(&alive);
    now = config.io->now(config.io_ctx);
    if ((i32)(now - deadline) >= 0) return OS32_ERR_HOSTDOWN;
    renew = !c->sid || reconnect ||
            (u32)(now - resident.last_use) >= SFS_PERSISTENT_IDLE_TICKS ||
            c->seq > SFS_PERSISTENT_SEQ_LIMIT;
    resident.last_use = now;
    if (!renew) return 0;

    if (c->sid && c->seq > SFS_PERSISTENT_SEQ_LIMIT) {
        /* A failed BYE is harmless: HELLO replaces the old session. */
        (void)sfs_client_oneway_deadline(c, SFS_T_BYE, 0, 0, deadline);
    }
    c->sid = 0;
    c->dead = SFS_DEAD_NONE;
    c->fails = 0;
    if ((i32)(config.io->now(config.io_ctx) - deadline) >= 0)
        return OS32_ERR_HOSTDOWN;
    /* Never reuse a nonce after a failed exchange, including clock wrap. */
    if (++nonce == 0) ++nonce;
    rc = sfs_client_hello_deadline(c, nonce, deadline);
    if (rc != 0) {
        c->sid = 0;
        return rc;
    }
    resident.last_use = config.io->now(config.io_ctx);
    reconnect = 0;
    return 0;
}
