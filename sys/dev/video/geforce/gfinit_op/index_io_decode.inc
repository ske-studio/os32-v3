    case GF_INIT_ZM_INDEX_IO: case GF_INIT_INDEX_IO:
        o->port = le(r, pc + 1, 2); o->index = r[pc + 3];
        o->mask = o->opcode == GF_INIT_INDEX_IO ? r[pc + 4] : 0;
        o->value = r[pc + len - 1]; break;
