    case GF_INIT_COPY: case GF_INIT_COPY_NV_REG:
        o->ref = le(r, pc + 1, 4); o->shift = r[pc + 5];
        if (!shift_ok(o->shift) || o->ref & 3U) return -1;
        if (o->opcode == GF_INIT_COPY) {
            o->mask = r[pc + 10]; o->value = r[pc + 6];
            o->port = le(r, pc + 7, 2); o->index = r[pc + 9];
        } else { o->target = le(r, pc + 14, 4); o->mask = le(r, pc + 18, 4); }
        break;
