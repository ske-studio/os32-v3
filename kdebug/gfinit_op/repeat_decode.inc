    case GF_INIT_REPEAT:
        o->count = r[pc + 1];
        if (!o->count || o->count >= 255) return -1;
        break;
