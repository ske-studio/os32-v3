/* /sys/unicode.bin: headerless LE (u16 Unicode, u16 JIS) pairs. */
#include "unicode_file.h"
#include "config.h"
#include "endian_le.h"
#include "memmap.h"
#include "pgalloc.h"
#include "vfs.h"
#include "kprintf.h"

STATIC_ASSERT((UNICODE_FILE_BYTES + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE ==
              UNICODE_FILE_PAGES, unicode_file_page_count);

static const u8 *unicode_pairs;

void unicode_file_load(void)
{
    OS32_Stat st;
    u32 phys = 0;
    const char *reason;
    int rc = vfs_stat(SYS_UNICODE_BIN, &st);
    if (rc < 0) {
        reason = rc == VFS_ERR_NOTFOUND ? "missing" : "io";
        goto unavailable;
    }
    if (st.st_size != UNICODE_FILE_BYTES) {
        reason = "size";
        goto unavailable;
    }
    phys = pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, UNICODE_FILE_PAGES);
    if (!phys) {
        reason = "nomem";
        goto unavailable;
    }
    u8 *pairs = P2V(phys);
    rc = vfs_read(SYS_UNICODE_BIN, pairs, UNICODE_FILE_BYTES);
    if (rc != UNICODE_FILE_BYTES) {
        reason = rc < 0 ? "io" : "size";
        goto unavailable;
    }
    for (u32 i = 1; i < UNICODE_FILE_PAIRS; i++) {
        if (le16_rd(pairs + (i - 1) * UNICODE_FILE_PAIR_BYTES) >=
            le16_rd(pairs + i * UNICODE_FILE_PAIR_BYTES)) {
            reason = "order";
            goto unavailable;
        }
    }
    unicode_pairs = pairs;
    kprintf(0x07, "[unicode] loaded pairs=%u pages=%u pfn=%u\n",
            (u32)UNICODE_FILE_PAIRS, (u32)UNICODE_FILE_PAGES, phys / MEM_PAGE_SIZE);
    return;

unavailable:
    if (phys)
        pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, phys / MEM_PAGE_SIZE,
                             UNICODE_FILE_PAGES);
    kprintf(0x07, "[unicode] unavailable reason=%s; kana-only\n", reason);
}

u16 unicode_file_lookup(u32 cp)
{
    /* Preserve lib/utf8.c arithmetic even when the file is unavailable. */
    if (cp >= 0x3041 && cp <= 0x3093)
        return (u16)(0x2421 + (cp - 0x3041));
    if (cp >= 0x30A1 && cp <= 0x30F6)
        return (u16)(0x2521 + (cp - 0x30A1));
    if (cp > 0xFFFF) return 0;
    if (!unicode_pairs) return 0;

    u32 lo = 0, hi = UNICODE_FILE_PAIRS;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        const u8 *pair = unicode_pairs + mid * UNICODE_FILE_PAIR_BYTES;
        u16 unicode = le16_rd(pair);
        if (cp < unicode)
            hi = mid;
        else if (cp > unicode)
            lo = mid + 1;
        else
            return le16_rd(pair + 2);
    }
    return 0;
}

/* Private kdebug import: inspect ownership without publishing a user view. */
u32 unicode_file_debug_phys(void)
{
    return unicode_pairs ? V2P(unicode_pairs) : 0;
}
