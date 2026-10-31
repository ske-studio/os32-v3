/* Shared sector LRU and the directory side of the existing physical-LBA window.
 * Lifetime/media checks stay at the ISO operation's existing entry/result sites. */
typedef struct {
    bool use_ra;
    u32 end;                 /* exclusive sector index of the last window used */
} IsoDirScan;

static const u8 *iso_lookup_sector(Iso9660Ctx *ctx, u32 lba)
{
    ctx->scache_clock++;
    for (int i = 0; i < ISO_SCACHE_SLOTS; i++) {
        IsoSectorSlot *s = &ctx->scache[i];
        if (s->valid && s->lba == lba) {
            s->used = ctx->scache_clock;
            ctx->stats.scache_hits++;
            return s->data;
        }
    }
    return (const u8 *)0;
}

/* Register only the requested sector. A window copy is not a physical single
 * read, and therefore does not increment scache_fills. */
static const u8 *iso_fill_sector(Iso9660Ctx *ctx, u32 lba, const u8 *source)
{
    int victim = -1;
    IsoSectorSlot *s;

    for (int i = 0; i < ISO_SCACHE_SLOTS; i++) {
        s = &ctx->scache[i];
        if (!s->valid) { victim = i; break; }
        if (victim < 0 || s->used < ctx->scache[victim].used) victim = i;
    }
    s = &ctx->scache[victim];
    s->valid = 0;
    if (source) {
        kmemcpy(s->data, source, ISO_SECTOR_SIZE);
    } else {
        if (iso_dev_read(ctx, lba, 1, s->data) != 0) return (const u8 *)0;
        ctx->stats.scache_fills++;
    }
    s->lba = lba;
    s->used = ctx->scache_clock;
    s->valid = 1;
    return s->data;
}

/* The returned LRU pointer lasts until the next sector acquisition. */
static const u8 *iso_get_sector(Iso9660Ctx *ctx, u32 lba)
{
    const u8 *sec = iso_lookup_sector(ctx, lba);
    return sec ? sec : iso_fill_sector(ctx, lba, (const u8 *)0);
}

static const u8 *iso_dir_sector(Iso9660Ctx *ctx, u32 dir_lba, u32 dir_size,
                               u32 sect_idx, IsoDirScan *scan)
{
    u32 sectors = dir_size ? (dir_size - 1) / ISO_SECTOR_SIZE + 1 : 0;
    u32 lba;
    const u8 *sec;

    if (sect_idx >= sectors || dir_lba >= ctx->volume_size ||
        sect_idx >= ctx->volume_size - dir_lba) return (const u8 *)0;
    lba = dir_lba + sect_idx;
    sec = iso_lookup_sector(ctx, lba);
    if (sec) return sec;

    if (scan->use_ra && ctx->ra_buf) {
        bool hit = ctx->ra_valid && lba - ctx->ra_lba < ctx->ra_count;
        /* A callback replaced a window which should still cover this scan.
         * Avoid refilling the same range once per callback/sector. */
        if (!hit && sect_idx < scan->end) scan->use_ra = false;
        if (scan->use_ra && !hit) {
            u32 n = sectors - sect_idx;
            if (n > ISO_RA_SECTORS) n = ISO_RA_SECTORS;
            if (n > ctx->volume_size - lba) n = ctx->volume_size - lba;
            ctx->ra_valid = 0;
            if (iso_dev_read(ctx, lba, n, ctx->ra_buf) != 0) {
                scan->use_ra = false;
            } else {
                ctx->ra_lba = lba;
                ctx->ra_count = n;
                ctx->ra_valid = 1;
                ctx->stats.ra_fills++;
            }
        }
        if (scan->use_ra) {
            u32 left = ctx->ra_count - (lba - ctx->ra_lba);
            if (left > sectors - sect_idx) left = sectors - sect_idx;
            scan->end = sect_idx + left;
            ctx->stats.ra_hits++;
            return iso_fill_sector(ctx, lba,
                                    ctx->ra_buf + (lba - ctx->ra_lba) * ISO_SECTOR_SIZE);
        }
    }
    return iso_fill_sector(ctx, lba, (const u8 *)0);
}
