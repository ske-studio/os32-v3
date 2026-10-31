/* Owner lifecycle, page ownership, regions, and ledger consistency. */
/* ======================================================================== */
/*  所有権台帳 (TASK_T1_LEDGER §3-1・§3-2、T1b)                              */
/*                                                                          */
/*  L2 の更新は必ず L1 と同じ IRQ 保存区間で、全部検査してから行う。         */
/* ======================================================================== */
static int owner_ok(u32 owner)
{
    return owner && owner < LEDGER_MAX_OWNERS && ledger_owners[owner].kind;
}
/* RAM を持てる owner (DEVICE は MMIO の予約だけ、X4)。 */
static int owner_ram(u32 owner)
{
    return owner_ok(owner) && ledger_owners[owner].kind != LEDGER_KIND_DEVICE;
}
static void take_page(u32 p, u32 owner) { bmp_set(p); owner_map[p] = (u8)owner; }
static void give_page(u32 p) { bmp_clear(p); owner_map[p] = 0; }
/* eligible かつ allocated で L2 が owner のページか。解放・移譲・回収・claim の
 * 全件検査が同じ述語を使う (noinline はカーネルの予算のため)。 */
static int __attribute__((noinline)) page_owned(u32 p, u32 owner)
{
    return bit(eligible, p) && bit(bitmap, p) && owner_map[p] == owner;
}
int pgalloc_page_owned(u32 pfn, u32 owner)
{
    return pfn < limit_pfn && owner_ok(owner) && page_owned(pfn, owner);
}

/* [pfn, pfn + n) が PFN の管理範囲 [0, limit_pfn) に収まるか (n > 0)。 */
static int __attribute__((noinline)) pfn_span_ok(u32 pfn, int n)
{
    return initialized && n > 0 && pfn < limit_pfn && (u32)n <= limit_pfn - pfn;
}

static void note_last(u32 *last, u32 op, u32 owner, void *eip)
{
    last[LEDGER_LAST_OP] = op;
    last[LEDGER_LAST_OWNER] = owner;
    last[LEDGER_LAST_EIP] = (u32)(uptr)eip;
}
/* R1 (§3-5、T2a): IRQ / 例外上の台帳操作を診断し、変更前に panic。
 * 失敗する要求も禁止。DMA プール内の割当はここを通らない (R4)。
 * 呼び手は IRQ 保存区間の中。 */
static void ledger_note(u32 op, u32 owner, void *eip)
{
    if (!kctx_irq_depth && !kctx_exc_depth) return;
    if (kctx_irq_depth) {
        ledger_irq_ops++;
        note_last(ledger_irq_last, op, owner, eip);
    }
    if (kctx_exc_depth) {
        ledger_exc_ops++;
        note_last(ledger_exc_last, op, owner, eip);
    }
    if (owner_ok(owner)) {
        if (op == LEDGER_OP_FREE || op == LEDGER_OP_RECLAIM)
            ledger_owners[owner].free_irq++;
        else
            ledger_owners[owner].alloc_irq++;
    }
    ledger_check_tag = "R1 context";
    for (;;) { _stop(); }
}
#define LEDGER_CALLER() __builtin_return_address(0)

int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner)
{
    struct ledger_owner *o;
    u32 i, k;
    unsigned int flags;
    int ok;
    if (!owner || (kind != LEDGER_KIND_AS && kind != LEDGER_KIND_MODULE &&
                   kind != LEDGER_KIND_DEVICE)) return 0;
    flags = irq_save();
    ok = 0;
    for (i = LEDGER_OWNER_FIXED_LAST + 1; i < LEDGER_MAX_OWNERS; i++)
        if (!ledger_owners[i].kind) break;
    if (initialized && i < LEDGER_MAX_OWNERS) {
        o = &ledger_owners[i];
        o->kind = (u8)kind;
        o->id = (u8)id;
        for (k = 0; k < LEDGER_NAME_LEN; k++) {
            o->name[k] = name ? *name : 0;
            if (name && *name) name++;
        }
        o->pages = o->alloc_irq = o->free_irq = 0;
        *owner = i;
        ok = 1;
    }
    irq_restore(flags);
    return ok;
}

int ledger_owner_retire(u32 owner)
{
    struct ledger_owner *o;
    u32 i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (!owner_ok(owner)) goto done;
    o = &ledger_owners[owner];
    if (o->kind == LEDGER_KIND_PERSIST || o->kind == LEDGER_KIND_DEVICE ||
        o->pages) goto refused;
    for (i = 0; i < ledger_region_count; i++)
        if (ledger_regions[i].owner == owner) goto refused;
    for (i = 0; i < LEDGER_MAX_SURFACES; i++)
        if (ledger_surfaces[i].npages && ledger_surfaces[i].owner == owner)
            goto refused;
    o->kind = 0;
    o->id = 0;
    for (i = 0; i < LEDGER_NAME_LEN; i++) o->name[i] = 0;
    o->alloc_irq = o->free_irq = 0;
    ok = 1;
    goto done;
refused:
    ledger_retire_refused++;
done:
    irq_restore(flags);
    return ok;
}

