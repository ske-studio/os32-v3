    case GF_INIT_IO_RESTRICT_PROG: case GF_INIT_IO_RESTRICT_PLL2: {
        u32 src = key_for(p, 0, v->port, v->index, head);
        write_node(d, p, s, state, e++, REC_SELECT, src, 0, v->index, false);
        read_node(d, p, s, state, src);
        u32 branches[GF_INIT_COUNT_MAX], ends[GF_INIT_COUNT_MAX];
        for (u32 k = 0; k < v->count; k++) {
            struct recover_plan_arg a[] = {IMM(v->mask), IMM(v->shift), IMM(k), IMM(v->count)};
            branches[k] = emit(d, p, s, state, GF_INIT_C+k, RECOVER_NONE, a, 4);
            if (branches[k] == RECOVER_NONE) return -1;
        }
        for (u32 k = 0; k < v->count; k++) {
            d->nodes[branches[k]].next[0] = k+1 < v->count ? branches[k+1] : destination(next_step, state);
            if (op == GF_INIT_IO_RESTRICT_PLL2 && k && le(r, v->ref+4*k, 4) == le(r, v->ref, 4)) {
                d->nodes[branches[k]].next[1] = ends[0]; continue;
            }
            ends[k] = d->plan.node_count;
            d->nodes[branches[k]].next[1] = ends[k];
            if (op == GF_INIT_IO_RESTRICT_PROG) write_node(d, p, s, state, e+k, REC_WRITE, key_for(p, v->target, 0, 0, head), 0, le(r, v->ref+4*k, 4), false);
            else if (expand_pll(d, r, p, s, state, le(r, v->ref+4*k, 4))) return -1;
            if (d->error) return -1;
            d->nodes[d->plan.node_count-1].next[0] = d->nodes[d->plan.node_count-1].next[1] = destination(next_step, state);
        }
        d->reachable[next_step] |= 1U << state;
        return d->error ? -1 : 0;
    }
