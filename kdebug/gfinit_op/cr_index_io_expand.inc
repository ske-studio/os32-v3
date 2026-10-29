    case GF_INIT_CR: case GF_INIT_ZM_CR: case GF_INIT_ZM_INDEX_IO: case GF_INIT_INDEX_IO:
        write_node(d, p, s, state, e++, REC_SELECT, t, 0, v->index, false);
        write_node(d, p, s, state, e, REC_DATA, t, v->mask, v->value, op == GF_INIT_CR || op == GF_INIT_INDEX_IO);
        if (v->port == RECOVER_CRTC && v->index == RECOVER_CR44) next_state = (v->value == RECOVER_CR44_HEAD1 ? GF_STATE_HEAD1 : 0) | (state&GF_STATE_SKIP);
        break;
