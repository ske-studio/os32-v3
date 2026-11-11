/* HAL v2 C9: validate a stable batch before executing it (§5-3). */
#include "gfx2_sw.h"
#include "kstring.h"
#include "exec/exec.h"

#define GFX2_SUBMIT_DL 1000000UL
#define PATTERN_SIDE 8u
static GFX2_Op k_ops[GFX2_MAX_CMDS];
static struct gfx2_exec execs[GFX2_MAX_CMDS];
static struct gfx2_pattern pattern;

static int range(u32 data, u32 pitch, u32 row, u32 h, int user, int write)
{
    u32 bytes;
    if (!data || !h || !row || pitch < row || (h - 1) > (~0UL - row) / pitch)
        return 0;
    bytes = (h - 1) * pitch + row;
    if (bytes - 1 > ~0UL - data) return 0;
    return !user || (write ? ring3_user_range_writable(data, bytes) :
                            ring3_user_range_ok(data, bytes));
}

/* Zero every permitted field; anything remaining is an unused input. */
static int fields(const GFX2_Op *o)
{
    GFX2_Op z = *o;
    u32 flags = 0;
    z.op = 0;
    switch (o->op) {
    case GFX2_OP_FILL:
        flags = GFX2_F_PAT | GFX2_F_CLIP;
        z.dst = 0; z.dx = z.dy = z.dw = z.dh = 0; z.fg.v = 0; z.rop = 0;
        break;
    case GFX2_OP_COPY: case GFX2_OP_STRETCH: case GFX2_OP_ROP4: case GFX2_OP_ALPHA:
        flags = GFX2_F_CLIP;
        z.dst = z.src = 0; z.dx = z.dy = z.dw = z.dh = 0;
        z.sx = z.sy = z.sw = z.sh = 0;
        if (o->op == GFX2_OP_ALPHA) {
            flags |= GFX2_F_ALPHA_PIXEL;
            z.fg.x = 0;
        } else {
            z.rop = 0; z.fg.v = 0; flags |= GFX2_F_PAT;
            if (o->op != GFX2_OP_ROP4) flags |= GFX2_F_SRCKEY | GFX2_F_DSTKEY;
            else z.data = z.pitch = 0;
            if (o->op == GFX2_OP_STRETCH) flags |= GFX2_F_FILTER;
        }
        break;
    case GFX2_OP_MONO:
        flags = GFX2_F_CLIP;
        z.dst = 0; z.dx = z.dy = z.dw = z.dh = 0;
        z.fg.v = z.bg.v = 0; z.bg.x = 0; z.data = z.pitch = 0;
        break;
    case GFX2_OP_HOSTDATA:
        flags = GFX2_F_SRCKEY | GFX2_F_CLIP;
        z.dst = 0; z.dx = z.dy = z.dw = z.dh = 0; z.data = z.pitch = 0;
        break;
    case GFX2_OP_READBACK:
        z.src = 0; z.sx = z.sy = z.sw = z.sh = 0; z.data = z.pitch = 0;
        break;
    case GFX2_OP_LINE:
        flags = GFX2_F_CLIP;
        z.dst = 0; z.dx = z.dy = z.sx = z.sy = 0; z.fg.v = 0; z.rop = 0;
        break;
    case GFX2_OP_SET_PATTERN:
        flags = GFX2_F_PAT_COLOR;
        z.data = 0; z.fg.v = z.bg.v = z.bg.x = 0;
        break;
    default: return OS32_ERR_INVAL;
    }
    if (o->flags & ~flags) return OS32_ERR_INVAL;
    if (o->flags & GFX2_F_CLIP) z.cx = z.cy = z.cw = z.ch = 0;
    if (o->flags & GFX2_F_SRCKEY) z.skey.v = z.skey.x = 0;
    if (o->flags & GFX2_F_DSTKEY) z.dkey.v = z.dkey.x = 0;
    z.flags = 0;
    for (u32 i = 0; i < sizeof(z); i++)
        if (((const u8 *)&z)[i]) return OS32_ERR_INVAL;
    if ((o->op == GFX2_OP_MONO || o->op == GFX2_OP_SET_PATTERN) &&
        (o->bg.x & ~GFX2_CX_TRANSPARENT)) return OS32_ERR_INVAL;
    if (o->op == GFX2_OP_ALPHA && o->fg.x > 255) return OS32_ERR_INVAL;
    if (o->op == GFX2_OP_COPY || o->op == GFX2_OP_ROP4) {
        if (o->sw != o->dw || o->sh != o->dh) return OS32_ERR_INVAL;
    }
    if (o->op == GFX2_OP_FILL || o->op == GFX2_OP_LINE) {
        if (o->rop > 255 || (((o->rop >> 2) ^ o->rop) & 0x33)) return OS32_ERR_INVAL;
    }
    if (o->op == GFX2_OP_COPY && (o->rop > 255 ||
        ((o->flags & (GFX2_F_SRCKEY | GFX2_F_DSTKEY)) && o->rop != GFX2_ROP_SRCCOPY)))
        return OS32_ERR_INVAL;
    if (o->op == GFX2_OP_STRETCH && o->rop != GFX2_ROP_SRCCOPY) return OS32_ERR_INVAL;
    if ((o->flags & GFX2_F_SRCKEY) && o->skey.v > o->skey.x) return OS32_ERR_INVAL;
    if ((o->flags & GFX2_F_DSTKEY) && o->dkey.v > o->dkey.x) return OS32_ERR_INVAL;
    return 0;
}

