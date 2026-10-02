static int h3_identity(volatile H3Block *b)
{
    if (!b->owner || !b->generation) return 0;
    b->phase = H3_IDENTIFIED;
    return 1;
}

static unsigned int h3_consume(volatile H3Block *b)
{
    unsigned int mode;
    if (b->phase != H3_RESUMED || b->arm != 1 || b->consumed != 0)
        return 0;
    mode = b->mode;
    if (mode < H3_PF || mode > H3_KAPI_LOOP) {
        b->phase = H3_ERROR;
        return 0;
    }
    b->arm = 0;
    b->consumed++;
    b->phase = H3_ARMED;
    return mode;
}
