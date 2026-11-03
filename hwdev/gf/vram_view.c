/* Independent CPU windows and explicit pinned VRAM ranges.
 * nouveau (c03c1a1), nouveau_bo.c:594-599,618-629 (pin/unpin),
 * :669-689 (BO-specific map/unmap). OS32 borrows resident nodes and ops
 * instead of BO/TTM; foreground ownership needs no reservation lock. */
#include "accel.h"

static struct gf_vram_registry registry;

static bool registry_matches(struct gf2_chan *ch)
{
    return ch && registry.ch == ch;
}

static bool contains(const struct gf_vram_reservation *r, u32 off, u32 bytes)
{
    return off >= r->off && off - r->off <= r->bytes &&
           bytes <= r->bytes - (off - r->off);
}

int gf_vram_registry_init(struct gf2_chan *ch)
{
    if (!ch) return GF3_EINVAL;
    if (registry.ch) return registry.ch == ch ? 0 : GF3_ESTATE;
    registry.ch = ch;
    registry.next_id = 1;
    return 0;
}

int gf_vram_reserve(struct gf2_chan *ch, const void *owner,
                    u32 off, u32 bytes, struct gf_vram_reservation_node *node)
{
    if (!ch || !owner || !node || !bytes) return GF3_EINVAL;
    if (!registry_matches(ch)) return GF3_ESTATE;
    for (struct gf_vram_reservation_node *n = registry.head; n; n = n->next)
        if (n == node) return GF2_EEXIST;
    if (!ch->f) return GF3_ESTATE;
    const struct gf_imem *imem = &ch->f->imem;
    if (off > ch->B || bytes > ch->B - off ||
        imem->reserved > imem->vram_bytes ||
        off > imem->vram_bytes - imem->reserved ||
        bytes > imem->vram_bytes - imem->reserved - off)
        return GF3_ERANGE;
    for (struct gf_vram_reservation_node *n = registry.head; n; n = n->next) {
        /* Ranges are already bounded without overflow above. */
        if (off < n->value.off + n->value.bytes && n->value.off < off + bytes)
            return GF2_EEXIST;
    }
    if (!registry.next_id || node->value.generation == U32_MAX)
        return GF2_ENOSPC;
    node->value = (struct gf_vram_reservation){
        .owner = owner, .off = off, .bytes = bytes,
        .reservation_id = registry.next_id,
        .generation = node->value.generation + 1U
    };
    registry.next_id = registry.next_id == U32_MAX ? 0 : registry.next_id + 1U;
    node->next = registry.head;
    registry.head = node;
    return 0;
}

int gf_vram_release(struct gf2_chan *ch, const struct gf_vram_reservation *key)
{
    if (!ch || !key || !key->owner || !key->bytes) return GF3_EINVAL;
    if (!registry_matches(ch)) return GF3_ESTATE;
    for (struct gf_vram_reservation_node **link = &registry.head;
         *link; link = &(*link)->next) {
        struct gf_vram_reservation_node *n = *link;
        const struct gf_vram_reservation *v = &n->value;
        if (v->owner != key->owner || v->off != key->off ||
            v->bytes != key->bytes || v->reservation_id != key->reservation_id ||
            v->generation != key->generation) continue;
        *link = n->next;
        n->next = 0;
        n->value.owner = 0;
        n->value.off = n->value.bytes = 0;
        return 0;
    }
    return GF3_ESTATE;
}

int gf_vram_match(struct gf2_chan *ch, const void *owner,
                  u32 reservation_id, u32 generation, u32 off, u32 bytes,
                  struct gf_vram_reservation *out)
{
    if (!ch || !out || !bytes) return GF3_EINVAL;
    if (!registry_matches(ch)) return GF3_ESTATE;
    for (struct gf_vram_reservation_node *n = registry.head; n; n = n->next) {
        const struct gf_vram_reservation *v = &n->value;
        if (v->reservation_id != reservation_id || v->generation != generation ||
            (owner && v->owner != owner)) continue;
        if (!contains(v, off, bytes)) return GF3_ERANGE;
        *out = *v;
        return 0;
    }
    return GF3_ESTATE;
}

int gf_vram_next(struct gf2_chan *ch, u32 *cursor, struct gf_vram_reservation *out)
{
    if (!ch || !cursor || !out) return GF3_EINVAL;
    if (!registry_matches(ch)) return GF3_ESTATE;
    const struct gf_vram_reservation *first = 0;
    for (struct gf_vram_reservation_node *n = registry.head; n; n = n->next)
        if (n->value.reservation_id > *cursor &&
            (!first || n->value.reservation_id < first->reservation_id))
            first = &n->value;
    if (!first) return 0;
    *out = *first;
    *cursor = first->reservation_id;
    return 1;
}

int gf_vram_view_map(struct gf_vram_view *view, struct gf2_chan *ch,
                     u32 off, u32 len, volatile u8 **out)
{
    const u32 page_mask = MEM_PAGE_SIZE - 1U;
    /* Boot display map: production.h / geforce_platform.c; its UC pages
     * are borrowed directly, never mapped/unmapped by an independent view. */
    const u32 display_end = (GF_MODE_VRAM_BYTES + page_mask) & ~page_mask;
    struct recover_bar_extent extent;
    volatile u8 *base;
    u32 start, end, bytes;

    if (!view || !ch || !out || view->mapped || !ch->map_ops ||
        !ch->map_ops->map || !ch->map_ops->unmap || !ch->bar1.valid ||
        !len || (off & 3U) || (len & 3U) || off > U32_MAX - len)
        return GF2_EMAP;
    start = off & ~page_mask;
    end = off + len;
    if (end > U32_MAX - page_mask) return GF2_EMAP;
    end = (end + page_mask) & ~page_mask;
    bytes = end - start;
    if (start < display_end || (ch->bar1.base & page_mask) ||
        start > ch->bar1.length || bytes > ch->bar1.length - start ||
        start > U32_MAX - ch->bar1.base ||
        bytes - 1U > U32_MAX - (ch->bar1.base + start))
        return GF2_EMAP;
    if (!registry_matches(ch)) return GF3_ESTATE;
    struct gf_vram_reservation_node *n;
    for (n = registry.head; n; n = n->next)
        if (contains(&n->value, off, len)) break;
    if (!n) return GF3_ESTATE;
    extent = ch->bar1;
    extent.base += start;
    extent.length = bytes;
    if (ch->map_ops->map(ch->map_owner, &extent, bytes, &base)) return GF2_EMAP;
    *view = (struct gf_vram_view){
        .extent = extent, .va = base + (off - start), .map_base = base,
        .map_bytes = bytes, .bytes = len, .mapped = true,
        .map_ops = ch->map_ops, .map_owner = ch->map_owner
    };
    *out = view->va;
    return 0;
}

int gf_vram_view_unmap(struct gf_vram_view *view)
{
    if (!view || !view->mapped || !view->map_ops || !view->map_ops->unmap ||
        view->map_ops->unmap(view->map_owner, view->map_base, view->map_bytes))
        return GF2_EMAP;
    *view = (struct gf_vram_view){0};
    return 0;
}
