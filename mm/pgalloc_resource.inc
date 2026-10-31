/* Validated device resources and atomic MMIO/RAM reservations. */
/* ======================================================================== */
/*  MMIO 登録と検証済み資源レコード (§3-1・§3-2、T1d、D33・X4)               */
/*                                                                          */
/*  予約権限は資源レコード (decode 範囲 + その根拠) から来る。生の {first,   */
/*  end} だけでは予約できない。1 レコード = decode 区間 1 組 (B10)。         */
/* ======================================================================== */
int ledger_resource_add(const struct ledger_resource *rec, u32 *rid)
{
    u32 i;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (!rec || !rid || !rec->bus || rec->bus > LEDGER_BUS_FIXED ||
        !rec->width_basis || rec->width_basis > LEDGER_WB_PROBE_UNVERIFIED ||
        rec->decode_first > rec->decode_end || rec->decode_end > PHYSMEM_MAX_PFN ||
        rec->map_first > rec->map_end ||
        (rec->map_first < rec->map_end &&
         (rec->map_first < rec->decode_first || rec->map_end > rec->decode_end)))
        goto done;
    for (i = 0; i < LEDGER_MAX_RESOURCES && ledger_resources[i].bus; i++) {}
    if (i == LEDGER_MAX_RESOURCES) {
        ledger_res_overflow++;
        goto done;
    }
    ledger_resources[i] = *rec;
    *rid = i;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int ledger_resource_set_map(u32 rid, u32 first, u32 end)
{
    unsigned int flags = irq_save();
    int ok = 0;
    if (rid < LEDGER_MAX_RESOURCES && ledger_resources[rid].bus) {
        struct ledger_resource *r = &ledger_resources[rid];
        if ((!first && !end) || (first < end && first >= r->decode_first &&
                               end <= r->decode_end)) {
            r->map_first = first;
            r->map_end = end;
            ok = 1;
        }
    }
    irq_restore(flags);
    return ok;
}

int ledger_reserve_set(u32 owner, const struct ledger_span *spans, u32 n)
{
    struct ledger_span s[LEDGER_MAX_SPANS], t;
    const struct ledger_resource *rr;
    const struct ledger_region *g;
    struct ledger_region *r;
    u32 i, j, k, p, mine, hit;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ledger_note(LEDGER_OP_RESERVE, owner, LEDGER_CALLER());
    ok = 0;
    if (!online || !paging_boot_context() || !owner_ok(owner) ||
        ledger_owners[owner].kind != LEDGER_KIND_DEVICE || !spans || !n ||
        n > LEDGER_MAX_SPANS) goto done;
    /* (e) 正規化の前に span ごとに自分の資源レコードと照合する (B10)。空きの
     * レコードは width_basis 0 なのでここで落ちる。res は根拠のビットにして
     * 先頭の順に並べる。 */
    for (i = 0; i < n; i++) {
        t = spans[i];
        if (t.res >= LEDGER_MAX_RESOURCES) goto done;
        rr = &ledger_resources[t.res];
        if (!rr->width_basis || rr->width_basis > LEDGER_WB_GLUE_CONST ||
            t.first >= t.end || t.first < rr->decode_first ||
            t.end > rr->decode_end ||
            (t.kind != LEDGER_SPAN_MMIO && t.kind != LEDGER_SPAN_RAM)) goto done;
        /* 以後 kind は区間のキャッシュ属性で持つ (MMIO = UC、RAM = WB)。 */
        t.kind = t.kind == LEDGER_SPAN_MMIO ? LEDGER_CACHE_UC : LEDGER_CACHE_WB;
        t.res = 1UL << t.res;
        for (j = i; j && s[j - 1].first > t.first; j--) s[j] = s[j - 1];
        s[j] = t;
    }
    /* 正規化: 同種の重なる・接する span を併合 (根拠は和)、異種の重なりは拒否。 */
    k = 0;
    for (i = 0; i < n; i++) {
        if (k && s[i].first <= s[k - 1].end && s[i].kind == s[k - 1].kind) {
            if (s[i].end > s[k - 1].end) s[k - 1].end = s[i].end;
            s[k - 1].res |= s[i].res;
        } else {
            if (k && s[i].first < s[k - 1].end) goto done;
            s[k++] = s[i];
        }
    }
    n = k;
    /* 区間の表との照合 (§3-1 の拒否規則 (a)〜(c))。自分の DEVICE 区間は
     * 完全一致の判定に回す。 */
    mine = hit = 0;
    for (j = 0; j < ledger_region_count; j++) {
        g = &ledger_regions[j];
        if (g->type == LEDGER_R_DEVICE && g->owner == owner) {
            mine++;
            for (i = 0; i < n; i++)
                if (g->first == s[i].first && g->end == s[i].end &&
                    g->res_mask == s[i].res && g->cache == s[i].kind) hit++;
            continue;
        }
        if (g->type == LEDGER_R_BACKGROUND) continue;
        for (i = 0; i < n; i++)
            if (s[i].first < g->end && g->first < s[i].end) goto done;
    }
    /* 同 owner は同一集合だけ冪等 (何も変えない)、部分一致は拒否。 */
    if (mine) {
        ok = hit == n && mine == n;
        goto done;
    }
    if (n > LEDGER_MAX_REGIONS - ledger_region_count) goto done;
    /* (d) 管理範囲の中: L2 に owner があれば拒否 (使用中のページ — allocated
     * ⇔ owner ≠ 0 — と永久予約の両方)、RAM 種別 (WB) は全ページ eligible で
     * 管理範囲の内側。 */
    for (i = 0; i < n; i++) {
        if (s[i].kind == LEDGER_CACHE_WB && s[i].end > limit_pfn) goto done;
        for (p = s[i].first; p < s[i].end && p < limit_pfn; p++)
            if (owner_map[p] ||
                (s[i].kind == LEDGER_CACHE_WB && !bit(eligible, p))) goto done;
    }
    /* All fallible work is above this line. */
    ledger_span_sets++;
    for (i = 0; i < n; i++) {
        for (p = s[i].first; p < s[i].end && p < limit_pfn; p++) {
            if (bit(eligible, p)) {
                eligible[p / BITS_PER_U32] &= ~(1UL << (p % BITS_PER_U32));
                total_pages--;
            }
        }
        r = region_put(LEDGER_R_DEVICE, owner, s[i].first, s[i].end, s[i].kind,
                       LEDGER_RF_PERMANENT);
        r->res_mask = (u16)s[i].res;
        r->span_set = ledger_span_sets;
    }
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

