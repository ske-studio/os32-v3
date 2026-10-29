    case GF_INIT_SUB:
        o->index = r[pc + 1];
        if (o->index != GF_INIT_SUB_FIRST && o->index != GF_INIT_SUB_FIRST + 1 && o->index != GF_INIT_SUB_LAST) return -1;
        if (span(size, GF_INIT_SCRIPT_TABLE + o->index * 2, 2)) return -1;
        o->ref = le(r, GF_INIT_SCRIPT_TABLE + o->index * 2, 2); break;
