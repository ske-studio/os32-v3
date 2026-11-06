/* Stateless block RPC for lan.mod; session ownership stays in vmkernel.
 * NET_HOSTSVC_DESIGN §4, #7/#8/#9. Module imports are wired in stage I. */
#include "sfs_block.h"
#include "serialfs.h"
#include "os32_kapi_shared.h"

#define SFS_BLOCK_SECTOR_SHIFT 9u

/* This request endpoint borrows the shared client only for the call. Do not
 * ensure again between checking the sid and sending a session-local handle. */
static int block_session(u32 sid, u32 deadline, SfsClient **out)
{
    int rc = sfs_ensure_session(deadline);
    if (rc) return rc;
    *out = (SfsClient *)sfs_persistent_client();
    if (!*out) return OS32_ERR_HOSTDOWN;
    if (sid && (*out)->sid != sid) return OS32_ERR_STALE;
    return 0;
}

static int block_call(SfsClient *client, u8 op, const void *in, u16 len,
                      struct SfsReply *reply, u32 deadline)
{
    int rc = sfs_client_call_deadline(client, op, in, len, reply, deadline);
    if (rc == OS32_ERR_STALE) sfs_persistent_stale();
    /* Fixed reply storage covers every valid reply for this operation. */
    if (rc == OS32_ERR_NOSPC) return OS32_ERR_IO;
    if (rc) return rc;
    if (reply->length < SFS_STATUS_LEN) return OS32_ERR_IO;
    i32 status = (i32)sfs_get32(reply->data);
    if (status < 0)
        return reply->length == SFS_STATUS_LEN ? status : OS32_ERR_IO;
    return status == 0 ? 0 : OS32_ERR_IO;
}

int sfs_bopen(const char *alias, u16 len, struct SfsBlock *out, u32 deadline)
{
    u8 data[SFS_BOPEN_RSP_LEN];
    struct SfsReply reply = {data, sizeof(data), 0};
    struct SfsBlock opened;
    SfsClient *client;
    if (!alias || !out || len < SFS_BLOCK_ALIAS_MIN || len > SFS_BLOCK_ALIAS_MAX)
        return OS32_ERR_INVAL;
    for (u16 i = 0; i < len; i++)
        if (!alias[i]) return OS32_ERR_INVAL;
    int rc = block_session(0, deadline, &client);
    if (rc) return rc;
    rc = block_call(client, SFS_T_BOPEN, alias, len, &reply, deadline);
    if (rc) return rc;
    if (reply.length != SFS_BOPEN_RSP_LEN) return OS32_ERR_IO;
    opened.sid = client->sid;
    opened.handle = sfs_get32(data + SFS_BOPEN_HANDLE_OFF);
    opened.size[0] = sfs_get32(data + SFS_BOPEN_SIZE_LO_OFF);
    opened.size[1] = sfs_get32(data + SFS_BOPEN_SIZE_HI_OFF);
    opened.mtime[0] = sfs_get32(data + SFS_BOPEN_MTIME_LO_OFF);
    opened.mtime[1] = sfs_get32(data + SFS_BOPEN_MTIME_HI_OFF);
    /* u64 size / 512 must fit u32, with no discarded partial sector. */
    if ((opened.size[0] & (SFS_LOGICAL_BLOCK_SIZE - 1)) ||
        opened.size[1] >= SFS_LOGICAL_BLOCK_SIZE) return OS32_ERR_IO;
    *out = opened;
    return 0;
}

int sfs_bread(const struct SfsBlock *block, u32 lba, u16 count,
              void *out, u32 cap, u32 deadline)
{
    u8 request[SFS_BREAD_REQ_LEN], data[SFS_BREAD_RSP_MAX];
    struct SfsReply reply = {data, sizeof(data), 0};
    SfsClient *client;
    if (!block || !block->sid || !out || count < SFS_BREAD_COUNT_MIN ||
        count > SFS_BREAD_COUNT_MAX) return OS32_ERR_INVAL;
    u32 bytes = (u32)count * SFS_LOGICAL_BLOCK_SIZE;
    if (cap < bytes) return OS32_ERR_INVAL;
    if ((block->size[0] & (SFS_LOGICAL_BLOCK_SIZE - 1)) ||
        block->size[1] >= SFS_LOGICAL_BLOCK_SIZE) return OS32_ERR_INVAL;
    u32 sectors = (block->size[0] / SFS_LOGICAL_BLOCK_SIZE) |
                  (block->size[1] << (32 - SFS_BLOCK_SECTOR_SHIFT));
    if (lba > sectors || count > sectors - lba) return OS32_ERR_INVAL;
    int rc = block_session(block->sid, deadline, &client);
    if (rc) return rc;
    sfs_put32(request + SFS_BREAD_HANDLE_OFF, block->handle);
    sfs_put32(request + SFS_BREAD_LBA_OFF, lba);
    sfs_put16(request + SFS_BREAD_COUNT_OFF, count);
    rc = block_call(client, SFS_T_BREAD, request, sizeof(request), &reply, deadline);
    if (rc) return rc;
    if (reply.length != SFS_BREAD_DATA_OFF + bytes) return OS32_ERR_IO;
    for (u32 i = 0; i < bytes; i++) ((u8 *)out)[i] = data[SFS_BREAD_DATA_OFF + i];
    return 0;
}

int sfs_bclose(const struct SfsBlock *block, u32 deadline)
{
    u8 request[SFS_BCLOSE_REQ_LEN], data[SFS_BCLOSE_RSP_LEN];
    struct SfsReply reply = {data, sizeof(data), 0};
    SfsClient *client;
    if (!block || !block->sid) return OS32_ERR_INVAL;
    int rc = block_session(block->sid, deadline, &client);
    if (rc) return rc;
    sfs_put32(request + SFS_BCLOSE_HANDLE_OFF, block->handle);
    rc = block_call(client, SFS_T_BCLOSE, request, sizeof(request), &reply, deadline);
    if (rc) return rc;
    return reply.length == SFS_BCLOSE_RSP_LEN ? 0 : OS32_ERR_IO;
}
