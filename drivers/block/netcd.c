/* Network CD, lan.mod. NET_HOSTSVC_DESIGN §4 and addenda #5/#8/#9.
 * One cooperative task; the resident SFS owner handles HELLO and sid renewal. */
#include "netcd.h"
#include "fs/serialfs/serialfs.h"
#include "net/host_mailbox.h"
#include "lib/kstring.h"
#include "os32_kapi_shared.h"

#define NETCD_IDLE_TICKS 3000u
#define NETCD_DEADLINE_TICKS 100u
#define NETCD_SECTOR_SHIFT 9u
#define NETCD_SEQ_LIMIT 0xF000u

struct NetCdContext {
    struct NetCd media;
    struct SfsBlock block;
    char alias[SFS_BLOCK_ALIAS_MAX + 1];
    u16 alias_len;
    int identified;
    int recheck;
};

static struct NetCdContext cd;
static int registered;

static int netcd_write(Device *dev, int lba, int count, const void *buf)
{
    (void)dev; (void)lba; (void)count; (void)buf;
    return OS32_ERR_ROFS;
}

static Device cd_device = {
    .name = "cd0", .type = DEV_BLOCK, .bus_type = DEV_BUS_NONE,
    .sect_size = SFS_LOGICAL_BLOCK_SIZE, .blk_write = netcd_write,
    .priv = &cd, .blk_read_u32 = netcd_read, .media_check = netcd_media_check
};

/* Capture the operation's clock once, before any ensure/HELLO/RPC. */
static int netcd_clock(const SfsClient **client, u32 *deadline)
{
    *client = sfs_persistent_client();
    if (!*client) return OS32_ERR_HOSTDOWN;
    *deadline = (*client)->io->now((*client)->ctx) + NETCD_DEADLINE_TICKS;
    return 0;
}

static int same_image(const struct NetCd *media, const struct SfsBlock *block)
{
    return media->size[0] == block->size[0] &&
           media->size[1] == block->size[1] &&
           media->mtime[0] == block->mtime[0] &&
           media->mtime[1] == block->mtime[1];
}

static int netcd_check(Device *dev, const SfsClient *client, u32 deadline)
{
    struct NetCdContext *ctx = dev->priv;
    struct NetCd *media = &ctx->media;
    struct HostAlive alive;
    struct SfsBlock opened;
    u32 now = client->io->now(client->ctx);
    host_alive_snapshot(&alive);
    int refresh = !ctx->block.sid || ctx->recheck || !client->sid ||
                  client->seq > NETCD_SEQ_LIMIT ||
                  (u32)(now - media->last_use) >= NETCD_IDLE_TICKS ||
                  (alive.valid && alive.observation != media->alive_seen);
    int rc = sfs_ensure_session(deadline);
    if (rc) return rc;
    /* /host can renew the shared session between two CD accesses. */
    if (client->sid != ctx->block.sid) refresh = 1;
    if (refresh) {
        if (ctx->block.sid && ctx->block.sid == client->sid) {
            rc = sfs_bclose(&ctx->block, deadline);
            ctx->block.sid = 0;
            if (rc && rc != OS32_ERR_STALE) return rc;
        }
        rc = sfs_bopen(ctx->alias, ctx->alias_len, &opened, deadline);
        if (rc) return rc;
        int changed = ctx->identified && !same_image(media, &opened);
        ctx->block = opened;
        ctx->recheck = 0;
        media->handle = opened.handle;
        media->size[0] = opened.size[0]; media->size[1] = opened.size[1];
        media->mtime[0] = opened.mtime[0]; media->mtime[1] = opened.mtime[1];
        dev->total_sects = (opened.size[0] >> NETCD_SECTOR_SHIFT) |
                           (opened.size[1] << (32 - NETCD_SECTOR_SHIFT));
        ctx->identified = 1;
        /* Save the pre-I/O snapshot: arrivals during BOPEN stay pending. */
        if (alive.valid) media->alive_seen = alive.observation;
        media->last_use = client->io->now(client->ctx);
        if (changed) {
            media->media_gen++;
            return OS32_ERR_STALE;
        }
    }
    media->last_use = client->io->now(client->ctx);
    return 0;
}

int netcd_media_check(Device *dev, u32 *generation)
{
    const SfsClient *client;
    u32 deadline;
    if (!dev || !dev->priv || !generation) return OS32_ERR_INVAL;
    int rc = netcd_clock(&client, &deadline);
    if (!rc) rc = netcd_check(dev, client, deadline);
    *generation = ((struct NetCdContext *)dev->priv)->media.media_gen;
    return rc;
}

int netcd_read(Device *dev, u32 lba, int count, void *buf)
{
    const SfsClient *client;
    u32 deadline;
    if (!dev || !dev->priv || !buf || count <= 0) return OS32_ERR_INVAL;
    int rc = netcd_clock(&client, &deadline);
    if (rc) return rc;
    rc = netcd_check(dev, client, deadline);
    if (rc) return rc;
    if (lba > dev->total_sects || (u32)count > dev->total_sects - lba)
        return OS32_ERR_INVAL;
    struct NetCdContext *ctx = dev->priv;
    u8 *dst = buf;
    while (count) {
        /* BREAD's ensure may otherwise roll sid at the sequence boundary.
         * Check before each chunk so no old handle is sent after rollover. */
        rc = netcd_check(dev, client, deadline);
        if (rc) return rc;
        /* BOPEN itself can be the request crossing the sequence limit. */
        if (client->seq > NETCD_SEQ_LIMIT) continue;
        u16 chunk = count > SFS_BREAD_COUNT_MAX ? SFS_BREAD_COUNT_MAX : count;
        u32 bytes = (u32)chunk * SFS_LOGICAL_BLOCK_SIZE;
        rc = sfs_bread(&ctx->block, lba, chunk, dst, bytes, deadline);
        if (rc) {
            if (rc == OS32_ERR_STALE &&
                (!client->sid || client->sid != ctx->block.sid)) continue;
            /* STALE from the host requires BOPEN even if sid is unchanged. */
            if (rc == OS32_ERR_STALE) ctx->recheck = 1;
            return rc;
        }
        lba += chunk;
        count -= chunk;
        dst += bytes;
    }
    ctx->media.last_use = client->io->now(client->ctx);
    return 0;
}

int netcd_register(const char *alias)
{
    if (!alias) return OS32_ERR_INVAL;
    u32 len = kstrlen(alias);
    if (len < SFS_BLOCK_ALIAS_MIN || len > SFS_BLOCK_ALIAS_MAX)
        return OS32_ERR_INVAL;
    if (registered || dev_find(cd_device.name)) return OS32_ERR_EXIST;
    /* Boot wiring calls this only in stage I. No host contact at registration. */
    /* The general cdN helper is supplied in I; cd0 is mandatory here. With
     * a valid static Device and no duplicate, registration can only be full. */
    if (dev_register(&cd_device)) return OS32_ERR_FULL;
    kstrncpy(cd.alias, alias, sizeof(cd.alias));
    cd.alias_len = (u16)len;
    cd.media.media_gen = 1;
    registered = 1;
    return 0;
}
