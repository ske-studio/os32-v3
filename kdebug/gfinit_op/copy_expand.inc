    case GF_INIT_COPY: case GF_INIT_COPY_NV_REG: {
        if (op == GF_INIT_COPY) write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
        u32 src = key_for(p, v->ref, 0, 0, head);
        struct recover_plan_arg a[] = {IMM(t), READ(op == GF_INIT_COPY ? t : src), READ(op == GF_INIT_COPY ? src : t),
            IMM(v->shift), IMM(op == GF_INIT_COPY ? v->value : le(r, pc+6, 4)),
            IMM(op == GF_INIT_COPY ? v->mask : le(r, pc+10, 4)), IMM(v->mask)};
        if (src == RECOVER_NONE) return -1;
        emit(d, p, s, state, e, op == GF_INIT_COPY ? REC_DATA : REC_WRITE, a, op == GF_INIT_COPY ? 6 : 7); break;
    }
