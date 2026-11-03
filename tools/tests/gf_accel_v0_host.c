/* Link hwdev/gf/vram_view.c only, no libc. V0 needs only map_ops stubs;
 * reuse A1's prescribed gf_command_runtime_host.h assertion/entry substrate.
 * No MMIO, push or clock dependency is needed for this component. */
#include "gf_command_runtime_host.h"
#include "../../hwdev/gf/accel.h"

#define PAGE MEM_PAGE_SIZE
#define SURFACE_OFF (GF2_PUSH_MIN_BASE + PAGE)
#define WINDOW_BYTES (2U * PAGE)
#define VRAM_BYTES (16U * 1024U * 1024U)
#define INST_BYTES (1024U * 1024U)
#define BAR_BASE 0x90000000U /* Fake checked PCI memory resource. */

static struct gf_foundation f;
static struct gf2_chan ch, other;
static struct gf_vram_reservation_node a, b, c, narrow, duplicate;
static int owner_a, owner_b, adapter_owner;
static u8 storage[WINDOW_BYTES], second_storage[PAGE];
static struct recover_bar_extent seen_extent;
static u32 seen_bytes, maps, unmaps;
static int map_rc, unmap_rc;
static volatile u8 *expected_base;
static u32 expected_bytes;
static u32 closes;
static i32 close_rc;

static i32 close_stub(void *owner, struct gf_vram_view *view)
{
    assert(owner == &owner_a || !owner);
    ++closes;
    if (owner == &owner_a && close_rc) return close_rc;
    return gf_vram_view_unmap(view);
}

static i32 second_close_stub(void *owner, struct gf_vram_view *view)
{
    assert(owner == &owner_b);
    ++closes;
    return gf_vram_view_unmap(view);
}

static int map_stub(void *owner, const struct recover_bar_extent *extent,
                    u32 bytes, volatile u8 **out)
{
    assert(owner == &adapter_owner);
    ++maps; seen_extent = *extent; seen_bytes = bytes;
    /* Even a failing lower implementation may change its local output. */
    *out = maps == 2 ? second_storage : storage;
    return map_rc;
}

static int unmap_stub(void *owner, volatile u8 *base, u32 bytes)
{
    assert(owner == &adapter_owner);
    assert(base == expected_base && bytes == expected_bytes);
    ++unmaps;
    return unmap_rc;
}

static const struct gf_instmem_map_ops ops = {map_stub, unmap_stub};

static void same_reservation(const struct gf_vram_reservation *a,
                             const struct gf_vram_reservation *b)
{
    assert(a->owner == b->owner && a->off == b->off && a->bytes == b->bytes);
    assert(a->reservation_id == b->reservation_id && a->generation == b->generation);
}

static void same_view(const struct gf_vram_view *a, const struct gf_vram_view *b)
{
    assert(a->extent.raw == b->extent.raw && a->extent.base == b->extent.base);
    assert(a->extent.length == b->extent.length && a->extent.valid == b->extent.valid);
    assert(a->va == b->va && a->map_base == b->map_base);
    assert(a->map_bytes == b->map_bytes && a->bytes == b->bytes && a->mapped == b->mapped);
    assert(a->map_ops == b->map_ops && a->map_owner == b->map_owner);
    assert(a->ch == b->ch && a->next == b->next);
    assert(a->close_owner == b->close_owner && a->close == b->close);
}