u32 ledger_owner_pages(u32 owner)
{
    return owner < LEDGER_MAX_OWNERS ? ledger_owners[owner].pages : 0;
}

/* 呼び手は IRQ 保存区間の中。[first, end) から n ページ連続の空きを探して
 * owner で取る。TOP_DOWN は上端から、BOTTOM_UP は下端からの最初適合。 */
static int alloc_owner(u32 owner, u32 n, u32 first, u32 end, u32 dir, u32 *pfn)
{
    u32 s, i, b;
    if (!online || !owner_ram(owner) || !pfn || !n || first >= end ||
        end > limit_pfn || n > end - first) return 0;
    if (dir == LEDGER_TOP_DOWN) {
        s = end - n;
        for (;;) {
            for (i = n; i > 0 && !bmp_test(s + i - 1); i--) {}
            if (!i) break;
            b = s + i - 1;          /* 候補の中で最も上の使用中ページ */
            if (b < first + n) return 0;
            s = b - n;
        }
    } else if (dir == LEDGER_BOTTOM_UP) {
        for (s = first;; s += i + 1) {
            if (s > end - n) return 0;
            for (i = 0; i < n && !bmp_test(s + i); i++) {}
            if (i == n) break;
        }
    } else return 0;
    for (i = 0; i < n; i++) take_page(s + i, owner);
    used_pages += n;
    ledger_owners[owner].pages += n;
    *pfn = s;
    return 1;
}

int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 dir,
                          u32 *pfn)
{
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_ALLOC, owner, LEDGER_CALLER());
    ok = n > 0 && alloc_owner(owner, (u32)n, first, end, dir, pfn);
    irq_restore(flags);
    return ok;
}

/* Clients dereference returned addresses: MODEL publishes the full eligible
 * limit only after stage_online has mapped it. */
u32 pgalloc_alloc_phys(u32 owner, int n)
{
    u32 pfn;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_ALLOC, owner, LEDGER_CALLER());
    ok = n > 0 && alloc_owner(owner, (u32)n,
                              MEM_SHM_RESV_START / PAGE_SIZE,
                              generic_end, LEDGER_BOTTOM_UP, &pfn);
    irq_restore(flags);
    return ok ? pfn * PAGE_SIZE : 0;
}

int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    u32 i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_FREE, owner, LEDGER_CALLER());
    ok = 0;
    if (!owner_ok(owner) || !pfn_span_ok(pfn, n)) goto bad;
    for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
        const struct ledger_surface *sf = &ledger_surfaces[i];
        if (sf->npages && (!sf->closing || sf->lease_count) &&
            pfn < sf->first + sf->npages &&
            sf->first < pfn + (u32)n) goto bad;
    }
    for (i = 0; i < (u32)n; i++)
        if (!page_owned(pfn + i, owner)) goto bad;
    for (i = 0; i < (u32)n; i++) give_page(pfn + i);
    used_pages -= (u32)n;
    ledger_owners[owner].pages -= (u32)n;
    ok = 1;
    goto done;
bad:
    ledger_bad_free++;
done:
    irq_restore(flags);
    return ok;
}

int ledger_transfer(u32 pfn, int n, u32 from, u32 to)
{
    u32 i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_TRANSFER, from, LEDGER_CALLER());
    ok = 0;
    if (!owner_ok(from) || !owner_ram(to) || !pfn_span_ok(pfn, n)) goto done;
    for (i = 0; i < (u32)n; i++)
        if (!page_owned(pfn + i, from)) goto done;
    for (i = 0; i < (u32)n; i++) owner_map[pfn + i] = (u8)to;
    ledger_owners[from].pages -= (u32)n;
    ledger_owners[to].pages += (u32)n;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int ledger_reclaim_owner(u32 owner, u32 *pages)
{
    const struct ledger_surface *sf;
    u32 p, i, n, kind;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_RECLAIM, owner, LEDGER_CALLER());
    ok = 0;
    if (!initialized || !owner_ok(owner)) goto done;
    kind = ledger_owners[owner].kind;
    if (kind == LEDGER_KIND_DEVICE || kind == LEDGER_KIND_PERSIST) goto done;
    for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
        sf = &ledger_surfaces[i];
        if (sf->npages && sf->owner == owner) goto done;
    }
    /* 返し忘れが無ければ (pages == 0) 走査しない。AS / MODULE の L2 の
     * ページは全部 eligible かつ allocated。 */
    n = 0;
    for (p = 0; ledger_owners[owner].pages && p < limit_pfn; p++) {
        if (!page_owned(p, owner)) continue;
        give_page(p);
        n++;
        ledger_owners[owner].pages--;
    }
    used_pages -= n;
    ledger_reclaim_pages += n;
    if (pages) *pages = n;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int ledger_claim_fixed(u32 owner, u32 first, u32 end)
{
    u32 p;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_CLAIM, owner, LEDGER_CALLER());
    ok = 0;
    if (!initialized || !owner_ram(owner) || first >= end || end > limit_pfn)
        goto done;
    for (p = first; p < end; p++) {
        if (bit(eligible, p) && bit(bitmap, p) && owner_map[p] != owner) {
            ledger_claim_refused++;
            goto done;
        }
    }
    for (p = first; p < end; p++) {
        if (bit(eligible, p) && !bit(bitmap, p)) {
            take_page(p, owner);
            used_pages++;
            ledger_owners[owner].pages++;
        }
    }
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

