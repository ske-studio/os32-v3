    case GF_INIT_ZM_CR_GROUP:
        for (u32 k = 0; k < v->count; k++) {
            u32 index = r[pc+2+2*k], value = r[pc+3+2*k];
            t = key_for(p, 0, RECOVER_CRTC, index, GF_STATE_HEAD(next_state));
            write_node(d, p, s, next_state, e++, REC_SELECT, t, 0, index, false);
            write_node(d, p, s, next_state, e++, REC_DATA, t, 0, value, false);
            if (index == RECOVER_CR44) next_state = (value == RECOVER_CR44_HEAD1 ? GF_STATE_HEAD1 : 0)|(state&GF_STATE_SKIP);
        }
        break;
