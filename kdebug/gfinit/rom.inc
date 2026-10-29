static void gf_inspect_line(u32 sid, int rc, u32 len, u32 crc, u32 chip)
{
    kprintf(ATTR_WHITE, "GF inspect sid=%u rc=%d rom_len=", sid, rc);
    if (len) kprintf(ATTR_WHITE, "%u crc=%08X", len, crc);
    else kprintf(ATTR_WHITE, "unknown crc=unknown");
    if (chip == GF_CHIP_UNKNOWN)
        kprintf(ATTR_WHITE, " chip=unknown expect=%02X match=unknown\n", GF_VERSION_CHIP);
    else kprintf(ATTR_WHITE, " chip=%02X expect=%02X match=%u\n",
                 chip, GF_VERSION_CHIP, chip == GF_VERSION_CHIP);
}
static int gf_read_rom(struct gfinit_owner *g, u32 *len, u32 *crc, u32 *chip)
{
    KdebugPeek req;
    u32 raw, bars[RECOVER_BAR_COUNT];
    if (gfinit_bus_raw_config(&g->bus, &raw, bars) || !(raw & RECOVER_COMMAND_MEM) ||
        bars[0] > ~0U - KPEEK_NV_PROM_OFF - GF_ROM_MAX) return -1;
    g->rom = gf_alloc(GF_ROM_MAX);
    if (!g->rom) return -1;
    /* Read both copies through the existing read-only PROM descriptor/window. */
    for (u32 pass = 0; pass < 2; pass++) {
        for (u32 off = 0; off < GF_ROM_MAX; off += KDEBUG_PEEK_CHUNK) {
            req = (KdebugPeek){0};
            req.addr = bars[0] + KPEEK_NV_PROM_OFF + off;
            req.len = KDEBUG_PEEK_CHUNK; req.width = 1; req.space = KDEBUG_PEEK_SPACE_MMIO;
            if (kpeek_call(KDEBUG_OP_PEEK, &req, sizeof(req)) || req.status) return -1;
            for (u32 j = 0; j < req.len; j++) {
                if (pass && g->rom[off+j] != req.data[j]) return -1;
                g->rom[off+j] = req.data[j];
            }
        }
    }
    if (gf_rom_validate(g->rom, len, crc, chip)) return -1;
    g->rom_size = *len;
    return gfinit_decode_rom(g->rom, g->rom_size, &g->profile, g->decoded);
}