static void registry_cases(void)
{
    struct gf_vram_reservation out = {.owner = &owner_b, .off = 17};
    const struct gf_vram_reservation sentinel = out;
    u32 cursor = 0;
    assert(gf_vram_views_close(0) == GF3_EINVAL);
    assert(gf_vram_views_close(&ch) == 0); /* Empty before registry init. */
    assert(gf_vram_registry_init(0) == GF3_EINVAL);
    assert(gf_vram_next(&ch, &cursor, &out) == GF3_ESTATE);
    assert(gf_vram_reserve(&ch, &owner_a, SURFACE_OFF, PAGE, &a) == GF3_ESTATE);
    assert(gf_vram_match(&ch, 0, 1, 1, SURFACE_OFF, PAGE, &out) == GF3_ESTATE);
    same_reservation(&out, &sentinel);
    assert(gf_vram_registry_init(&ch) == 0);
    assert(gf_vram_registry_init(&other) == GF3_ESTATE);
    assert(gf_vram_views_close(&other) == GF3_ESTATE);
    assert(gf_vram_views_close(&ch) == 0);
    assert(gf_vram_next(&ch, &cursor, &out) == 0 && cursor == 0);
    same_reservation(&out, &sentinel);
    assert(gf_vram_reserve(&ch, 0, SURFACE_OFF, PAGE, &a) == GF3_EINVAL);
    assert(gf_vram_reserve(&ch, &owner_a, SURFACE_OFF, 0, &a) == GF3_EINVAL);
    assert(gf_vram_reserve(&ch, &owner_a, SURFACE_OFF, PAGE, 0) == GF3_EINVAL);
    assert(gf_vram_reserve(&other, &owner_a, SURFACE_OFF, PAGE, &a) == GF3_ESTATE);
    assert(gf_vram_reserve(&ch, &owner_a, ch.B, 1, &a) == GF3_ERANGE);
    assert(gf_vram_reserve(&ch, &owner_a, U32_MAX, PAGE, &a) == GF3_ERANGE);
    u32 saved_b = ch.B;
    ch.B = VRAM_BYTES;
    assert(gf_vram_reserve(&ch, &owner_a, VRAM_BYTES - INST_BYTES, 1, &a) == GF3_ERANGE);
    ch.B = saved_b;
    assert(!a.value.owner && !a.value.generation && !a.next);
    assert(gf_vram_reserve(&ch, &owner_a, SURFACE_OFF, WINDOW_BYTES, &a) == 0);
    const struct gf_vram_reservation first = a.value;
    assert(first.reservation_id == 1 && first.generation == 1);
    assert(gf_vram_registry_init(&ch) == 0); /* Must not clear a live list. */
    assert(gf_vram_reserve(&ch, &owner_b, SURFACE_OFF + WINDOW_BYTES, PAGE, &a) == GF2_EEXIST);
    assert(gf_vram_reserve(&ch, &owner_b, SURFACE_OFF + PAGE, PAGE, &duplicate) == GF2_EEXIST);
    assert(gf_vram_reserve(&ch, &owner_b, SURFACE_OFF - 4, 8, &duplicate) == GF2_EEXIST);
    assert(!duplicate.value.owner && !duplicate.next && !duplicate.value.generation);
    same_reservation(&a.value, &first);
    assert(gf_vram_reserve(&ch, &owner_b, SURFACE_OFF + WINDOW_BYTES, PAGE, &b) == 0);
    assert(b.value.reservation_id == first.reservation_id + 1U);
    assert(gf_vram_reserve(&ch, &owner_a, 0, GF_MODE_VRAM_BYTES, &c) == 0);
    assert(gf_vram_next(&ch, &cursor, &out) == 1);
    same_reservation(&out, &a.value);
    assert(cursor == a.value.reservation_id);
    assert(gf_vram_next(&ch, &cursor, &out) == 1);
    same_reservation(&out, &b.value);
    assert(cursor == b.value.reservation_id);
    assert(gf_vram_next(&ch, &cursor, &out) == 1);
    same_reservation(&out, &c.value);
    assert(cursor == c.value.reservation_id);
    assert(gf_vram_next(&ch, &cursor, &out) == 0);
    same_reservation(&out, &c.value);
    assert(cursor == c.value.reservation_id);
    out = sentinel;
    assert(gf_vram_match(&ch, &owner_b, first.reservation_id, first.generation,
                         SURFACE_OFF, PAGE, &out) == GF3_ESTATE);
    assert(gf_vram_match(&ch, 0, first.reservation_id, first.generation + 1U,
                         SURFACE_OFF, PAGE, &out) == GF3_ESTATE);
    assert(gf_vram_match(&ch, 0, 0, 0, SURFACE_OFF, PAGE, &out) == GF3_ESTATE);
    assert(gf_vram_match(&ch, 0, first.reservation_id, first.generation,
                         SURFACE_OFF + WINDOW_BYTES - 4U, 8, &out) == GF3_ERANGE);
    assert(gf_vram_match(&ch, 0, first.reservation_id, first.generation,
                         SURFACE_OFF - 4U, 4, &out) == GF3_ERANGE);
    assert(gf_vram_match(&ch, 0, first.reservation_id, first.generation,
                         U32_MAX - 3U, 8, &out) == GF3_ERANGE);
    same_reservation(&out, &sentinel);
    assert(gf_vram_match(&ch, 0, first.reservation_id, first.generation,
                         SURFACE_OFF + 4, PAGE, &out) == 0);
    same_reservation(&out, &first);
    /* Every release key field is mandatory; failed release preserves list. */
    struct gf_vram_reservation bad = first;
    bad.owner = &owner_b; assert(gf_vram_release(&ch, &bad) == GF3_ESTATE);
    bad = first; ++bad.off; assert(gf_vram_release(&ch, &bad) == GF3_ESTATE);
    bad = first; --bad.bytes; assert(gf_vram_release(&ch, &bad) == GF3_ESTATE);
    bad = first; ++bad.reservation_id; assert(gf_vram_release(&ch, &bad) == GF3_ESTATE);
    bad = first; ++bad.generation; assert(gf_vram_release(&ch, &bad) == GF3_ESTATE);
    same_reservation(&a.value, &first);
    assert(gf_vram_release(&ch, &first) == 0); /* Remove middle node. */
    assert(!a.value.owner && !a.value.off && !a.value.bytes && !a.next);
    assert(a.value.reservation_id == first.reservation_id && a.value.generation == first.generation);
    assert(gf_vram_release(&ch, &first) == GF3_ESTATE);
    assert(gf_vram_reserve(&ch, &owner_a, SURFACE_OFF, WINDOW_BYTES, &a) == 0);
    assert(a.value.reservation_id > c.value.reservation_id);
    assert(a.value.generation == first.generation + 1U);
    out = sentinel;
    assert(gf_vram_match(&ch, 0, first.reservation_id, first.generation,
                         SURFACE_OFF, PAGE, &out) == GF3_ESTATE);
    same_reservation(&out, &sentinel);
    /* Construct the released-node history at its generation limit. No
     * private registry access or billions of allocations are needed. */
    duplicate.value.generation = U32_MAX;
    assert(gf_vram_reserve(&ch, &owner_b, ch.B - PAGE, PAGE, &duplicate) == GF2_ENOSPC);
    assert(duplicate.value.generation == U32_MAX && !duplicate.value.owner && !duplicate.next);
    assert(gf_vram_reserve(&ch, &owner_b, b.value.off + PAGE + 4U, 4, &narrow) == 0);
    assert(narrow.value.reservation_id == a.value.reservation_id + 1U);
}

