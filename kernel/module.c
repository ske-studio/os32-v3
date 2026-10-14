/* POST_ROOT debug module: validate privately, publish once, retain body only. */
#include "module.h"
#include "module_image.h"
#include "kdebug_imports.h"
#include "bootinfo.h"
#include "config.h"
#include "sysconfig.h"
#include "vfs.h"
#include "pgalloc.h"
#include "memmap.h"
#include "kstring.h"
#include "kprintf.h"
#include "os32_generations.h"

struct kdebug kdebug;
STATIC_ASSERT(sizeof(struct kdebug_ops) == (MODULE_OPS_SLOTS + 1) * 4,
              kdebug_ops_wire_size);
STATIC_ASSERT(MODULE_PAGE_SIZE == PAGE_SIZE, module_page_matches_allocator);

/* Unaligned reloc immediates are legal, including their runtime writes. */
static u32 read_word(const void *ptr)
{
    const u8 *p = ptr;
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static const char *reject_name(int state)
{
    static const char *const names[] = {
        "OFF", "RUNNING", "NOT_FOUND", "BAD_HEADER", "PAIR_MISMATCH",
        "NO_IMAGE_CRC", "ABI_MISMATCH", "NO_PAGES", "SHORT_READ",
        "BAD_CRC", "INIT_FAILED", "RELOC_INVALID", "IMPORT_INVALID"
    };
    return names[state];
}

void kdebug_load(void)
{
    u8 header[MODULE_HEADER_SIZE];
    struct module_image_plan plan;
    struct kdebug_ops candidate = {0};
    OS32_Stat st;
    u32 phys = 0, npages = 0;
    int fd, rc;
    if (kdebug.open_count ||
        sysconfig_get_int(SYS_SYSTEM_CFG, "KDEBUG", 0) != 1) return;
    kdebug.open_count++;
    fd = vfs_open(SYS_KDEBUG_BIN, O_RDONLY);
    rc = KDEBUG_NOT_FOUND;
    if (fd < 0) goto rejected;
    rc = KDEBUG_SHORT_READ;
    if (vfs_fstat(fd, &st) < 0 ||
        vfs_read_fd(fd, header, sizeof(header)) != (int)sizeof(header)) goto rejected;
    kdebug.pair_crc = read_word(header + MI_PAIR_CRC * 4);
    const struct bootinfo *bi = bootinfo_get();
    const struct module_image_policy policy = {
        KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, KDEBUG_ABI_GEN,
        bi->img_crc, KDEBUG_OPS_VERSION, bi->img_valid, KDEBUG_IMPORT_COUNT,
        kdebug_import_contracts, kdebug_export_contracts
    };
    rc = module_image_header(header, sizeof(header), st.st_size, &policy, &plan);
    if (rc) goto rejected;
    rc = KDEBUG_NO_PAGES;
    if (!ledger_owner_new(LEDGER_KIND_MODULE, MODULE_ID_KDEBUG, "kdebug",
                          &kdebug.owner)) goto rejected;
    npages = plan.load_span / PAGE_SIZE;
    phys = pgalloc_alloc_phys(kdebug.owner, npages);
    if (!phys) goto rejected;
    u8 *image = P2V(phys);
    kmemcpy(image, header, sizeof(header));
    rc = KDEBUG_SHORT_READ;
    if (vfs_read_fd(fd, image + sizeof(header), st.st_size - sizeof(header)) !=
        (int)(st.st_size - sizeof(header))) goto rejected;
    vfs_close(fd);
    fd = -1;
    rc = module_image_validate(image, st.st_size, &policy, &plan);
    if (rc) goto rejected;
    const u32 *h = plan.word;
    u8 *body = image + h[MI_PAYLOAD_OFF];
    u32 base = (u32)(uptr)body;   /* VA of the body (P2V above) [C5] */
    kmemset(body + h[MI_PAYLOAD_SIZE], 0, h[MI_MEM_SIZE] - h[MI_PAYLOAD_SIZE]);
    for (u32 i = 0; i < h[MI_RELOC_COUNT]; i++) {
        u32 off = read_word(image + h[MI_RELOC_OFF] + i * 4);
        rc = MODULE_RELOC_INVALID;
        if (off > h[MI_PAYLOAD_SIZE] || 4 > h[MI_PAYLOAD_SIZE] - off)
            goto rejected;
        u32 value = read_word(body + off);
        if (value > h[MI_MEM_SIZE]) goto rejected;
        value += base;
        kmemcpy(body + off, &value, sizeof(value));
    }
    for (u32 i = 0; i < h[MI_IMPORT_COUNT]; i++) {
        u32 off = read_word(image + h[MI_IMPORT_OFF] + i * MODULE_RECORD_SIZE + 8);
        kmemcpy(body + off, &kdebug_kexports[i], sizeof(u32));
    }
    candidate.version = h[MI_OPS_VERSION];
    for (u32 i = 0; i < h[MI_EXPORT_COUNT]; i++) {
        const u8 *rec = image + h[MI_EXPORT_OFF] + i * MODULE_RECORD_SIZE;
        u32 slot = (u32)rec[4] | ((u32)rec[5] << 8);
        u32 entry = base + read_word(rec + 8);
        kmemcpy((u8 *)&candidate + slot * 4, &entry, sizeof(entry));
    }
    /* init may only fill its candidate. Require exactly the validated exports. */
    struct kdebug_ops expected = candidate;
    int (*init)(struct kdebug_ops *) = (int (*)(struct kdebug_ops *))(body + h[MI_INIT]);
    rc = KDEBUG_INIT_FAILED;
    if (init(&candidate) || memcmp(&candidate, &expected, sizeof(candidate)))
        goto rejected;
    kdebug.ops = candidate;
    pgalloc_free_n_owner(kdebug.owner, phys / PAGE_SIZE, h[MI_PAYLOAD_OFF] / PAGE_SIZE);
    kdebug.base = base;
    kdebug.pages = h[MI_MEM_SIZE] / PAGE_SIZE;
    kdebug.state = KDEBUG_RUNNING;
    return;

rejected:
    if (fd >= 0) vfs_close(fd);
    if (phys) pgalloc_free_n_owner(kdebug.owner, phys / PAGE_SIZE, npages);
    if (kdebug.owner) ledger_owner_retire(kdebug.owner);
    /* Keep the retired owner number for diagnosis; ops/base/pages remain zero. */
    kdebug.state = rc;
    kprintf(0x07, "[kdebug] rejected: %s\n", reject_name(rc));
}
