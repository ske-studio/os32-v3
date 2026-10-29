    case GF_INIT_IO_FLAG_CONDITION: case GF_INIT_CONDITION: case GF_INIT_MACRO: {
        u32 entry = r[pc + 1];
        u32 base = o->opcode == GF_INIT_IO_FLAG_CONDITION ? GF_INIT_IO_FLAG_TABLE : o->opcode == GF_INIT_CONDITION ? GF_INIT_CONDITION_TABLE : GF_INIT_MACRO_TABLE;
        u32 width = o->opcode == GF_INIT_IO_FLAG_CONDITION ? GF_INIT_IO_FLAG_ENTRY : o->opcode == GF_INIT_CONDITION ? GF_INIT_CONDITION_ENTRY : GF_INIT_MACRO_ENTRY;
        u32 count = o->opcode == GF_INIT_IO_FLAG_CONDITION ? GF_INIT_IO_FLAG_COUNT : o->opcode == GF_INIT_CONDITION ? GF_INIT_CONDITION_COUNT : GF_INIT_MACRO_COUNT;
        if (entry >= count) return -1;
        o->ref = base + entry * width; o->index = entry;
        if (span(size, o->ref, width)) return -1;
        if (o->opcode == GF_INIT_IO_FLAG_CONDITION) {
            o->port = le(r, o->ref, 2); o->index = r[o->ref + GF_INIT_IO_FLAG_INDEX];
            o->mask = r[o->ref + GF_INIT_IO_FLAG_REG_MASK]; o->shift = r[o->ref + GF_INIT_IO_FLAG_SHIFT];
            if (o->shift >= GF_SHIFT_BITS || span(size, le(r, o->ref + GF_INIT_IO_FLAG_DATA, 2),
                                      (o->mask >> o->shift) + 1)) return -1;
        } else {
            o->target = le(r, o->ref, 4);
            o->mask = le(r, o->ref + (o->opcode == GF_INIT_CONDITION ?
                GF_INIT_CONDITION_MASK : GF_INIT_MACRO_VALUE), 4);
            o->value = o->opcode == GF_INIT_CONDITION ? le(r, o->ref + GF_INIT_CONDITION_VALUE, 4) : o->mask;
        }
        break;
    }
