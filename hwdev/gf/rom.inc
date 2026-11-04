/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
/* BIT i byte order: envytools nvbios/{info,bit}.c (MIT), version 2.
 * Header/checksum logic moved from kdebug/gfinit/rom.inc (P1).
 * Caller provides two identical GF_ROM_MAX-byte PROM reads. */
#define GF_VERSION_CHIP 0x44U
#define GF_CHIP_UNKNOWN 0x100U
#define GF_BIT_HEADER 12U
#define GF_BIT_ENTRY 6U
#define GF_BIT_CHIP_OFFSET 2U
#define GF_BIT_I_VERSION 2U
/* Shared BIT directory search: nouveau nvkm/subdev/bios/bit.c:29-55, init.c:359-407.
 * Return 0 found, 1 absent, -1 malformed. A found entry's entire payload is
 * checked once; consumers separately check their required length/version. */
static inline int gf_rom_bit_entry(const u8 *rom, u32 size, u8 id,
                                    u32 *data, u32 *length, u8 *version)
{
    static const u8 signature[] = {0xff, 0xb8, 'B', 'I', 'T', 0, 0};
    if (!rom || !data || !length || !version) return -1;
    for (u32 off = 0; off <= size && GF_BIT_HEADER <= size - off; off++) {
        u32 i = 0;
        while (i < sizeof(signature) && rom[off+i] == signature[i]) i++;
        if (i != sizeof(signature)) continue;
        u32 head = rom[off+8], stride = rom[off+9], count = rom[off+10];
        if (head < GF_BIT_HEADER || stride < GF_BIT_ENTRY ||
            head + count * stride > size - off) return -1;
        for (u32 n = 0; n < count; n++) {
            const u8 *entry = rom + off + head + n * stride;
            if (entry[0] != id) continue;
            u32 len = (u32)entry[2] | (u32)entry[3] << 8;
            u32 ptr = (u32)entry[4] | (u32)entry[5] << 8;
            if (ptr > size || len > size - ptr) return -1;
            *data = ptr; *length = len; *version = entry[1];
            return 0;
        }
        return 1;
    }
    return 1;
}
#ifndef GF_ROM_BIT_ONLY
static u32 gf_rom_chip(const u8 *rom, u32 size)
{
    u32 data, len;
    u8 version;
    if (gf_rom_bit_entry(rom, size, 'i', &data, &len, &version) ||
        version != GF_BIT_I_VERSION || len <= GF_BIT_CHIP_OFFSET)
        return GF_CHIP_UNKNOWN;
    return rom[data + GF_BIT_CHIP_OFFSET];
}
static int gf_rom_validate(const u8 *rom, u32 *len, u32 *crc, u32 *chip)
{
    if (rom[0] != PCI_ROM_SIG0 || rom[1] != PCI_ROM_SIG1 || !rom[PCI_ROM_SIZE_OFF]) return -1;
    *len = (u32)rom[PCI_ROM_SIZE_OFF] * PCI_ROM_SIZE_UNIT;
    if (*len > GF_ROM_MAX) return -1;
    *crc = crc32_core_final(crc32_core_update(CRC32_INIT, rom, *len));
    *chip = gf_rom_chip(rom, *len);
    u32 pcir = (u32)rom[PCI_ROM_PCIR_PTR] | (u32)rom[PCI_ROM_PCIR_PTR + 1] << 8;
    if (*len < PCIR_HEADER_SIZE || pcir > *len - PCIR_HEADER_SIZE ||
        rom[pcir] != 'P' || rom[pcir+1] != 'C' ||
        rom[pcir+2] != 'I' || rom[pcir+3] != 'R' ||
        ((u32)rom[pcir+PCIR_VENDOR] | (u32)rom[pcir+PCIR_VENDOR+1] << 8) != PCI_VENDOR_NVIDIA ||
        ((u32)rom[pcir+PCIR_DEVICE] | (u32)rom[pcir+PCIR_DEVICE+1] << 8) != PCI_DEVICE_NV44A) return -1;
    u8 sum = 0;
    for (u32 i = 0; i < *len; i++) sum += rom[i];
    return sum || *chip != GF_VERSION_CHIP ? -1 : 0;
}

#endif /* GF_ROM_BIT_ONLY */
