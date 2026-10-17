/* KMOD v1: bounded, read-only validation of the container and body offsets. */
#include "module_image.h"
#include "../lib/crc32_core.inc"

STATIC_ASSERT(sizeof(u32) == 4, module_u32_is_four_bytes);
STATIC_ASSERT(MI_WORDS * 4 == MODULE_HEADER_SIZE, module_header_words);

static const struct module_component_policy components[] = MODULE_COMPONENT_POLICIES;

static const struct module_component_policy *component_policy(u32 id)
{
    for (u32 i = 0; i < sizeof(components) / sizeof(components[0]); i++)
        if (components[i].module_id == id) return &components[i];
    return 0;
}

static u32 get32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u16 get16(const u8 *p)
{
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static int within(u32 off, u32 len, u32 limit)
{
    return off <= limit && len <= limit - off;
}

/* Zero-length intervals do not overlap; subtraction avoids endpoint wrap. */
static int overlap(u32 a, u32 an, u32 b, u32 bn)
{
    if (!an || !bn) return 0;
    return a <= b ? b - a < an : a - b < bn;
}

static int in_text(u32 off, const u32 *h)
{
    return off >= h[MI_TEXT_OFF] && off - h[MI_TEXT_OFF] < h[MI_TEXT_SIZE];
}

int module_image_header(const u8 *header, u32 header_bytes, u32 file_size,
                        const struct module_image_policy *policy,
                        struct module_image_plan *out)
{
    struct module_image_plan plan;
    u32 *h = plan.word;
    u32 table_off[4], table_len[4];
    static const u32 record_size[3] = {4, MODULE_RECORD_SIZE, MODULE_RECORD_SIZE};
    if (!header || !policy || header_bytes < MODULE_HEADER_SIZE)
        return MODULE_BAD_HEADER;
    const struct module_component_policy *component = component_policy(policy->module_id);
    if (!component) return MODULE_BAD_HEADER;
    const u32 count_max[3] = {
        component->reloc_max, component->import_max, component->export_max
    };
    for (u32 i = 0; i < MI_WORDS; i++) h[i] = get32(header + 4 * i);
    if (h[MI_MAGIC] != MODULE_MAGIC || h[MI_FORMAT] != MODULE_FORMAT_VERSION ||
        h[MI_HEADER_SIZE] != MODULE_HEADER_SIZE || h[MI_IMAGE_SIZE] != file_size ||
        file_size > component->image_max || h[MI_ID] != component->module_id ||
        h[MI_PHASE] != component->phase ||
        (h[MI_FLAGS] & component->required_flags) != component->required_flags ||
        (h[MI_FLAGS] & ~component->allowed_flags) ||
        (!component->stop_allowed && h[MI_STOP]) ||
        h[MI_RESERVED0] != 0 || h[MI_RESERVED1] != 0)
        return MODULE_BAD_HEADER;
    if (h[MI_PAYLOAD_OFF] < MODULE_PAGE_SIZE ||
        h[MI_PAYLOAD_OFF] % MODULE_PAGE_SIZE ||
        h[MI_PAYLOAD_OFF] > file_size ||
        h[MI_PAYLOAD_SIZE] != file_size - h[MI_PAYLOAD_OFF] ||
        h[MI_MEM_SIZE] % MODULE_PAGE_SIZE || h[MI_MEM_SIZE] > component->mem_max ||
        h[MI_PAYLOAD_SIZE] > h[MI_MEM_SIZE] ||
        h[MI_MEM_SIZE] > 0xffffffffUL - h[MI_PAYLOAD_OFF])
        return MODULE_BAD_HEADER;
    plan.load_span = h[MI_PAYLOAD_OFF] + h[MI_MEM_SIZE];
    if (plan.load_span > component->load_span_max) return MODULE_BAD_HEADER;
    for (u32 i = 0; i < 4; i++) {
        u32 off = h[MI_TEXT_OFF + 2 * i], len = h[MI_TEXT_SIZE + 2 * i];
        if (!within(off, len, h[MI_MEM_SIZE]) ||
            (i < 3 && !within(off, len, h[MI_PAYLOAD_SIZE])) ||
            (i == 3 && off < h[MI_PAYLOAD_SIZE]))
            return MODULE_BAD_HEADER;
        for (u32 j = 0; j < i; j++)
            if (overlap(off, len, h[MI_TEXT_OFF + 2 * j], h[MI_TEXT_SIZE + 2 * j]))
                return MODULE_BAD_HEADER;
    }
    if (!in_text(h[MI_INIT], h)) return MODULE_BAD_HEADER;
    if (h[MI_STOP] && !in_text(h[MI_STOP], h)) return MODULE_BAD_HEADER;
    for (u32 i = 0; i < 4; i++) {
        u32 off = h[MI_RELOC_OFF + 2 * i], count = h[MI_RELOC_COUNT + 2 * i];
        if (off < MODULE_HEADER_SIZE || off > h[MI_PAYLOAD_OFF])
            return MODULE_BAD_HEADER;
        if (i < 3) {
            if (count > count_max[i] || count > (h[MI_PAYLOAD_OFF] - off) / record_size[i])
                return MODULE_BAD_HEADER;
            table_len[i] = count * record_size[i];
        } else {
            if (count > h[MI_PAYLOAD_OFF] - off) return MODULE_BAD_HEADER;
            table_len[i] = count;
        }
        table_off[i] = off;
    }
    /* Four fixed intervals: insertion sort, then compare nonempty neighbors. */
    for (u32 i = 1; i < 4; i++) {
        u32 off = table_off[i], len = table_len[i], j = i;
        while (j && table_off[j - 1] > off) {
            table_off[j] = table_off[j - 1]; table_len[j] = table_len[j - 1]; j--;
        }
        table_off[j] = off; table_len[j] = len;
    }
    u32 end = MODULE_HEADER_SIZE;
    for (u32 i = 0; i < 4; i++) {
        if (!table_len[i]) continue;
        if (table_off[i] < end) return MODULE_BAD_HEADER;
        end = table_off[i] + table_len[i];
    }
    if (h[MI_KAPI] != policy->kapi_abi_gen || h[MI_LAYOUT] != policy->layout_gen ||
        h[MI_ABI] != policy->module_abi_gen || h[MI_OPS_VERSION] != policy->ops_version ||
        h[MI_OPS_VERSION] != component->ops_version ||
        h[MI_IMPORT_COUNT] != policy->import_count)
        return MODULE_ABI_MISMATCH;
    if (h[MI_FLAGS] & MODULE_FLAG_PAIR) {
        if (!policy->img_valid) return MODULE_NO_IMAGE_CRC;
        if (h[MI_PAIR_CRC] != policy->pair_image_crc) return MODULE_PAIR_MISMATCH;
    }
    if (out) *out = plan;
    return MODULE_IMAGE_OK;
}

/* Consume exactly one bounded NUL-terminated name, never prefix padding. */
static int check_name(const u8 *names, u32 size, u32 *pos, u32 expected)
{
    u32 start = *pos, hash = 2166136261UL;
    while (*pos < size && *pos - start <= MODULE_NAME_MAX) {
        u8 ch = names[(*pos)++];
        if (!ch) return *pos - start > 1 && hash == expected;
        hash = (hash ^ ch) * 16777619UL;
    }
    return 0;
}

int module_image_validate(const u8 *image, u32 size,
                          const struct module_image_policy *policy,
                          struct module_image_plan *out)
{
    struct module_image_plan plan;
    int rc = module_image_header(image, size, size, policy, &plan);
    if (rc) return rc;
    const struct module_component_policy *component = component_policy(policy->module_id);
    const u32 *h = plan.word;
    const u8 zero[4] = {0, 0, 0, 0};
    u32 crc_off = MI_IMAGE_CRC * 4;
    u32 crc = crc32_core_update(CRC32_INIT, image, crc_off);
    crc = crc32_core_update(crc, zero, sizeof(zero));
    crc = crc32_core_update(crc, image + crc_off + 4, size - crc_off - 4);
    if (crc32_core_final(crc) != h[MI_IMAGE_CRC]) return MODULE_BAD_CRC;
    if (h[MI_IMPORT_COUNT] && !policy->imports) return MODULE_IMPORT_INVALID;
    u32 name_pos = 0;
    const u8 *names = image + h[MI_NAMES_OFF];
    for (u32 i = 0; i < h[MI_IMPORT_COUNT]; i++) {
        const u8 *rec = image + h[MI_IMPORT_OFF] + i * MODULE_RECORD_SIZE;
        const struct module_symbol_contract *want = &policy->imports[i];
        u32 slot = get32(rec + 8), hash = get32(rec);
        if (get16(rec + 6) < 1 || get16(rec + 6) > 5 ||
            get16(rec + 6) != want->kind || get16(rec + 4) != want->version ||
            !want->version || hash != want->name_hash ||
            !check_name(names, h[MI_NAMES_SIZE], &name_pos, hash) ||
            slot % 4 || slot < h[MI_DATA_OFF] ||
            !within(slot - h[MI_DATA_OFF], 4, h[MI_DATA_SIZE]))
            return MODULE_IMPORT_INVALID;
        for (u32 j = 0; j < i; j++) {
            const u8 *prev = image + h[MI_IMPORT_OFF] + j * MODULE_RECORD_SIZE;
            if (slot == get32(prev + 8) || hash == get32(prev)) return MODULE_IMPORT_INVALID;
        }
    }
    u32 seen = 0;
    for (u32 i = 0; i < h[MI_EXPORT_COUNT]; i++) {
        const u8 *rec = image + h[MI_EXPORT_OFF] + i * MODULE_RECORD_SIZE;
        u32 slot = get16(rec + 4), hash = get32(rec);
        if (slot < 1 || slot > component->ops_slots || (seen & (1U << slot)) ||
            !in_text(get32(rec + 8), h) || !policy->exports)
            return MODULE_BAD_HEADER;
        const struct module_symbol_contract *want = &policy->exports[slot - 1];
        if (!want->version || get16(rec + 6) != want->version || hash != want->name_hash ||
            !check_name(names, h[MI_NAMES_SIZE], &name_pos, hash)) return MODULE_BAD_HEADER;
        for (u32 j = 0; j < i; j++)
            if (hash == get32(image + h[MI_EXPORT_OFF] + j * MODULE_RECORD_SIZE))
                return MODULE_BAD_HEADER;
        seen |= 1U << slot;
    }
    if (name_pos != h[MI_NAMES_SIZE]) return MODULE_BAD_HEADER;
    /* Generated tables are sorted: a high-water mark makes those linear.
     * For unsorted input compare earlier intervals, preserving immutable input
     * and bounded stack use. Counts are capped in header validation. */
    u32 reloc_end = 0;
    for (u32 i = 0; i < h[MI_RELOC_COUNT]; i++) {
        u32 off = get32(image + h[MI_RELOC_OFF] + 4 * i);
        if (!within(off, 4, h[MI_PAYLOAD_SIZE]) ||
            get32(image + h[MI_PAYLOAD_OFF] + off) > h[MI_MEM_SIZE])
            return MODULE_RELOC_INVALID;
        if (off < reloc_end)
            for (u32 j = 0; j < i; j++) {
                u32 prev = get32(image + h[MI_RELOC_OFF] + 4 * j);
                if (overlap(off, 4, prev, 4)) return MODULE_RELOC_INVALID;
            }
        if (off + 4 > reloc_end) reloc_end = off + 4;
        for (u32 j = 0; j < h[MI_IMPORT_COUNT]; j++) {
            u32 slot = get32(image + h[MI_IMPORT_OFF] + j * MODULE_RECORD_SIZE + 8);
            if (overlap(off, 4, slot, 4)) return MODULE_RELOC_INVALID;
        }
    }
    if (out) *out = plan;
    return MODULE_IMAGE_OK;
}

/* Validate every write and base addition before changing even BSS. The owned
 * image is stable in OS32's cooperative loader; no prefix pointers escape. */
int module_image_relocate(u8 *image, u32 size,
                          const struct module_image_policy *policy, u32 base,
                          struct module_image_plan *out)
{
    struct module_image_plan plan;
    int rc = module_image_validate(image, size, policy, &plan);
    if (rc) return rc;
    const u32 *h = plan.word;
    if (base > 0xffffffffUL - h[MI_MEM_SIZE]) return MODULE_RELOC_INVALID;
    u8 *body = image + h[MI_PAYLOAD_OFF];
    for (u32 i = h[MI_PAYLOAD_SIZE]; i < h[MI_MEM_SIZE]; i++) body[i] = 0;
    for (u32 i = 0; i < h[MI_RELOC_COUNT]; i++) {
        u32 off = get32(image + h[MI_RELOC_OFF] + 4 * i);
        u32 value = base + get32(body + off);
        for (u32 j = 0; j < 4; j++) body[off + j] = (u8)(value >> (8 * j));
    }
    if (out) *out = plan;
    return MODULE_IMAGE_OK;
}
