    case GF_INIT_CR_INDEX_ADDRESS_LATCHED: {
        write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
        u32 producer = read_node(d, p, s, state, t);
        u32 other = key_for(p, 0, RECOVER_CRTC, v->value, head);
        for (u32 k = 0; k < v->count; k++) {
            if (k) write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
            write_node(d, p, s, state, e++, REC_DATA, t, 0, v->mask+k, false);
            write_node(d, p, s, state, e++, REC_SELECT, other, 0, v->value, false);
            write_node(d, p, s, state, e++, REC_DATA, other, 0, r[v->ref+k], false);
        }
        write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
        struct recover_plan_arg a[] = {IMM(t), SAVED(p->count+1+head)};
        u32 consumer = emit(d, p, s, state, e, REC_DATA, a, 2);
        d->captures[1+head] = (struct recover_capture){p->count+1+head, producer, consumer, t, 0}; break;
    }
