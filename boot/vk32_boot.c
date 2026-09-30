/* ======================================================================== */
/*  VK32_BOOT.C — vmkernel.lz4 (VK32 v2) の検査と展開 (HDD ローダ)          */
/*                                                                          */
/*  票: docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md 部品 A-4                    */
/*  形式: boot/boot_defs.h の「VK32 ヘッダ (v2)」                          */
/*  試験: tools/tests/test_vk32_crc.py (この C と FD の ASM に同じ壊れた    */
/*        イメージを渡し、同じ VK32_ERR_* で断ることを見る)                  */
/*                                                                          */
/*  検査の順序 (FD の pm_vk32_boot と同じ):                                 */
/*    1. 長さ (共通部 16B 以上・MAX_IMAGE_SIZE 以下)                         */
/*    2. magic / version / entry_count (1〜VK32_MAX_ENTRIES)                */
/*    3. header_size == VK32_HEADER_SIZE(n) かつ <= ファイル長              */
/*    4. image_size == ファイル長 (完全長)                                  */
/*    5. ファイル全体の CRC32 (image_crc の欄を 0 として計算)               */
/*    6. 全エントリの範囲 (展開の前に全部): data_offset >= header_size、     */
/*       data_offset + compressed_size <= ファイル長、展開先が              */
/*       [VK32_LOAD_MIN, VK32_LOAD_END) に収まる、raw_size != 0、           */
/*       エントリ同士が重ならない                                           */
/*    7. エントリごとに展開 → decoded == raw_size → 展開後の CRC32          */
/*  足し算は桁あふれしない形 (差で比べる) で書く。                          */
/* ======================================================================== */

#include "boot_defs.h"
#include "../lib/crc32_core.inc"

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* ファイル全体の CRC32。[crc_off, crc_off + 4) は 0 として送る。 */
static u32 vk32_file_crc(const u8 *file, u32 size, u32 crc_off)
{
    static const u8 zero4[4] = { 0, 0, 0, 0 };
    u32 s = CRC32_INIT;

    s = crc32_core_update(s, file, crc_off);
    s = crc32_core_update(s, zero4, 4);
    s = crc32_core_update(s, file + crc_off + 4, size - crc_off - 4);
    return crc32_core_final(s);
}

int vk32_boot(const u8 *file, u32 file_size, u8 *window, u32 *out_crc)
{
    u32 n, hsz, i, j;
    const u8 *ent;
    u32 crc_off;

    if (!file || !window || !out_crc) return VK32_ERR_SIZE;

    /* 1. 長さ */
    if (file_size < VK32_COMMON_SIZE || file_size > (u32)MAX_IMAGE_SIZE)
        return VK32_ERR_SIZE;

    /* 2. 共通部 */
    if (rd32(file) != VK32_MAGIC) return VK32_ERR_MAGIC;
    if (rd32(file + 8) != VK32_VERSION) return VK32_ERR_VERSION;
    n = rd32(file + 12);
    if (n < 1 || n > (u32)VK32_MAX_ENTRIES) return VK32_ERR_COUNT;

    /* 3. header_size (n <= 4 なので VK32_HEADER_SIZE は溢れない) */
    hsz = rd32(file + 4);
    if (hsz != VK32_HEADER_SIZE(n) || hsz > file_size) return VK32_ERR_HEADER;

    /* 4. 完全長 */
    if (rd32(file + VK32_OFF_IMAGE_SIZE(n)) != file_size) return VK32_ERR_LENGTH;

    /* 5. ファイル全体の CRC32 */
    crc_off = VK32_OFF_IMAGE_CRC(n);
    if (vk32_file_crc(file, file_size, crc_off) != rd32(file + crc_off))
        return VK32_ERR_FILE_CRC;

    /* 6. 範囲 (展開の前に全エントリ) */
    for (i = 0; i < n; i++) {
        u32 addr, raw, off, csz;

        ent  = file + VK32_COMMON_SIZE + i * VK32_ENTRY_SIZE;
        addr = rd32(ent + 0);
        raw  = rd32(ent + 4);
        off  = rd32(ent + 8);
        csz  = rd32(ent + 12);

        if (off < hsz || off > file_size || csz > file_size - off)
            return VK32_ERR_SRC;
        if (raw == 0 || addr < VK32_LOAD_MIN || addr >= VK32_LOAD_END ||
            raw > VK32_LOAD_END - addr)
            return VK32_ERR_DST;
        /* 読み込み域 (0x10000〜) は帯の下なので重ならない。エントリ同士だけ見る */
        for (j = 0; j < i; j++) {
            const u8 *e2 = file + VK32_COMMON_SIZE + j * VK32_ENTRY_SIZE;
            u32 a2 = rd32(e2 + 0);
            u32 r2 = rd32(e2 + 4);
            if (addr < a2 + r2 && a2 < addr + raw) return VK32_ERR_DST;
        }
    }

    /* 7. 展開と展開後の CRC32 */
    for (i = 0; i < n; i++) {
        u32 addr, raw, off, csz;
        u8 *dst;
        int decoded;

        ent  = file + VK32_COMMON_SIZE + i * VK32_ENTRY_SIZE;
        addr = rd32(ent + 0);
        raw  = rd32(ent + 4);
        off  = rd32(ent + 8);
        csz  = rd32(ent + 12);
        dst  = window + (addr - VK32_LOAD_MIN);

        decoded = boot_lz4_decode(file + off, (int)csz, dst, (int)raw);
        if (decoded < 0) return VK32_ERR_DECODE;
        if ((u32)decoded != raw) return VK32_ERR_RAW_SIZE;
        if (crc32_core_final(crc32_core_update(CRC32_INIT, dst, raw)) !=
            rd32(file + VK32_OFF_ENTRY_CRC(n) + i * 4))
            return VK32_ERR_ENTRY_CRC;
    }

    *out_crc = rd32(file + crc_off);
    return VK32_OK;
}

const char *vk32_strerror(int rc)
{
    switch (rc) {
    case VK32_ERR_SIZE:      return "VK32: bad file size";
    case VK32_ERR_MAGIC:     return "VK32: bad magic";
    case VK32_ERR_VERSION:   return "VK32: unknown version (need 2)";
    case VK32_ERR_COUNT:     return "VK32: bad entry count";
    case VK32_ERR_HEADER:    return "VK32: bad header size";
    case VK32_ERR_LENGTH:    return "VK32: file truncated/length mismatch";
    case VK32_ERR_FILE_CRC:  return "VK32: image CRC mismatch";
    case VK32_ERR_SRC:       return "VK32: entry data outside file";
    case VK32_ERR_DST:       return "VK32: load address out of range";
    case VK32_ERR_DECODE:    return "VK32: LZ4 decode FAIL";
    case VK32_ERR_RAW_SIZE:  return "VK32: decoded size mismatch";
    case VK32_ERR_ENTRY_CRC: return "VK32: entry CRC mismatch";
    default:                 return "VK32: error";
    }
}