static void map_error(struct gf_vram_view *view, struct gf2_chan *channel,
                       u32 off, u32 bytes, int rc)
{
    const struct gf_vram_view before = *view;
    volatile u8 *out = second_storage;
    u32 calls = maps;
    assert(gf_vram_view_map(view, channel, off, bytes, &out) == rc);
    same_view(view, &before);
    assert(out == second_storage && maps == calls);
}

static void window_cases(void)
{
    struct gf_vram_view v = {.close_owner = &owner_a},
                        w = {.close = close_stub}; /* NULL close owner is valid. */
    volatile u8 *out = second_storage;
    const struct gf2_chan saved = ch;
    map_error(&v, &ch, SURFACE_OFF, PAGE, GF2_EMAP); /* close is mandatory. */
    v.close = close_stub;
    map_error(&v, &ch, SURFACE_OFF, 0, GF2_EMAP);
    map_error(&v, &ch, SURFACE_OFF + 1U, 4, GF2_EMAP);
    map_error(&v, &ch, SURFACE_OFF, 5, GF2_EMAP);
    map_error(&v, &ch, U32_MAX - 3U, 8, GF2_EMAP);
    map_error(&v, &ch, U32_MAX - PAGE + 1U, PAGE - 4U, GF2_EMAP);
    map_error(&v, &ch, 0, 4, GF2_EMAP); /* Live display reservation. */
    map_error(&v, &ch, GF_MODE_VRAM_BYTES - 4U, 4, GF2_EMAP);
    map_error(&v, &ch, ch.B - PAGE, PAGE, GF3_ESTATE);
    map_error(&v, &other, SURFACE_OFF, PAGE, GF3_ESTATE);
    map_error(&v, &ch, SURFACE_OFF + WINDOW_BYTES - 4U, 8, GF3_ESTATE);
    assert(gf_vram_view_map(0, &ch, SURFACE_OFF, PAGE, &out) == GF2_EMAP);
    assert(gf_vram_view_map(&v, &ch, SURFACE_OFF, PAGE, 0) == GF2_EMAP);
    ch.map_ops = 0; map_error(&v, &ch, SURFACE_OFF, PAGE, GF2_EMAP); ch = saved;
    const struct gf_instmem_map_ops missing = {.map = map_stub};
    ch.map_ops = &missing; map_error(&v, &ch, SURFACE_OFF, PAGE, GF2_EMAP); ch = saved;
    ch.bar1.valid = 0; map_error(&v, &ch, SURFACE_OFF, PAGE, GF2_EMAP); ch = saved;
    ch.bar1.length = SURFACE_OFF + PAGE - 4U;
    map_error(&v, &ch, SURFACE_OFF, PAGE - 4U, GF2_EMAP); ch = saved;
    ch.bar1.base = U32_MAX & ~(PAGE - 1U);
    map_error(&v, &ch, SURFACE_OFF, PAGE, GF2_EMAP); ch = saved;
    ch.bar1.base = (U32_MAX & ~(PAGE - 1U)) - SURFACE_OFF;
    map_error(&v, &ch, SURFACE_OFF + 4U, PAGE, GF2_EMAP); ch = saved;
    map_rc = -1;
    const struct gf_vram_view prepared = v;
    assert(gf_vram_view_map(&v, &ch, SURFACE_OFF, PAGE, &out) == GF2_EMAP);
    const struct gf_vram_view zero = {0};
    same_view(&v, &prepared); assert(out == second_storage);
    assert(gf_vram_views_close(&ch) == 0 && !closes); /* Failed map is absent. */
    map_rc = 0; maps = 0;
    assert(gf_vram_view_map(&v, &ch, SURFACE_OFF + 4U, PAGE, &out) == 0);
    assert(maps == 1 && out == storage + 4 && v.va == out);
    assert(v.close == close_stub && v.close_owner == &owner_a && v.ch == &ch);
    assert(!v.next);
    assert(v.map_base == storage && v.map_bytes == WINDOW_BYTES && v.bytes == PAGE);
    assert(seen_extent.base == BAR_BASE + SURFACE_OFF && seen_extent.length == WINDOW_BYTES);
    assert(seen_extent.raw == ch.bar1.raw && seen_extent.valid == ch.bar1.valid);
    assert(seen_bytes == WINDOW_BYTES && v.extent.base == seen_extent.base);
    map_error(&v, &ch, SURFACE_OFF, PAGE, GF2_EMAP);
    assert(gf_vram_view_map(&w, &ch, b.value.off, PAGE, &out) == 0);
    assert(w.map_base == second_storage && out == second_storage && w.map_bytes == PAGE);
    assert(w.close == close_stub && !w.close_owner && w.ch == &ch && w.next == &v);
    assert(ch.va == saved.va && ch.mapped == saved.mapped && ch.vram_reserved == saved.vram_reserved);
    assert(ch.push_extent.base == saved.push_extent.base && ch.push_extent.length == saved.push_extent.length);
    assert(ch.map_bytes == saved.map_bytes);
    /* Unmap must use its saved adapter even after the channel loan changes. */
    ch.map_ops = 0; ch.map_owner = 0;
    expected_base = storage; expected_bytes = WINDOW_BYTES;
    const struct gf_vram_view mapped = v;
    unmap_rc = -1;
    assert(gf_vram_view_unmap(&v) == GF2_EMAP);
    same_view(&v, &mapped);
    unmap_rc = 0;
    assert(gf_vram_view_unmap(&v) == 0); same_view(&v, &zero);
    assert(gf_vram_view_unmap(&v) == GF2_EMAP);
    assert(gf_vram_view_unmap(0) == GF2_EMAP);
    expected_base = second_storage; expected_bytes = PAGE;
    assert(gf_vram_view_unmap(&w) == 0); same_view(&w, &zero);
    assert(gf_vram_views_close(&ch) == 0 && !closes); /* Both manually removed. */
    ch = saved;
    /* Only the requested bytes, not the rounded pages, need containment. */
    map_error(&v, &ch, narrow.value.off, narrow.value.bytes, GF2_EMAP);
    v.close = second_close_stub; v.close_owner = &owner_b;
    assert(gf_vram_view_map(&v, &ch, narrow.value.off, narrow.value.bytes, &out) == 0);
    assert(v.close == second_close_stub && v.close_owner == &owner_b && !v.next);
    assert(out == storage + 4 && v.bytes == 4 && v.map_bytes == PAGE);
    assert(seen_extent.base == BAR_BASE + b.value.off + PAGE);
    expected_base = storage; expected_bytes = PAGE;
    assert(gf_vram_view_unmap(&v) == 0);
    /* One failed unmap and three successful independent window closes. */
    assert(unmaps == 4);
}

