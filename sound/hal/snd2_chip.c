#include "snd2_int.h"
#include "kstring.h"

extern int res_owner_get(void);

/* S1 allocates PCM devs 0..15. IS enumerates both sets of stable devs. */
#define CHIP_BASE 16U
#define CHIP_COUNT 16U
typedef struct {
    const SndChipBackend *be;
    void *ctx;
    SND2_DevInfo info;
    SND2_ChipCaps caps;
    u32 gen;
    int owner;
} Chip;

static Chip snd2_chips[CHIP_COUNT];

static Chip *device(u32 dev)
{
    u32 i = dev - CHIP_BASE;
    return i < CHIP_COUNT && snd2_chips[i].be ? &snd2_chips[i] : 0;
}

int snd2_chip_register(const SndChipBackend *be, void *ctx, const SND2_DevInfo *info)
{
    if (!info || (info->kind != SND2_KIND_FM && info->kind != SND2_KIND_PSG) ||
        !be || be->abi != SND_HAL2_ABI || !be->caps || !be->open ||
        !be->close || !be->quiet || !be->write || !be->read)
        return OS32_ERR_INVAL;
    u32 i;
    for (i = 0; i < CHIP_COUNT && snd2_chips[i].be; ++i) {}
    if (i == CHIP_COUNT)
        return OS32_ERR_FULL;
    SND2_ChipCaps caps = {0};
    int rc = be->caps(ctx, &caps);
    if (rc < 0)
        return rc;
    if (!caps.banks)
        return OS32_ERR_INVAL;
    Chip *c = &snd2_chips[i];
    c->info = *info;
    c->info.size = sizeof(c->info);
    c->info.dev = caps.dev = CHIP_BASE + i;
    c->info.flags = (info->flags & SND2_DEV_DEFAULT) | SND2_DEV_PRESENT | SND2_DEV_READY;
    caps.kind = info->kind;
    caps.size = sizeof(caps);
    c->caps = caps;
    c->ctx = ctx;
    c->be = be;
    return caps.dev;
}

int snd2_chip_dev_info(u32 index, SND2_DevInfo *out)
{
    for (u32 i = 0; out && i < CHIP_COUNT; ++i)
        if (snd2_chips[i].be && index-- == 0) {
            *out = snd2_chips[i].info;
            return 0;
        }
    return OS32_ERR_INVAL;
}

int snd2_chip_caps(u32 dev, SND2_ChipCaps *out, u32 size)
{
    Chip *c = device(dev);
    if (!c || !out || !size || (size & 3) || size > sizeof(*out))
        return OS32_ERR_INVAL;
    kmemcpy(out, &c->caps, size);
    return 0;
}

static int fault(Chip *c)
{
    c->info.flags &= ~SND2_DEV_READY;
    c->info.flags |= SND2_DEV_FAULTED;
    return OS32_ERR_IO;
}

/* Legacy FM wrappers report the first failed checked register write. */
void snd2_chip_fault(u32 dev)
{
    Chip *c = device(dev);
    if (c)
        fault(c);
}

static int acquire(Chip *c, int owner)
{
    if (!c || !owner)
        return OS32_ERR_INVAL;
    if (c->info.flags & SND2_DEV_FAULTED)
        return OS32_ERR_IO;
    if (c->owner)
        return c->owner == owner ? 0 : OS32_ERR_BUSY;
    if (c->be->open(c->ctx) < 0)
        return fault(c);
    c->owner = owner;
    return 0;
}

int snd2_chip_open(u32 dev, u32 *h)
{
    Chip *c = device(dev);
    if (!h)
        return OS32_ERR_INVAL;
    int rc = acquire(c, res_owner_get());
    if (!rc)
        *h = (c->gen << 8) | dev;
    return rc;
}

int snd2_chip_implicit(int owner)
{
    for (u32 i = 0; i < CHIP_COUNT; ++i)
        if (snd2_chips[i].be && snd2_chips[i].info.kind == SND2_KIND_FM)
            return acquire(&snd2_chips[i], owner);
    return OS32_ERR_NOTFOUND;
}

static int owned(u32 h, Chip **out)
{
    Chip *c = device(h & 255);
    if (!c || !c->owner || c->gen != (h >> 8))
        return OS32_ERR_STALE;
    if (c->owner != res_owner_get())
        return OS32_ERR_BUSY;
    *out = c;
    return 0;
}

static int ready(u32 h, Chip **out)
{
    int rc = owned(h, out);
    if (!rc && ((*out)->info.flags & SND2_DEV_FAULTED))
        return OS32_ERR_IO;
    return rc;
}

int snd2_chip_write(u32 h, const SND2_RegW *w, u32 n)
{
    Chip *c;
    int rc = ready(h, &c);
    if (rc)
        return rc;
    if (n > 256 || (n && !w))
        return OS32_ERR_INVAL;
    for (u32 i = 0; i < n; ++i)
        if (w[i].bank >= c->caps.banks)
            return OS32_ERR_INVAL;
    if (!n)
        return 0;
    rc = c->be->write(c->ctx, w, n);
    if (rc != (int)n)
        fault(c);
    return rc;
}

int snd2_chip_read(u32 h, u32 bank, u32 what, u32 reg)
{
    Chip *c;
    int rc = ready(h, &c);
    if (rc)
        return rc;
    if (bank >= c->caps.banks || (what != 1 && what != 2) || reg > 255)
        return OS32_ERR_INVAL;
    rc = c->be->read(c->ctx, bank, what, reg);
    return rc == OS32_ERR_IO ? fault(c) : rc;
}

int snd2_chip_reset(u32 h)
{
    Chip *c;
    int rc = ready(h, &c);
    if (rc)
        return rc;
    return c->be->quiet(c->ctx) < 0 ? fault(c) : 0;
}

static int release(Chip *c)
{
    int quiet = c->be->quiet(c->ctx);
    int close = c->be->close(c->ctx);
    c->owner = 0;
    c->gen = (c->gen + 1) & 0xffffff;
    if (quiet < 0 || close < 0)
        return fault(c);
    return c->info.flags & SND2_DEV_FAULTED ? OS32_ERR_IO : 0;
}

int snd2_chip_close(u32 h)
{
    Chip *c;
    int rc = owned(h, &c);
    return rc ? rc : release(c);
}

void snd2_chip_reclaim(int owner)
{
    for (u32 i = 0; owner && i < CHIP_COUNT; ++i)
        if (snd2_chips[i].owner == owner)
            release(&snd2_chips[i]);
}

int snd2_chip_unregister(u32 dev)
{
    Chip *c = device(dev);
    if (!c)
        return OS32_ERR_INVAL;
    if (c->info.flags & SND2_DEV_FAULTED)
        return OS32_ERR_BUSY;
    if (c->owner && release(c) < 0)
        return OS32_ERR_BUSY;
    c->be = 0;
    c->ctx = 0;
    return 0;
}
