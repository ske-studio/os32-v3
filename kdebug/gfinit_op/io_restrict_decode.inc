    case GF_INIT_IO_RESTRICT_PROG: case GF_INIT_IO_RESTRICT_PLL2:
        o->port = le(r, pc + 1, 2); o->index = r[pc + 3];
        o->mask = r[pc + 4]; o->shift = r[pc + 5];
        o->target = le(r, pc + 7, 4); o->ref = pc + 11;
        if (o->shift >= GF_SHIFT_BITS || !o->count || (o->mask >> o->shift) >= o->count) return -1;
        break;
