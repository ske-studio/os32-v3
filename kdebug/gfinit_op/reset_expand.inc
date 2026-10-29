    case GF_INIT_RESET: {
        u32 saved = key_for(p, RECOVER_PBUS184C, 0, 0, head), producer = d->plan.node_count;
        write_node(d, p, s, state, e++, REC_WRITE, saved, GF_INIT_RESET_KEEP, 0, true);
        write_node(d, p, s, state, e++, REC_WRITE, t, 0, v->mask, false);
        control(d, p, s, state, GF_INIT_C, GF_INIT_RESET_US, 0, 0, 1);
        write_node(d, p, s, state, e++, REC_WRITE, t, 0, v->value, false);
        struct recover_plan_arg a[] = {IMM(saved), SAVED(p->count)};
        u32 consumer = emit(d, p, s, state, e++, REC_WRITE, a, 2);
        d->captures[0] = (struct recover_capture){p->count, producer, consumer, saved, 0};
        write_node(d, p, s, state, e, REC_WRITE, key_for(p, RECOVER_PBUS1850, 0, 0, head), 0, 0, false); break;
    }
