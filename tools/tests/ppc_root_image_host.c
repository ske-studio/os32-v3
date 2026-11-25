/* docs/design/platform/powermac/root_pair.md: only a complete verified decode
 * transfers the output pages. All earlier failures leave the caller no image. */
#include "sys/platform/powermac/root_boot.h"
#include "bootinfo.h"
#include "crc32.h"
#include "endian_le.h"
#include "pgalloc.h"

int pm_root_image(struct boot_root_image *, u32);
u8 __root_record_start[16], __root_image_start[] = {0x10, 'A', 0, 0};
static u8 output[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static int available, owned, allocated, freed, valid;

u32 pgalloc_limit_pfn(void) { return 0x10000; }
int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 flags, u32 *pfn)
{
    (void)first; (void)end; (void)flags;
    ++allocated;
    if (!available || owned || n != 1 || owner != LEDGER_OWNER_BOOT) return 0;
    *pfn = V2P(output) / PAGE_SIZE;
    owned = 1;
    return 1;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    ++freed;
    if (!owned || owner != LEDGER_OWNER_BOOT || pfn != V2P(output) / PAGE_SIZE || n != 1)
        return 0;
    owned = 0;
    return 1;
}
void boot_common_set(const struct boot_common *info, u8 source)
{
    (void)source;
    valid = info->image_valid;
}

int main(void)
{
    const struct { int alloc, packed, length, literal, rc; } cases[] = {
        {0, 2, 1, 'A', -2}, {1, 4, 8, 'A', -3}, {1, 2, 2, 'A', -3},
        {1, 2, 1, 'B', -4}, {1, 2, 1, 'A', 0},
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        struct boot_root_image image = {output, PAGE_SIZE};
        available = cases[i].alloc;
        valid = allocated = freed = owned = 0;
        __root_image_start[1] = cases[i].literal;
        le32_wr(__root_record_start + 4, crc32_calc((const u8 *)"A", 1));
        le32_wr(__root_record_start + 8, cases[i].packed);
        le32_wr(__root_record_start + 12, cases[i].length);
        int rc = pm_root_image(&image, sizeof(__root_image_start));
        int success = cases[i].rc == 0;
        if (rc != cases[i].rc || allocated != 1 || owned != success ||
            freed != (available && !success) || valid != success ||
            image.base != (success ? output : 0) || image.bytes != (u32)success)
            return 10 + i;
    }
    return 0;
}
