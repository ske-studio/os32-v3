#include "input_policy.h"
#include "os32_kapi_shared.h"

static u16 read16(const u8 *p)
{
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 read32(const u8 *p)
{
    return (u32)read16(p) | ((u32)read16(p + 2) << 16);
}

int ikm_wire_header(const u8 *wire, u32 length, u32 *count)
{
    u32 n;
    if (!wire || !count || length < IKM_HEADER_SIZE ||
        wire[0] != 'I' || wire[1] != 'K' || wire[2] != 'M' || wire[3] != '1' ||
        read16(wire + 4) != 2 || read16(wire + 6) != IKM_ROW_SIZE ||
        read32(wire + 12) != 0)
        return OS32_ERR_INVAL;
    n = read32(wire + 8);
    if (n > IKM_MAX_ROWS || length != IKM_HEADER_SIZE + n * IKM_ROW_SIZE)
        return OS32_ERR_INVAL;
    *count = n;
    return 0;
}

int ikm_wire_row(const u8 *wire, u32 length, u32 index, IkmRow *row)
{
    u32 count;
    const u8 *p;
    IkmRow decoded;
    if (!row || ikm_wire_header(wire, length, &count) || index >= count)
        return OS32_ERR_INVAL;
    p = wire + IKM_HEADER_SIZE + index * IKM_ROW_SIZE;
    decoded.kind = p[0]; decoded.profile = p[1];
    decoded.context = p[2]; decoded.flags = p[3];
    decoded.index = read16(p + 4); decoded.selector = read16(p + 6);
    decoded.value = read32(p + 8); decoded.aux = read32(p + 12);
    *row = decoded;
    return 0;
}

void ikm_check_init(IkmCheck *check)
{
    u32 i;
    if (!check) return;
    for (i = 0; i < IKM_SEEN_BYTES; i++) check->bits[i] = 0;
    for (i = 0; i < sizeof(check->term_bytes); i++) check->term_bytes[i] = 0;
    check->term_ext_count = 0;
}

static int valid_map(u8 platform, u16 map)
{
    return map == 2 || (map == 1 && platform == IKM_PLATFORM_PC98);
}

static int valid_sym(u32 sym)
{
    return sym <= 0xffff && !(sym >= 0xd800 && sym <= 0xdfff) &&
        sym < 0xfffe && !(sym >= 0x0300 && sym <= 0x036f);
}

int ikm_check_row(IkmCheck *check, u8 platform, const IkmRow *r)
{
    u32 key, op, slot, group;
    if (!check || !r || (platform != IKM_PLATFORM_PC98 && platform != IKM_PLATFORM_MAC) || r->flags)
        return OS32_ERR_INVAL;
    if (r->kind == 1 || r->kind == 2) {
        if (r->profile || !valid_map(platform, r->context)) return OS32_ERR_INVAL;
        if (r->kind == 1) {
            if (r->index > 1 || r->selector || r->aux ||
                (r->index == 0 && r->value != (r->context == 1 ? 128u : 256u)) ||
                (r->index == 1 && r->value != 0)) return OS32_ERR_INVAL;
            key = (r->context - 1) * 2 + r->index;
        } else {
            if (r->index >= (r->context == 1 ? 128 : 256) || r->selector > 4 ||
                r->aux || !valid_sym(r->value)) return OS32_ERR_INVAL;
            key = 4 + (r->context == 1 ? 0 : 640) + r->index * 5 + r->selector;
        }
    } else if (r->kind == 3 || r->kind == 7) {
        if (r->profile || r->context != 1 || platform != IKM_PLATFORM_PC98 || r->index >= 128)
            return OS32_ERR_INVAL;
        if (r->kind == 3) {
            op = (r->aux >> 8) & 255;
            if (r->selector > 15 || (r->aux & 0xffff0000u) || (r->aux & 255) > 1 ||
                !valid_sym(r->value)) return OS32_ERR_INVAL;
            if ((r->aux & 255) == 0) { if (op) return OS32_ERR_INVAL; }
            else if (r->value != 0xf500 || op == 1 || (op >= 0x7b && op <= 0x7f) ||
                     op > 0xa3 || (op >= 0x89 && op <= 0x97)) return OS32_ERR_INVAL;
            key = 1924 + r->index * 16 + r->selector;
        } else {
            if (r->selector || r->value > 3 || r->aux) return OS32_ERR_INVAL;
            key = 3972 + r->index;
        }
    } else {
        if (r->kind < 5 || r->kind > 8 || r->kind == 7 || r->context > 4 ||
            r->profile < (platform == IKM_PLATFORM_PC98 ? 1 : 3) ||
            r->profile > (platform == IKM_PLATFORM_PC98 ? 2 : 4)) return OS32_ERR_INVAL;
        group = (r->profile - 1) * 5 + r->context;
        if (r->kind == 5) {
            if (!r->index || r->index > 8 || r->selector || r->value > 255 || r->aux)
                return OS32_ERR_INVAL;
            key = 4100 + group * 8 + r->index - 1;
        } else if (r->kind == 6) {
            if (r->index >= 128 || r->aux) return OS32_ERR_INVAL;
            if (r->selector == 0xffff) {
                if (r->context || r->value > 255) return OS32_ERR_INVAL;
                slot = 16;
            } else {
                if ((r->selector & ~0x17u) || r->value > 18) return OS32_ERR_INVAL;
                slot = (r->selector & 7) | ((r->selector & 0x10) >> 1);
            }
            key = 4260 + (group * 128 + r->index) * 17 + slot;
        } else {
            if (!valid_map(platform, r->selector) ||
                r->index >= (r->selector == 1 ? 128 : 256) || r->value > 18 || (r->aux & ~0x17u))
                return OS32_ERR_INVAL;
            slot = (r->aux & 7) | ((r->aux & 0x10) >> 1);
            key = 47780 + (group * 384 + (r->selector == 1 ? 0 : 128) + r->index) * 16 + slot;
        }
        if ((r->kind == 5 || r->kind == 6) && check->term_ext_count >= 128)
            return OS32_ERR_INVAL;
    }
    if (key >= IKM_SEEN_BITS || (check->bits[key / 8] & (1u << (key % 8))))
        return OS32_ERR_INVAL;
    if (r->kind == 5 && (check->term_bytes[group * 32 + r->value / 8] & (1u << (r->value % 8))))
        return OS32_ERR_INVAL;
    check->bits[key / 8] |= (u8)(1u << (key % 8));
    if (r->kind == 5) check->term_bytes[group * 32 + r->value / 8] |= (u8)(1u << (r->value % 8));
    if (r->kind == 5 || r->kind == 6) check->term_ext_count++;
    return 0;
}

/* Rows include defaults and explicit overrides. A failed lookup leaves out alone. */
int ikm_term_lookup(const IkmRow *rows, u32 count, u8 profile, u8 context,
                    u16 index, u8 *value)
{
    const IkmRow *common = 0;
    if ((!rows && count) || !value || profile < 1 || profile > 4 ||
        context > 4 || index < 1 || index > 8 || count > IKM_MAX_ROWS)
        return OS32_ERR_INVAL;
    for (u32 i = 0; i < count; ++i) {
        const IkmRow *row = &rows[i];
        if (row->kind != 5 || row->profile != profile || row->index != index)
            continue;
        if (row->context == context && context != 0) {
            *value = (u8)row->value;
            return 0;
        }
        if (row->context == 0) common = row;
    }
    if (!common) return OS32_ERR_NOTFOUND;
    *value = (u8)common->value;
    return 0;
}