static struct gfx2_rect intersect(struct gfx2_rect r, struct gfx2_rect b)
{
    i32 right = r.x + r.w, bottom = r.y + r.h;
    if (right > b.x + b.w) right = b.x + b.w;
    if (bottom > b.y + b.h) bottom = b.y + b.h;
    if (r.x < b.x) r.x = b.x;
    if (r.y < b.y) r.y = b.y;
    r.w = right > r.x ? right - r.x : 0;
    r.h = bottom > r.y ? bottom - r.y : 0;
    return r;
}

static int surface(struct gfx2_adapter *a, u32 sid, u32 owner, int write,
                   struct gfx2_surf **out, struct gfx2_view *v)
{
    int rc;
    GfxSyncCtx *ctx;
    u32 mode;
    struct gfx2_surf *s = gfx2_surf_get(sid, owner, &rc);
    if (!s) return rc;
    if (s->adapter != a->index) return OS32_ERR_INVAL;
    if (s->state == GFX2_ST_LOCKED_RW || (write && s->state == GFX2_ST_LOCKED_RO))
        return OS32_ERR_BUSY;
    if (s->state != GFX2_ST_IDLE && s->state != GFX2_ST_LOCKED_RO) return OS32_ERR_STALE;
    *out = s;
    v->base = s->kva ? s->kva : (u8 *)(uptr)s->map_va;
    v->pitch = s->pitch; v->w = s->w; v->h = s->h; v->fmt = s->fmt;
    v->pal = gfx2_pal;
    if ((s->flags & GFX2_SF_ALIAS) && !gfx2_sync_get(a, s->head, &ctx, &mode) &&
        mode == GFXSYNC_DIRECT) v->sync = ctx;
    return 0;
}