static void close_cases(void)
{
    struct gf_vram_view v = {.close = close_stub, .close_owner = &owner_a},
                        w = {.close = close_stub};
    volatile u8 *out = 0;
    maps = 0;
    assert(gf_vram_view_map(&v, &ch, SURFACE_OFF, PAGE, &out) == 0);
    assert(gf_vram_view_map(&w, &ch, b.value.off, PAGE, &out) == 0);
    const struct gf_vram_view saved_v = v, saved_w = w;
    expected_base = second_storage; expected_bytes = PAGE;
    unmap_rc = -1;
    u32 before = unmaps;
    assert(gf_vram_views_close(&other) == GF3_ESTATE && !closes);
    assert(gf_vram_views_close(&ch) == GF2_EMAP && closes == 1);
    assert(unmaps == before + 1U); /* Stop at failing head, leave tail untouched. */
    same_view(&v, &saved_v); same_view(&w, &saved_w);
    /* Remove head directly; retry must visit only the remaining window. */
    unmap_rc = 0;
    assert(gf_vram_view_unmap(&w) == 0);
    expected_base = storage;
    assert(gf_vram_views_close(&ch) == 0 && closes == 2);
    const struct gf_vram_view zero = {0};
    same_view(&v, &zero); same_view(&w, &zero);
    assert(gf_vram_views_close(&ch) == 0 && closes == 2);
    /* Multiple callback removals must follow saved next after zeroing head. */
    v.close = second_close_stub; v.close_owner = &owner_b;
    w.close = close_stub;
    maps = 0;
    assert(gf_vram_view_map(&v, &ch, SURFACE_OFF, PAGE, &out) == 0);
    /* Both adapters return the same address so one expected tuple suffices. */
    maps = 0;
    assert(gf_vram_view_map(&w, &ch, SURFACE_OFF, PAGE, &out) == 0);
    expected_base = storage;
    assert(gf_vram_views_close(&ch) == 0 && closes == 4);
    same_view(&v, &zero); same_view(&w, &zero);
    assert(gf_vram_views_close(&ch) == 0 && closes == 4);
    /* A successful head must stay removed when the next owner fails. */
    v.close = close_stub; v.close_owner = &owner_a;
    w.close = close_stub;
    maps = 0;
    assert(gf_vram_view_map(&v, &ch, SURFACE_OFF, PAGE, &out) == 0);
    maps = 0;
    assert(gf_vram_view_map(&w, &ch, SURFACE_OFF, PAGE, &out) == 0);
    const struct gf_vram_view retained = v;
    close_rc = GF2_EIO;
    assert(gf_vram_views_close(&ch) == GF2_EIO && closes == 6);
    same_view(&w, &zero); same_view(&v, &retained);
    close_rc = 0;
    assert(gf_vram_views_close(&ch) == 0 && closes == 7);
    same_view(&v, &zero);
    assert(gf_vram_views_close(&ch) == 0 && closes == 7);
}

