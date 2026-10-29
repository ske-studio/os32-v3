    case GF_INIT_ZM_REG_SEQUENCE:
        o->target = le(r, pc + 1, 4); o->ref = pc + 6;
        if (!o->count || o->count - 1 > (0xffffffffU - o->target) / 4) return -1;
        break;