static int check_one(struct gfx2_adapter *a, const GFX2_Op *o,
                     struct gfx2_exec *e, u32 owner, int user)
{
    int rc = fields(o);
    struct gfx2_fmt_info fmt;
    u32 row = 0, h = o->dh;
    if (rc) return rc;
    kmemset(e, 0, sizeof(*e)); e->op = o;
    if ((o->flags & GFX2_F_PAT) && !pattern.kind) return OS32_ERR_INVAL;
    if (o->flags & GFX2_F_PAT) e->pat = &pattern;
    if (o->op == GFX2_OP_SET_PATTERN) {
        row = o->flags & GFX2_F_PAT_COLOR ? sizeof(pattern.color) : sizeof(pattern.mono);
        if (!range(o->data, row, row, 1, user, 0)) return OS32_ERR_INVAL;
        pattern.kind = o->flags & GFX2_F_PAT_COLOR ? 2 : 1;
        pattern.fg = o->fg; pattern.bg = o->bg;
        e->how = GFX2_HOW_SW;
        return 0;
    }
    if (o->op != GFX2_OP_READBACK) {
        rc = surface(a, o->dst, owner, 1, &e->dst, &e->dv);
        if (rc) return rc;
        if (o->op != GFX2_OP_LINE && (!o->dw || !o->dh)) return OS32_ERR_INVAL;
        e->clip = (struct gfx2_rect){0, 0, e->dst->w, e->dst->h};
        if (o->op != GFX2_OP_LINE)
            e->clip = intersect((struct gfx2_rect){o->dx, o->dy, o->dw, o->dh}, e->clip);
        if (o->flags & GFX2_F_CLIP)
            e->clip = intersect(e->clip, (struct gfx2_rect){o->cx, o->cy, o->cw, o->ch});
    }
    if (o->op == GFX2_OP_COPY || o->op == GFX2_OP_STRETCH || o->op == GFX2_OP_ROP4 ||
        o->op == GFX2_OP_ALPHA || o->op == GFX2_OP_READBACK) {
        rc = surface(a, o->src, owner, 0, &e->src, &e->sv);
        if (rc) return rc;
        if (!o->sw || !o->sh || o->sx < 0 || o->sy < 0 ||
            (u32)o->sx + o->sw > e->src->w || (u32)o->sy + o->sh > e->src->h)
            return OS32_ERR_INVAL;
        if (e->src == e->dst && (o->op == GFX2_OP_STRETCH || o->op == GFX2_OP_ALPHA ||
            e->sv.fmt != e->dv.fmt) &&
            o->sx < o->dx + (i32)o->dw && o->dx < o->sx + (i32)o->sw &&
            o->sy < o->dy + (i32)o->dh && o->dy < o->sy + (i32)o->sh) return OS32_ERR_INVAL;
        if (o->op == GFX2_OP_READBACK) {
            e->clip = (struct gfx2_rect){o->sx, o->sy, o->sw, o->sh};
            if (gfx2_fmt_info(e->src->fmt, &fmt)) return OS32_ERR_INVAL;
            row = o->sw * fmt.cpp; h = o->sh;
        }
    }
    if (o->op == GFX2_OP_HOSTDATA) {
        if (gfx2_fmt_info(e->dst->fmt, &fmt)) return OS32_ERR_INVAL;
        row = o->dw * fmt.cpp;
    } else if (o->op == GFX2_OP_MONO || o->op == GFX2_OP_ROP4) row = (o->dw + 7u) / 8u;
    if (row && !range(o->data, o->pitch, row, h, user, o->op == GFX2_OP_READBACK))
        return OS32_ERR_INVAL;
    e->cpu = (const u8 *)(uptr)o->data; e->cpu_pitch = o->pitch;
    rc = gfx2_check_exec(a, e, &e->how);
    if (rc) return rc;
    return e->how == GFX2_HOW_NOSUPPORT ? OS32_ERR_NOSYS : 0;
}

