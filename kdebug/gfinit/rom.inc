static u32 gf_rom_chip(const u8 *rom, u32 size)
{
    static const u8 signature[] = {0xff, 0xb8, 'B', 'I', 'T', 0, 0};
    for (u32 off = 0; off + GF_BIT_HEADER <= size; off++) {
        u32 i = 0;
        while (i < sizeof(signature) && rom[off+i] == signature[i]) i++;
        if (i != sizeof(signature)) continue;
        u32 head = rom[off+8], stride = rom[off+9], count = rom[off+10];
        if (head < GF_BIT_HEADER || stride < GF_BIT_ENTRY ||
            head + count * stride > size - off) return GF_CHIP_UNKNOWN;
        for (u32 n = 0; n < count; n++) {
            const u8 *entry = rom + off + head + n * stride;
            if (entry[0] != 'i' || entry[1] != GF_BIT_I_VERSION) continue;
            u32 len = (u32)entry[2] | (u32)entry[3] << 8;
            u32 data = (u32)entry[4] | (u32)entry[5] << 8;
            if (len <= GF_BIT_CHIP_OFFSET || data > size || len > size - data)
                return GF_CHIP_UNKNOWN;
            return rom[data + GF_BIT_CHIP_OFFSET];
        }
        return GF_CHIP_UNKNOWN;
    }
    return GF_CHIP_UNKNOWN;
}
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
    if (g->rom[0] != PCI_ROM_SIG0 || g->rom[1] != PCI_ROM_SIG1 || !g->rom[PCI_ROM_SIZE_OFF]) return -1;
    g->rom_size = (u32)g->rom[PCI_ROM_SIZE_OFF] * PCI_ROM_SIZE_UNIT;
    if (g->rom_size > GF_ROM_MAX) return -1;
    *len = g->rom_size;
    *crc = crc32_core_final(crc32_core_update(CRC32_INIT, g->rom, *len));
    *chip = gf_rom_chip(g->rom, *len);
    u32 pcir = (u32)g->rom[PCI_ROM_PCIR_PTR] | (u32)g->rom[PCI_ROM_PCIR_PTR + 1] << 8;
    if (g->rom_size < PCIR_HEADER_SIZE || pcir > g->rom_size - PCIR_HEADER_SIZE ||
        g->rom[pcir] != 'P' || g->rom[pcir+1] != 'C' ||
        g->rom[pcir+2] != 'I' || g->rom[pcir+3] != 'R' ||
        ((u32)g->rom[pcir+PCIR_VENDOR] | (u32)g->rom[pcir+PCIR_VENDOR+1] << 8) != PCI_VENDOR_NVIDIA ||
        ((u32)g->rom[pcir+PCIR_DEVICE] | (u32)g->rom[pcir+PCIR_DEVICE+1] << 8) != PCI_DEVICE_NV44A) return -1;
    u8 sum = 0;
    for (u32 i = 0; i < g->rom_size; i++) sum += g->rom[i];
    return sum || *chip != GF_VERSION_CHIP ? -1 : gfinit_decode_rom(g->rom, g->rom_size, &g->profile, g->decoded);
}