void _start(void)
{
    f.imem.vram_bytes = VRAM_BYTES; f.imem.reserved = INST_BYTES;
    ch = (struct gf2_chan){.f = &f, .B = VRAM_BYTES - INST_BYTES - GF2_PUSH_BYTES,
        .state = GF2_RUN, .map_ops = &ops, .map_owner = &adapter_owner,
        .bar1 = {BAR_BASE | 8U, BAR_BASE, VRAM_BYTES, 1},
        .mapped = true, .vram_reserved = true, .va = (volatile u32 *)storage,
        .map_bytes = GF2_PUSH_BYTES,
        .push_extent = {BAR_BASE | 8U, BAR_BASE + VRAM_BYTES - INST_BYTES - GF2_PUSH_BYTES,
                        GF2_PUSH_BYTES, 1}};
    other = ch;
    registry_cases();
    window_cases();
    close_cases();
    /* Unlink middle, tail, middle and final entry, then enumerate empty. */
    assert(gf_vram_release(&ch, &a.value) == 0);
    assert(gf_vram_release(&ch, &b.value) == 0);
    assert(gf_vram_release(&ch, &c.value) == 0);
    assert(gf_vram_release(&ch, &narrow.value) == 0);
    u32 cursor = 0;
    struct gf_vram_reservation out = {0};
    assert(gf_vram_next(&ch, &cursor, &out) == 0 && !cursor && !out.owner);
    gf2_host_exit(0);
}
