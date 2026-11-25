    case GF_INIT_RESET: case GF_INIT_NV_REG:
        o->target = le(r, pc + 1, 4); o->mask = le(r, pc + 5, 4);
        o->value = le(r, pc + 9, 4); break;
