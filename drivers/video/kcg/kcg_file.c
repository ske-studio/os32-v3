/* Resident KCG1 provider for machines without a font ROM. */
#include "kcg_file.h"
#include "../../../kernel/console/font_source.h"
#include "../../../kernel/console/font_guard.h"
#include "../../../mm/kmalloc.h"
#include "../../../lib/kstring.h"
#include "os32_kapi_shared.h"

#define KCG_FILE_HEADER 16u
#define KCG_FILE_KANJI_COUNT (94u * 94u)
#define KCG_FILE_KANJI_BYTES (KCG_FILE_KANJI_COUNT * 32u)
#define KCG_FILE_ANK_BYTES (256u * 16u)
#define KCG_FILE_ANK_OFFSET (KCG_FILE_KANJI_BYTES + KCG_FILE_KANJI_COUNT)
#define KCG_FILE_PAYLOAD (KCG_FILE_ANK_OFFSET + KCG_FILE_ANK_BYTES + 256u)

static u8 *resident;
static KcgCacheStats counters;

static u32 read_le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int load_guarded(void *arg)
{
    const char *path = arg;
    u32 id, size, i;
    u8 header[KCG_FILE_HEADER];
    u8 *data = 0;
    int rc = font_lease_resident_open(path, &id, &size);
    if (rc) return rc;
    rc = OS32_ERR_INVAL;
    if (size != KCG_FILE_HEADER + KCG_FILE_PAYLOAD ||
        font_lease_resident_read_at(id, 0, header, sizeof(header)) != (i32)sizeof(header) ||
        header[0] != 'K' || header[1] != 'C' || header[2] != 'G' || header[3] != '1' ||
        read_le32(header + 4) != KCG_FILE_PAYLOAD || read_le32(header + 8) != 0 ||
        read_le32(header + 12) != 0) goto done;
    data = kmalloc(KCG_FILE_PAYLOAD);
    if (!data) { rc = OS32_ERR_NOSPC; goto done; }
    if (font_lease_resident_read_at(id, KCG_FILE_HEADER, data, KCG_FILE_PAYLOAD) != (i32)KCG_FILE_PAYLOAD ||
        font_lease_resident_validate(id)) goto done;
    for (i = 0; i < KCG_FILE_KANJI_COUNT; i++)
        if (data[KCG_FILE_KANJI_BYTES + i] != 1) goto done;
    for (i = 0; i < 256; i++)
        if (data[KCG_FILE_ANK_OFFSET + KCG_FILE_ANK_BYTES + i] != 1) goto done;
    rc = font_lease_resident_close(id);
    if (rc) { kfree(data); return rc; }
    if (resident) kfree(resident);
    resident = data;
    counters.hits = counters.misses = 0;
    return 0;
done:
    if (data) kfree(data);
    (void)font_lease_resident_close(id);
    return rc;
}

int kcg_file_load(const char *path)
{
    if (!path) return OS32_ERR_INVAL;
    return font_guard_run(load_guarded, (void *)path);
}

void kcg_file_unload(void)
{
    u8 *old = resident;
    resident = 0;
    if (old) kfree(old);
}

static void read_ank(u8 ch, u8 *buf)
{
    if (!resident) { kmemset(buf, 0, 16); counters.misses++; return; }
    kmemcpy(buf, resident + KCG_FILE_ANK_OFFSET + (u32)ch * 16u, 16);
    counters.hits++;
}

static void read_kanji(u16 jis, u8 *buf)
{
    u32 hi = jis >> 8, lo = jis & 255u;
    if (!resident || hi < 0x21 || hi > 0x7e || lo < 0x21 || lo > 0x7e) {
        kmemset(buf, 0, 32); counters.misses++; return;
    }
    kmemcpy(buf, resident + ((hi - 0x21) * 94u + lo - 0x21) * 32u, 32);
    counters.hits++;
}

static void stats(KcgCacheStats *out) { *out = counters; }
static void invalidate(void) { counters.hits = counters.misses = 0; }
const KcgModuleApi kcg_file_api = { read_ank, read_kanji, stats, invalidate };