/* 区間の表の末尾に 1 本足す (res_mask / span_set は 0)。呼び手は IRQ 保存区間
 * の中で検査を済ませ、空きがあることを確かめている。OUTSIDE は範囲から自動。 */
static struct ledger_region *region_put(u32 type, u32 owner, u32 first, u32 end,
                                        u32 cache, u32 rflags)
{
    struct ledger_region *r;
    r = &ledger_regions[ledger_region_count++];
    r->first = first;
    r->end = end;
    r->type = (u8)type;
    r->owner = (u8)owner;
    r->cache = (u8)cache;
    r->flags = (u8)(rflags | (end > limit_pfn ? LEDGER_RF_OUTSIDE : 0));
    r->res_mask = 0;
    r->span_set = 0;
    r->pad = 0;
    return r;
}

int ledger_register_region(u32 type, u32 owner, u32 first, u32 end, u32 cache,
                           u32 rflags)
{
    u32 i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (!initialized || !paging_boot_context() || !owner_ok(owner) ||
        type < LEDGER_R_FIXED || type > LEDGER_R_SURFACE_BACKING ||
        type == LEDGER_R_DEVICE || first >= end || end > PHYSMEM_MAX_PFN ||
        cache > LEDGER_CACHE_UC || (rflags & ~LEDGER_RF_PERMANENT) ||
        ledger_region_count >= LEDGER_MAX_REGIONS) goto done;
    for (i = 0; i < ledger_region_count; i++)
        if (first < ledger_regions[i].end && ledger_regions[i].first < end)
            goto done;
    region_put(type, owner, first, end, cache, rflags);
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

/* Counts and owner totals must describe one instant: IRQ allocations/frees
 * prevent a safe chunked snapshot. Lifecycle callers are limited to boot,
 * launch, GUI handoff and V86 return; never poll/yield/resume. */
int ledger_selfcheck(const char *tag)
{
    u32 counts[LEDGER_MAX_OWNERS];
    const struct ledger_region *r, *g;
    u32 p, i, j, o, live;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = initialized;
    for (i = 0; i < LEDGER_MAX_OWNERS; i++) counts[i] = 0;
    live = 0;
    for (p = 0; ok && p < limit_pfn; p++) {
        o = owner_map[p];
        if (bit(bitmap, p)) {
            live++;
            if (!bit(eligible, p) || !o) ok = 0;
        } else if (o && bit(eligible, p)) ok = 0;
        if (o) {
            if (!owner_ok(o)) ok = 0;
            counts[o]++;
        }
    }
    if (live != used_pages) ok = 0;
    for (i = 0; ok && i < LEDGER_MAX_OWNERS; i++)
        if (counts[i] != ledger_owners[i].pages) ok = 0;
    for (i = 0; ok && i < ledger_region_count; i++) {
        r = &ledger_regions[i];
        if (!owner_ok(r->owner) || r->first >= r->end) ok = 0;
        for (j = 0; j < i; j++) {
            g = &ledger_regions[j];
            /* 装置の予約は背景を細分してよい (§3-1 の BACKGROUND)。背景は
             * 起動時の登録 (重なりを断る) なので、重なる DEVICE より必ず前に
             * 並ぶ — 後ろの r が DEVICE、前の g が BACKGROUND の向きだけ許す。 */
            if (r->first < g->end && g->first < r->end &&
                !(r->type == LEDGER_R_DEVICE && g->type == LEDGER_R_BACKGROUND))
                ok = 0;
        }
    }
    for (i = 0; ok && i < LEDGER_MAX_SURFACES; i++)
        if (ledger_surfaces[i].npages && !owner_ok(ledger_surfaces[i].owner))
            ok = 0;
    if (!ok) {
        ledger_check_fail++;
        ledger_check_tag = tag;
    }
    irq_restore(flags);
    return ok;
}

