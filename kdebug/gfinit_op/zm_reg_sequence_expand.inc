    case GF_INIT_ZM_REG_SEQUENCE:
        for (u32 k = 0; k < v->count; k++) write_node(d, p, s, state, e++, REC_WRITE,
            key_for(p, v->target+4*k, 0, 0, head), 0, le(r, v->ref+4*k, 4), false);
        break;
