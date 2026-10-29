    case GF_INIT_CR_INDEX_ADDRESS_LATCHED:
        o->index = r[pc + 1]; o->value = r[pc + 2]; o->ref = pc + 5;
        o->mask = r[pc + 3]; o->port = RECOVER_CRTC;
        if (!o->count || o->count - 1 > 255 - o->mask) return -1;
        break;