static int run_one(struct gfx2_adapter *a, const struct gfx2_exec *original)
{
    struct gfx2_exec ex = *original;
    struct gfx2_pattern pat;
    const GFX2_Op *o = ex.op;
    struct gfx2_rect touched = {0};
    int rc;
    u32 da = GFX2_CPU_W;
    if (o->op == GFX2_OP_SET_PATTERN) {
        pattern.kind = o->flags & GFX2_F_PAT_COLOR ? 2 : 1;
        pattern.fg = o->fg; pattern.bg = o->bg;
        kmemcpy(pattern.kind == 2 ? (void *)pattern.color : (void *)pattern.mono,
                (const void *)(uptr)o->data, pattern.kind == 2 ? sizeof(pattern.color) : sizeof(pattern.mono));
        return 0;
    }
    if (ex.pat) {
        struct gfx2_fmt_info fmt;
        pat = pattern;
        if (pat.kind == 2) {
            rc = gfx2_fmt_info(ex.dst->fmt, &fmt);
            if (rc) return rc;
            u32 mask = fmt.cpp == 4 ? ~0UL : (1UL << (fmt.cpp * 8)) - 1;
            for (u32 i = 0; i < PATTERN_SIDE * PATTERN_SIDE; i++) pat.color[i] &= mask;
        }
        ex.pat = &pat;
    }
    if (o->op == GFX2_OP_READBACK) {
        rc = gfx2_cpu_begin(ex.src, GFX2_CPU_R, GFX2_SUBMIT_DL);
        if (rc) return rc;
        rc = ex.how == GFX2_HOW_HW ? a->be->readback(a->ctx, &ex, (u8 *)(uptr)o->data) :
                                    gfx2_sw_readback(&ex, (u8 *)(uptr)o->data);
        gfx2_cpu_end(ex.src, GFX2_CPU_R, 0);
        if (rc && rc != OS32_ERR_INVAL && rc != OS32_ERR_NOSYS) gfx2_adapter_lost(a, 0);
        return rc;
    }
    if (ex.how == GFX2_HOW_HW) {
        u32 f = gfx2_fence_reserve(a);
        if (!f) return OS32_ERR_STALE;
        rc = a->be->emit(a->ctx, &ex, f);
        if (rc == OS32_ERR_INVAL || rc == OS32_ERR_NOSYS) {
            gfx2_fence_abandon(a, f); return rc;
        }
        if (rc) { gfx2_fence_commit_fail(a, f, ex.dst, ex.src, rc); return rc; }
        return gfx2_fence_commit(a, f, ex.dst, ex.src);
    }
    if (ex.src == ex.dst) da |= GFX2_CPU_R;
    rc = gfx2_cpu_begin(ex.dst, da, GFX2_SUBMIT_DL);
    if (rc) return rc;
    if (ex.src && ex.src != ex.dst) {
        rc = gfx2_cpu_begin(ex.src, GFX2_CPU_R, GFX2_SUBMIT_DL);
        if (rc) { gfx2_cpu_end(ex.dst, da, &touched); return rc; }
    }
    gfx2_sw_fn fn = gfx2_sw_table[o->op];
    if (o->op == GFX2_OP_COPY && (o->flags & (GFX2_F_SRCKEY | GFX2_F_DSTKEY))) fn = gfx2_sw_ckey;
    else if ((o->op == GFX2_OP_FILL && (o->rop != GFX2_ROP_PATCOPY || ex.pat)) ||
             (o->op == GFX2_OP_COPY && (o->rop != GFX2_ROP_SRCCOPY || ex.pat))) fn = gfx2_sw_rop;
    rc = fn(&ex, &touched);
    if (ex.src && ex.src != ex.dst) gfx2_cpu_end(ex.src, GFX2_CPU_R, 0);
    gfx2_cpu_end(ex.dst, da, &touched);
    if (rc && rc != OS32_ERR_INVAL && rc != OS32_ERR_NOSYS) gfx2_adapter_lost(a, 0);
    return rc;
}

int gfx2_submit(u32 adapter, const GFX2_Op *ops, u32 n, u32 gen, u32 *done, u32 *fence)
{
    int user = ring3_call_from_user(), rc = 0;
    u32 k_done = 0, k_fence, k_hw = 0;
    struct gfx2_adapter *a;
    struct gfx2_caller caller;
    if (!done || !fence || (user &&
        (!ring3_user_range_writable((u32)(uptr)done, sizeof(*done)) ||
         !ring3_user_range_writable((u32)(uptr)fence, sizeof(*fence))))) return OS32_ERR_INVAL;
    a = gfx2_adapter_get(adapter);
    if (!a) return OS32_ERR_INVAL;
    if (!(a->flags & GFX2_AF_ACTIVE)) return OS32_ERR_NOSYS;
    if (gen != a->gen) return OS32_ERR_STALE;
    if (n > GFX2_MAX_CMDS || (n && !range((u32)(uptr)ops, n * sizeof(*ops),
        n * sizeof(*ops), 1, user, 0))) return OS32_ERR_INVAL;
    k_fence = a->fence.issued;
    if (!n) goto out;
    if (gfx2_caller_get(&caller)) return OS32_ERR_INVAL;
    kmemcpy(k_ops, ops, n * sizeof(*ops));
    kmemset(&pattern, 0, sizeof(pattern));
    for (u32 i = 0; i < n; i++) {
        rc = check_one(a, &k_ops[i], &execs[i], caller.owner, user);
        if (rc) goto out;
    }
    kmemset(&pattern, 0, sizeof(pattern));
    for (u32 i = 0; i < n; i++) {
        rc = run_one(a, &execs[i]);
        if (rc) break;
        k_done = i + 1;
        k_hw += execs[i].how == GFX2_HOW_HW;
    }
    if (k_hw) {
        int kick_rc = a->be->kick(a->ctx);
        if (!rc && kick_rc) { rc = kick_rc; gfx2_adapter_lost(a, 0); }
    }
    k_fence = a->fence.issued;
    gfx2_closing_sweep(a);
out:
    *done = k_done; *fence = k_fence;
    return rc;
}
