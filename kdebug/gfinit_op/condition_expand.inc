    case GF_INIT_CONDITION: case GF_INIT_IO_FLAG_CONDITION: {
        if (op == GF_INIT_IO_FLAG_CONDITION) write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
        read_node(d, p, s, state, t);
        u32 branch;
        if (op == GF_INIT_CONDITION) branch = control(d, p, s, state, GF_INIT_C, r[pc+1], v->mask, v->value, 3);
        else {
            struct recover_plan_arg a[] = {IMM(r[pc+1]), IMM(v->mask), IMM(v->shift),
                IMM(le(r, v->ref+GF_INIT_IO_FLAG_DATA, 2)), IMM(r[v->ref+GF_INIT_IO_FLAG_MASK]), IMM(r[v->ref+GF_INIT_IO_FLAG_VALUE])};
            branch = emit(d, p, s, state, GF_INIT_C, RECOVER_NONE, a, 6);
        }
        if (branch == RECOVER_NONE) return -1;
        d->nodes[branch].next[0] = destination(next_step, state|GF_STATE_SKIP);
        d->nodes[branch].next[1] = destination(next_step, state);
        d->reachable[next_step] |= (1U << (state|GF_STATE_SKIP)) | (1U << state);
        return d->error ? -1 : 0;
    }
