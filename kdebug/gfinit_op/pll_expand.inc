/* Solver runs before any bus access. All runtime values come from checked
 * captures and every write uses the recover-owned rule for this pc/substep. */
static int expand_pll(struct gfinit_decode *d, const u8 *r, const struct recover_profile *p,
                      const struct gfinit_scheduled *s, u32 state, u32 khz)
{
    const struct gfinit_instruction *v = &d->insns[s->instruction];
    struct gfinit_pll_limits limits;
    struct gfinit_pll_solution solution;
    struct gfinit_pll_sequence q;
    if (gfinit_pll_parse(r, d->plan.rom_size, GF_INIT_PLL_TABLE, v->target, &limits) ||
        gfinit_pll_solve(&limits, khz, &solution) ||
        gfinit_pll_sequence(v->target, &limits, &solution, &q)) return -1;
    u32 sources[GF_PLL_WRITES_MAX], producers[GF_PLL_WRITES_MAX], count = 0;
    for (u32 i = 0; i < q.count; i++) if (q.writes[i].source) {
        u32 source = q.writes[i].source, j = 0;
        for (; j < count; j++) if (sources[j] == source) break;
        if (j == count) {
            sources[count] = source;
            producers[count++] = read_node(d, p, s, state, key_for(p, source, 0, 0, 0));
        }
    }
    u32 base = GF_PLL_SUBSTEP + GF_PLL_SUBSTEP_STRIDE *
        (v->target == GF_PLL_CORE ? 0 : v->target == GF_PLL_MEMORY ? 1 : 2);
    for (u32 i = 0; i < q.count; i++) {
        const struct gfinit_pll_write *w = &q.writes[i];
        u32 key = key_for(p, w->reg, 0, 0, 0);
        if (key == RECOVER_NONE) return -1;
        if (!w->source) write_node(d, p, s, state, base + i, REC_WRITE, key, 0, w->value, false);
        else {
            u32 c = d->plan.capture_count, j = 0;
            if (c == RECOVER_CAPTURE_MAX) return -1;
            for (; j < count; j++) if (sources[j] == w->source) break;
            if (j == count) return -1;
            struct recover_plan_arg a[] = {IMM(key), SAVED(p->count + c), IMM(w->keep), IMM(w->value)};
            u32 consumer = emit(d, p, s, state, base + i, REC_WRITE, a, 4);
            d->captures[c] = (struct recover_capture){p->count + c, producers[j], consumer,
                key_for(p, w->source, 0, 0, 0), 0};
            d->plan.capture_count++;
        }
    }
#if GF_VPLL_PROBE
    if (s->main_id == RECOVER_VPLL_MAIN && v->pc == RECOVER_VPLL_PC && v->target == GF_PLL_VIDEO) {
        u32 switch_key = key_for(p, RECOVER_VPLL_SWITCH_OFF, 0, 0, 0);
        if (switch_key == RECOVER_NONE || q.count != 5) return -1;
        write_node(d, p, s, state, RECOVER_VPLL_STAGE2_FIRST, REC_WRITE, switch_key, 0, RECOVER_VPLL_SWITCH_ON, false);
        for (u32 i = 0; i < q.count; i++) {
            const struct gfinit_pll_write *w = &q.writes[i];
            u32 c = d->plan.capture_count, j = 0;
            if (!w->source || c == RECOVER_CAPTURE_MAX) return -1;
            for (; j < count; j++) if (sources[j] == w->source) break;
            if (j == count) return -1;
            u32 key = key_for(p, w->reg, 0, 0, 0);
            struct recover_plan_arg a[] = {IMM(key), SAVED(p->count + c), IMM(w->keep), IMM(w->value)};
            u32 consumer = emit(d, p, s, state, RECOVER_VPLL_REPEAT_FIRST + i, REC_WRITE, a, 4);
            d->captures[c] = (struct recover_capture){p->count + c, producers[j], consumer,
                key_for(p, w->source, 0, 0, 0), 0};
            d->plan.capture_count++;
        }
        write_node(d, p, s, state, RECOVER_VPLL_STAGE2_LAST, REC_WRITE, switch_key, 0, RECOVER_VPLL_SWITCH_DEFAULT, false);
    }
#endif
    return d->error ? -1 : 0;
}
